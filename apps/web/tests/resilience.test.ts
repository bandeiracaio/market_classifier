// Acceptance #3 and #4 in the browser: a dropped venue reconnects without touching the
// other venue, and a silent feed turns Stale with visible age. Venues are faked offline.
import { test, expect, type Page } from '@playwright/test';
import { installFakeVenues } from './fake-venues';

const LIVE = '1';
const STALE = '2';
const RECONNECTING = '3';

const phase = (page: Page, venue: 'binance' | 'hyperliquid') =>
	page.evaluate((v) => document.body.dataset[`${v}Phase`], venue);

test.describe('venue resilience (requires WASM build)', () => {
	test.beforeEach(async ({ page }) => {
		const response = await page.request.get('/wasm/market_classifier.js').catch(() => null);
		test.skip(response === null || response.status() !== 200, 'WASM build not present');
		await installFakeVenues(page);
		await page.goto('/');
		await page.waitForFunction(
			() =>
				document.body.dataset.binancePhase === '1' && document.body.dataset.hyperliquidPhase === '1'
		);
	});

	test('dropping Hyperliquid reconnects it and leaves Binance live', async ({ page }) => {
		const binanceReconnectsBefore = await page.evaluate(() =>
			(
				window as unknown as { mcTerminal: { _mc_venue_stat(v: number, w: number): number } }
			).mcTerminal._mc_venue_stat(0, 4)
		);
		await page.evaluate(() =>
			(window as unknown as { fakeVenues: { drop(v: string): void } }).fakeVenues.drop(
				'hyperliquid'
			)
		);
		await page.waitForFunction(() => document.body.dataset.hyperliquidPhase === '3');
		expect(await phase(page, 'hyperliquid')).toBe(RECONNECTING);
		expect(await phase(page, 'binance')).toBe(LIVE);
		// Backoff (<= 1 s for the first attempt) then recovery.
		await page.waitForFunction(() => document.body.dataset.hyperliquidPhase === '1', undefined, {
			timeout: 10_000
		});
		expect(await phase(page, 'binance')).toBe(LIVE);
		const binanceReconnectsAfter = await page.evaluate(() =>
			(
				window as unknown as { mcTerminal: { _mc_venue_stat(v: number, w: number): number } }
			).mcTerminal._mc_venue_stat(0, 4)
		);
		expect(binanceReconnectsAfter).toBe(binanceReconnectsBefore);
	});

	test('a silent Binance feed becomes Stale with age of at least 5 s', async ({ page }) => {
		await page.evaluate(() =>
			(
				window as unknown as { fakeVenues: { silence(v: string, ms: number): void } }
			).fakeVenues.silence('binance', 8000)
		);
		await page.waitForTimeout(6000);
		expect(await phase(page, 'binance')).toBe(STALE);
		const age = Number(await page.evaluate(() => document.body.dataset.binanceAgeMs));
		expect(age).toBeGreaterThanOrEqual(5000);
		expect(await phase(page, 'hyperliquid')).toBe(LIVE);
		await page.waitForFunction(() => document.body.dataset.binancePhase === '1', undefined, {
			timeout: 10_000
		});
	});
});
