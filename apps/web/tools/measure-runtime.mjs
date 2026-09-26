import { chromium } from '@playwright/test';

const targetUrl = process.env.MC_PROFILE_URL ?? 'http://127.0.0.1:5173/';
const sampleCount = 5;
const headed = process.env.MC_PROFILE_HEADED === '1';

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

try {
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
