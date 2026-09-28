/* global performance */
import { chromium } from '@playwright/test';

const targetUrl = process.env.MC_PROFILE_URL ?? 'http://127.0.0.1:5173/';
const sampleCount = 5;
const headed = process.env.MC_PROFILE_HEADED === '1';
// MVP modes (plan Task 13): `--presets` measures each preset for 30 s against live data;
// `--soak` samples JS + WASM heap every minute for MC_SOAK_MINUTES (default 60).
const mode = process.argv.includes('--soak')
	? 'soak'
	: process.argv.includes('--presets')
		? 'presets'
		: 'startup';
const presetSeconds = Number(process.env.MC_PRESET_SECONDS ?? 30);
const soakMinutes = Number(process.env.MC_SOAK_MINUTES ?? 60);
const presets = ['Overview', 'Tape Reader', 'Footprint', 'Liquidity', 'Derivatives'];

const browser = await chromium.launch({
	headless: !headed,
	args: headed ? [] : ['--use-gl=swiftshader', '--enable-features=Vulkan']
});

async function measurePage(context) {
	const page = await context.newPage();
	await page.goto(targetUrl);
	await page.waitForFunction(() => document.body.dataset.renderP95Ms !== undefined, undefined, {
		timeout: 30_000
	});

	const sample = await page.evaluate(() => {
		const canvas = document.querySelector('canvas#canvas');
		const gl = canvas instanceof HTMLCanvasElement ? canvas.getContext('webgl2') : null;
		const rendererInfo = gl?.getExtension('WEBGL_debug_renderer_info');

		return {
			readyMs: Number(document.body.dataset.wasmReadyMs),
			renderMeanMs: Number(document.body.dataset.renderMeanMs),
			renderP95Ms: Number(document.body.dataset.renderP95Ms),
			wasmHeapBytes: Number(document.body.dataset.wasmHeapBytes),
			userAgent: window.navigator.userAgent,
			webglRenderer:
				gl && rendererInfo
					? String(gl.getParameter(rendererInfo.UNMASKED_RENDERER_WEBGL))
					: 'unavailable'
		};
	});
	await page.close();
	return sample;
}

function summarize(values) {
	const sorted = [...values].sort((left, right) => left - right);
	return {
		min: sorted[0],
		median: sorted[Math.floor(sorted.length / 2)],
		max: sorted.at(-1)
	};
}

/** Frame-interval stats from requestAnimationFrame over `seconds` (bounded sample array). */
async function frameStats(page, seconds) {
	return page.evaluate(async (ms) => {
		const deltas = [];
		let last = performance.now();
		const end = last + ms;
		await new Promise((resolve) => {
			const step = (t) => {
				if (deltas.length < 100_000) deltas.push(t - last);
				last = t;
				if (t < end) requestAnimationFrame(step);
				else resolve();
			};
			requestAnimationFrame(step);
		});
		deltas.sort((a, b) => a - b);
		const at = (q) => deltas[Math.min(deltas.length - 1, Math.floor(q * deltas.length))];
		const mean = deltas.reduce((a, b) => a + b, 0) / deltas.length;
		return {
			frames: deltas.length,
			meanMs: mean,
			p95Ms: at(0.95),
			p99Ms: at(0.99),
			fps: 1000 / mean
		};
	}, seconds * 1000);
}

async function heap(page) {
	return page.evaluate(() => ({
		jsHeapBytes: performance.memory?.usedJSHeapSize ?? null,
		wasmHeapBytes: Number(document.body.dataset.wasmHeapBytes ?? NaN),
		binanceEvents: Number(document.body.dataset.binanceEvents ?? 0),
		hyperliquidEvents: Number(document.body.dataset.hyperliquidEvents ?? 0)
	}));
}

async function openLive(context) {
	const page = await context.newPage();
	await page.goto(targetUrl);
	await page.waitForFunction(() => document.body.dataset.wasmStatus === 'ready', undefined, {
		timeout: 30_000
	});
	// Let both venues connect and preload before measuring.
	await page.waitForFunction(
		() =>
			document.body.dataset.binancePhase === '1' && document.body.dataset.hyperliquidPhase === '1',
		undefined,
		{ timeout: 60_000 }
	);
	return page;
}

async function measurePresets() {
	const context = await browser.newContext();
	const page = await openLive(context);
	const results = [];
	for (let index = 0; index < presets.length; index += 1) {
		await page.evaluate((i) => window.mcTerminal._mc_debug_activate_layout(i), index);
		await page.waitForTimeout(2000); // settle layout
		results.push({
			preset: presets[index],
			...(await frameStats(page, presetSeconds)),
			...(await heap(page))
		});
	}
	await context.close();
	return results;
}

async function soak() {
	const context = await browser.newContext();
	const page = await openLive(context);
	await page.evaluate(() => window.mcTerminal._mc_debug_activate_layout(3)); // Liquidity: heaviest stores
	const samples = [];
	for (let minute = 0; minute <= soakMinutes; minute += 1) {
		samples.push({ minute, ...(await heap(page)) });
		console.error(JSON.stringify(samples.at(-1)));
		if (minute < soakMinutes) await page.waitForTimeout(60_000);
	}
	await context.close();
	return samples;
}

try {
	if (mode !== 'startup') {
		const result = mode === 'soak' ? await soak() : await measurePresets();
		console.log(
			JSON.stringify(
				{
					method: `Playwright ${headed ? 'headed' : 'headless'} Chromium, live venue data`,
					mode,
					browserVersion: browser.version(),
					result
				},
				null,
				2
			)
		);
		await browser.close();
		process.exit(0);
	}
	const cold = [];
	for (let index = 0; index < sampleCount; index += 1) {
		const context = await browser.newContext();
		cold.push(await measurePage(context));
		await context.close();
	}

	const warmContext = await browser.newContext();
	await measurePage(warmContext); // Prime the browser cache; exclude this run.
	const warm = [];
	for (let index = 0; index < sampleCount; index += 1) {
		warm.push(await measurePage(warmContext));
	}
	await warmContext.close();

	console.log(
		JSON.stringify(
			{
				method: `Playwright ${headed ? 'headed' : 'headless'} Chromium; five fresh contexts and five cache-warm pages`,
				browserVersion: browser.version(),
				userAgent: cold[0].userAgent,
				webglRenderer: cold[0].webglRenderer,
				coldReadyMs: summarize(cold.map((sample) => sample.readyMs)),
				warmReadyMs: summarize(warm.map((sample) => sample.readyMs)),
				renderMeanMs: summarize(warm.map((sample) => sample.renderMeanMs)),
				renderP95Ms: summarize(warm.map((sample) => sample.renderP95Ms)),
				wasmHeapBytes: summarize(warm.map((sample) => sample.wasmHeapBytes))
			},
			null,
			2
		)
	);
} finally {
	await browser.close();
}
