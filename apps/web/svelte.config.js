import adapter from '@sveltejs/adapter-static';
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';

/** @type {import('@sveltejs/kit').Config} */
const config = {
	preprocess: vitePreprocess(),
	kit: {
		adapter: adapter({
			pages: 'build',
			assets: 'build',
			fallback: 'index.html',
			precompress: false,
			strict: true
		}),
		// GitHub Pages serves the site under /<repo>; CI sets BASE_PATH (empty locally).
		paths: {
			base: process.env.BASE_PATH ?? ''
		},
		// No server-side rendering for the canvas terminal
		prerender: {
			handleHttpError: 'warn'
		}
	}
};

export default config;
