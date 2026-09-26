import { sveltekit } from '@sveltejs/kit/vite';
import { defineConfig } from 'vite';

export default defineConfig({
	plugins: [sveltekit()],
	// Serve WASM files with correct MIME type
	assetsInclude: ['**/*.wasm'],
	server: {
		headers: {
			// Note: COOP/COEP not set here — M0 uses single-threaded WASM (no SharedArrayBuffer).
			// See SPECIFICATION.md §6.2 and ADR threading decision.
		}
	},
	build: {
		// Target modern browsers with WebAssembly support
		target: 'es2022'
	}
});
