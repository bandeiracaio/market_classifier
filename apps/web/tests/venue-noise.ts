// Live venue traffic is outside the app's control in CI (e.g. Binance answers 451 to some
// regions; sockets may be refused). Tests assert on app assets and runtime errors, so
// failures that originate from venue hosts are filtered out — nothing else is.
const VENUE_HOSTS = ['fapi.binance.com', 'fstream.binance.com', 'api.hyperliquid.xyz'];

export function isVenueNoise(text: string): boolean {
	return VENUE_HOSTS.some((host) => text.includes(host));
}
