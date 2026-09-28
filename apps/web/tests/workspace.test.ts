// Workspace persistence (plan Task 10 / packet §12 #6). Requires the WASM build.
import { installFakeVenues } from './fake-venues';
import { isVenueNoise } from './venue-noise';
import { test, expect, type Page } from '@playwright/test';

type Terminal = {
	_mc_debug_activate_layout(i: number): number;
	_mc_debug_save_layout_as(ptr: number): number;
	_mc_workspace_export(): number;
	_malloc(n: number): number;
	stringToUTF8(s: string, p: number, n: number): void;
	UTF8ToString(p: number): string;
};

async function ready(page: Page) {
	await page.waitForFunction(() => document.body.dataset.wasmStatus === 'ready');
	await page.waitForFunction(() => document.body.dataset.workspaceSource !== undefined);
}

async function activeLayoutName(page: Page): Promise<string> {
	return page.evaluate(() => {
		const t = (window as unknown as { mcTerminal: Terminal }).mcTerminal;
		const ws = JSON.parse(t.UTF8ToString(t._mc_workspace_export()));
		const builtins = ['Overview', 'Tape Reader', 'Footprint', 'Liquidity', 'Derivatives'];
		return ws.active < builtins.length
			? builtins[ws.active]
			: ws.layouts[ws.active - builtins.length].name;
	});
}

test.describe('workspace (requires WASM build)', () => {
	test.beforeEach(async ({ page }) => {
		const response = await page.request.get('/wasm/market_classifier.js').catch(() => null);
		test.skip(response === null || response.status() !== 200, 'WASM build not present');
		await installFakeVenues(page); // deterministic, region-independent venue data
	});

	test('first run opens Overview and every preset opens without console errors', async ({
		page
	}) => {
		const errors: string[] = [];
		page.on('pageerror', (e) => errors.push(e.message));
		page.on('console', (m) => {
			if (m.type() === 'error' && !isVenueNoise(m.text() + m.location().url)) errors.push(m.text());
		});
		await page.goto('/');
		await ready(page);
		expect(await activeLayoutName(page)).toBe('Overview');
		for (let i = 0; i < 5; i++) {
			await page.evaluate(
				(k) =>
					(window as unknown as { mcTerminal: Terminal }).mcTerminal._mc_debug_activate_layout(k),
				i
			);
			await page.waitForTimeout(300);
		}
		expect(errors).toEqual([]);
	});

	test('saved layout survives reload; corrupt latest falls back; export-reset-import restores', async ({
		page
	}) => {
		await page.goto('/');
		await ready(page);
		await page.evaluate(() => {
			const t = (window as unknown as { mcTerminal: Terminal }).mcTerminal;
			const p = t._malloc(16);
			t.stringToUTF8('Mine', p, 16);
			t._mc_debug_save_layout_as(p);
		});
		await page.waitForTimeout(1500); // autosave debounce
		await page.reload();
		await ready(page);
		expect(await activeLayoutName(page)).toBe('Mine');

		// Corrupt `latest`; last-good (promoted on the successful load) must win.
		await page.evaluate(
			() =>
				new Promise<void>((resolve) => {
					const open = indexedDB.open('market-classifier', 1);
					open.onsuccess = () => {
						const tx = open.result.transaction('workspace', 'readwrite');
						tx.objectStore('workspace').put('{garbage', 'latest');
						tx.oncomplete = () => resolve();
					};
				})
		);
		await page.reload();
		await ready(page);
		await expect(page.getByTestId('mc-notice')).toContainText('Restored last good workspace');
		expect(await activeLayoutName(page)).toBe('Mine');

		// Export -> reset -> import.
		const exported = await page.evaluate(() => {
			const t = (window as unknown as { mcTerminal: Terminal }).mcTerminal;
			return t.UTF8ToString(t._mc_workspace_export());
		});
		await page.evaluate(() =>
			(
				window as unknown as { mcTerminal: { _mc_workspace_reset(): void } }
			).mcTerminal._mc_workspace_reset()
		);
		expect(await activeLayoutName(page)).toBe('Overview');
		await page.getByTestId('mc-import-input').setInputFiles({
			name: 'workspace.json',
			mimeType: 'application/json',
			// Node Buffer without @types/node (no new dependency).
			buffer: (globalThis as unknown as { Buffer: { from(s: string): never } }).Buffer.from(
				exported
			)
		});
		await expect(page.getByTestId('mc-notice')).toContainText('Workspace imported');
		expect(await activeLayoutName(page)).toBe('Mine');
	});
});
