// Smoke tests — verify the SvelteKit host boots and the WASM terminal initializes.
//
// These tests do NOT rely on pixel content — they use data attributes set by the
// WASM module (data-wasm-status, data-frame-count) and observable DOM state.

import { test, expect } from '@playwright/test';

test.describe('host boot', () => {
	test('page loads without fatal errors', async ({ page }) => {
		const pageErrors: string[] = [];
		const failedRequests: string[] = [];
		const errorResponses: string[] = [];
		page.on('pageerror', (error) => pageErrors.push(error.message));
		page.on('requestfailed', (request) => failedRequests.push(request.url()));
		page.on('response', (response) => {
			if (response.status() >= 400) errorResponses.push(`${response.status()} ${response.url()}`);
		});

		await page.goto('/');

		// The canvas element must exist
		const canvas = page.locator('canvas#canvas');
		await expect(canvas).toBeAttached();
		expect(pageErrors).toHaveLength(0);
		expect(failedRequests).toHaveLength(0);
		expect(errorResponses).toHaveLength(0);
	});

	test('missing WASM produces an actionable recovery state', async ({ page }) => {
		await page.route('**/wasm/market_classifier.js', (route) => route.abort());
		await page.goto('/');
		const error = page.getByTestId('mc-error');
		await expect(error).toContainText('Failed to load terminal assets');
		await expect(page.getByRole('button', { name: 'Reload' })).toBeVisible();
	});
});

test.describe('WASM terminal (requires WASM build)', () => {
	// These tests are skipped when the WASM artifact is absent; they pass when the
	// WASM build preset has been run and artifacts are in apps/web/static/wasm/.
	test.beforeEach(async ({ page }) => {
		const response = await page.request.get('/wasm/market_classifier.js').catch(() => null);
		test.skip(
			response === null || response.status() === 404,
			'WASM build not present — run the wasm-release build preset first'
		);
	});

	test('WASM ready state is set after boot', async ({ page }) => {
		await page.goto('/');
		// Wait for WASM to signal readiness via data-wasm-status="ready"
		await page.waitForFunction(() => document.body.dataset.wasmStatus === 'ready', {
			timeout: 30_000
		});
		const status = await page.evaluate(() => document.body.dataset.wasmStatus);
		expect(status).toBe('ready');
	});

	test('render heartbeat increments frame count', async ({ page }) => {
		await page.goto('/');
		await page.waitForFunction(() => document.body.dataset.wasmStatus === 'ready', {
			timeout: 30_000
		});
		// Verify at least 5 frames have rendered (proves the loop is alive)
		await page.waitForFunction(() => parseInt(document.body.dataset.frameCount ?? '0') >= 5, {
			timeout: 10_000
		});
		const frameCount = await page.evaluate(() => parseInt(document.body.dataset.frameCount ?? '0'));
		expect(frameCount).toBeGreaterThanOrEqual(5);
	});

	test('bounded render telemetry is published', async ({ page }) => {
		await page.goto('/');
		await page.waitForFunction(() => document.body.dataset.renderP95Ms !== undefined, {
			timeout: 30_000
		});

		const metrics = await page.evaluate(() => ({
			meanMs: Number(document.body.dataset.renderMeanMs),
			p95Ms: Number(document.body.dataset.renderP95Ms),
			heapBytes: Number(document.body.dataset.wasmHeapBytes),
			readyMs: Number(document.body.dataset.wasmReadyMs)
		}));

		expect(metrics.meanMs).toBeGreaterThan(0);
		expect(metrics.p95Ms).toBeGreaterThanOrEqual(metrics.meanMs);
		expect(metrics.heapBytes).toBeGreaterThan(0);
		expect(metrics.readyMs).toBeGreaterThan(0);
	});

	test('valid and malformed bridge batches are diagnosed while rendering continues', async ({
		page
	}) => {
		await page.goto('/');
		await page.waitForFunction(() => document.body.dataset.bridgeInvalidError !== undefined, {
			timeout: 30_000
		});

		const diagnostics = await page.evaluate(() => ({
			validResult: document.body.dataset.bridgeValidResult,
			invalidError: document.body.dataset.bridgeInvalidError,
			readModelEvents: Number(document.body.dataset.bridgeReadModelEvents),
			droppedBatches: Number(document.body.dataset.bridgeDroppedBatches),
			payloadBytes: Number(document.body.dataset.bridgePayloadBytes),
			decodeMeanMs: Number(document.body.dataset.bridgeDecodeMeanMs),
			frames: Number(document.body.dataset.frameCount)
		}));
		expect(diagnostics.validResult).toBe('accepted');
		expect(diagnostics.invalidError).toBe('unsupported-version');
		expect(diagnostics.readModelEvents).toBe(1);
		expect(diagnostics.droppedBatches).toBe(0);
		expect(diagnostics.payloadBytes).toBeGreaterThan(0);
		expect(diagnostics.decodeMeanMs).toBeGreaterThan(0);
		await page.waitForFunction(
			(previous) => Number(document.body.dataset.frameCount) > previous,
			diagnostics.frames,
			{ timeout: 10_000 }
		);
	});

	test('canvas is visible when WASM is ready', async ({ page }) => {
		await page.goto('/');
		await page.waitForFunction(() => document.body.dataset.wasmStatus === 'ready', {
			timeout: 30_000
		});
		const canvas = page.locator('canvas#canvas');
		await expect(canvas).toBeVisible();
	});

	test('loading overlay hides after WASM ready', async ({ page }) => {
		await page.goto('/');
		await page.waitForFunction(() => document.body.dataset.wasmStatus === 'ready', {
			timeout: 30_000
		});
		const overlay = page.locator('[data-testid="mc-overlay"]');
		await expect(overlay).toBeHidden();
	});

	test('no market-data network requests are made', async ({ page }) => {
		const externalWsUrls: string[] = [];
		page.on('websocket', (ws) => {
			const url = ws.url();
			if (url.includes('binance') || url.includes('hyperliquid')) {
				externalWsUrls.push(url);
			}
		});

		await page.goto('/');
		await page.waitForFunction(() => document.body.dataset.wasmStatus === 'ready', {
			timeout: 30_000
		});
		// Allow a brief window for any stray connections
		await page.waitForTimeout(2_000);

		expect(externalWsUrls).toHaveLength(0);
	});
});
