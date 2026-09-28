// Smoke tests — verify the SvelteKit host boots and the WASM terminal initializes.
//
// These tests do NOT rely on pixel content — they use data attributes set by the
// WASM module (data-wasm-status, data-frame-count) and observable DOM state.

import { test, expect } from '@playwright/test';
import { isVenueNoise } from './venue-noise';

test.describe('host boot', () => {
	test('page loads without fatal errors', async ({ page }) => {
		const pageErrors: string[] = [];
		const failedRequests: string[] = [];
		const errorResponses: string[] = [];
		page.on('pageerror', (error) => pageErrors.push(error.message));
		page.on('requestfailed', (request) => {
			if (!isVenueNoise(request.url())) failedRequests.push(request.url());
		});
		page.on('response', (response) => {
			if (response.status() >= 400 && !isVenueNoise(response.url()))
				errorResponses.push(`${response.status()} ${response.url()}`);
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
			payloadBytes: Number(document.body.dataset.bridgePayloadBytes),
			decodeMeanMs: Number(document.body.dataset.bridgeDecodeMeanMs),
			frames: Number(document.body.dataset.frameCount)
		}));
		expect(diagnostics.validResult).toBe('accepted');
		expect(diagnostics.invalidError).toBe('unsupported-version');
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

	// MVP (packet §4, docs/protocols/*.md): market data comes only from the documented public
	// endpoints — no other hosts, no private/user-data streams.
	test('network traffic is limited to documented public venue endpoints', async ({ page }) => {
		const allowedSockets = [
			'wss://fstream.binance.com/public/stream?streams=',
			'wss://fstream.binance.com/market/stream?streams=',
			'wss://api.hyperliquid.xyz/ws'
		];
		const allowedHosts = ['fapi.binance.com', 'api.hyperliquid.xyz'];
		const unexpected: string[] = [];
		page.on('websocket', (ws) => {
			const url = ws.url();
			const local = url.startsWith('ws://localhost') || url.startsWith('ws://127.0.0.1'); // dev server
			if (local) return;
			if (!allowedSockets.some((prefix) => url.startsWith(prefix)) || url.includes('listenKey')) {
				unexpected.push(url);
			}
		});
		page.on('request', (request) => {
			const url = new URL(request.url());
			const local = url.hostname === 'localhost' || url.hostname === '127.0.0.1';
			if (!local && url.protocol.startsWith('http') && !allowedHosts.includes(url.hostname)) {
				unexpected.push(request.url());
			}
		});

		await page.goto('/');
		await page.waitForFunction(() => document.body.dataset.wasmStatus === 'ready', {
			timeout: 30_000
		});
		await page.waitForTimeout(2_000);

		expect(unexpected).toEqual([]);
	});
});
