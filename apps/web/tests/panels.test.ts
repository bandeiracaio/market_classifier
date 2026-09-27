// Panel smoke (plan Task 9): every PanelKind opens in the real WASM terminal without
// console errors and the render loop keeps running.
import { test, expect } from '@playwright/test';

const PANEL_KINDS = 13;

test.describe('panels (requires WASM build)', () => {
	test.beforeEach(async ({ page }) => {
		const response = await page.request.get('/wasm/market_classifier.js').catch(() => null);
		test.skip(
			response === null || response.status() !== 200,
			'WASM build not present — run the wasm-release build preset first'
		);
	});

	test('each panel kind opens without console errors', async ({ page }) => {
		const errors: string[] = [];
		page.on('pageerror', (error) => errors.push(error.message));
		page.on('console', (message) => {
			if (message.type() === 'error') errors.push(message.text());
		});
		await page.goto('/');
		await page.waitForFunction(() => document.body.dataset.wasmStatus === 'ready');
		for (let kind = 0; kind < PANEL_KINDS; kind++) {
			const id = await page.evaluate(
				(k) =>
					(
						window as unknown as { mcTerminal?: { _mc_debug_open_panel(k: number): number } }
					).mcTerminal?._mc_debug_open_panel(k) ?? -1,
				kind
			);
			expect(id, `panel kind ${kind}`).toBeGreaterThan(0);
		}
		const frames = async () => Number(await page.evaluate(() => document.body.dataset.frameCount));
		const before = await frames();
		await page.waitForTimeout(2000);
		expect(await frames()).toBeGreaterThan(before);
		expect(errors).toEqual([]);
	});
});
