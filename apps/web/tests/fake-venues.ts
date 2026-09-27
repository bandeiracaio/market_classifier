// Deterministic offline venues for Playwright: REST via page.route, WebSockets via a
// scripted replacement installed before the app loads (Playwright 1.47 has no
// routeWebSocket). Frames use the venues' documented shapes (docs/protocols/*.md).
import type { Page } from '@playwright/test';

const exchangeInfo = {
	symbols: [
		{
			symbol: 'BTCUSDT',
			contractType: 'PERPETUAL',
			status: 'TRADING',
			baseAsset: 'BTC',
			quoteAsset: 'USDT',
			filters: [
				{ filterType: 'PRICE_FILTER', tickSize: '0.10' },
				{ filterType: 'LOT_SIZE', stepSize: '0.001' }
			]
		}
	]
};
const meta = [
	{ universe: [{ name: 'BTC', szDecimals: 5 }] },
	[{ markPx: '84504.0', oraclePx: '84537.0', funding: '0.0000125' }]
];

export async function installFakeVenues(page: Page): Promise<void> {
	await page.route('https://fapi.binance.com/**', async (route) => {
		const url = route.request().url();
		const body = url.includes('exchangeInfo')
			? exchangeInfo
			: url.includes('depth')
				? { lastUpdateId: 1, T: Date.now(), bids: [['84499.5', '1']], asks: [['84499.6', '1']] }
				: url.includes('openInterest')
					? { symbol: 'BTCUSDT', openInterest: '94034.253', time: Date.now() }
					: [];
		await route.fulfill({ contentType: 'application/json', body: JSON.stringify(body) });
	});
	await page.route('https://api.hyperliquid.xyz/**', async (route) => {
		const request = route.request().postData() ?? '';
		const body = request.includes('metaAndAssetCtxs') ? meta : [];
		await route.fulfill({ contentType: 'application/json', body: JSON.stringify(body) });
	});
	await page.addInitScript(() => {
		type Listener = ((event: unknown) => void) | null;
		const silencedUntil: Record<string, number> = {};
		const sockets: FakeSocket[] = [];
		const venueOf = (url: string) => (url.includes('binance') ? 'binance' : 'hyperliquid');

		class FakeSocket {
			url: string;
			readyState = 0;
			onopen: Listener = null;
			onmessage: Listener = null;
			onclose: Listener = null;
			onerror: Listener = null;
			private timer: number | undefined;
			private n = 0;
			constructor(url: string) {
				this.url = url;
				sockets.push(this);
				setTimeout(() => {
					this.readyState = 1;
					this.onopen?.({});
					this.timer = window.setInterval(() => this.emit(), 100);
				}, 20);
			}
			send() {}
			close() {
				this.readyState = 3;
				window.clearInterval(this.timer);
			}
			drop() {
				this.close();
				this.onclose?.({});
			}
			private emit() {
				const venue = venueOf(this.url);
				if ((silencedUntil[venue] ?? 0) > Date.now()) return;
				const t = Date.now();
				const i = ++this.n;
				let data: unknown;
				if (venue === 'binance' && this.url.includes('/public/')) {
					data = {
						stream: 'btcusdt@bookTicker',
						data: {
							e: 'bookTicker',
							u: i,
							s: 'BTCUSDT',
							b: '84499.50',
							B: '1',
							a: '84499.60',
							A: '1',
							T: t,
							E: t
						}
					};
				} else if (venue === 'binance') {
					data = {
						stream: 'btcusdt@aggTrade',
						data: {
							e: 'aggTrade',
							E: t,
							a: i,
							s: 'BTCUSDT',
							p: '84499.50',
							q: '0.001',
							T: t,
							m: i % 2 === 0
						}
					};
				} else {
					data = {
						channel: 'trades',
						data: [
							{ coin: 'BTC', side: i % 2 ? 'B' : 'A', px: '84504.0', sz: '0.01', time: t, tid: i }
						]
					};
				}
				this.onmessage?.({ data: JSON.stringify(data) });
			}
		}
		(window as unknown as { WebSocket: unknown }).WebSocket = FakeSocket;
		(window as unknown as { fakeVenues: unknown }).fakeVenues = {
			drop(venue: string) {
				for (const s of sockets) if (s.readyState === 1 && venueOf(s.url) === venue) s.drop();
			},
			silence(venue: string, ms: number) {
				silencedUntil[venue] = Date.now() + ms;
			}
		};
	});
}
