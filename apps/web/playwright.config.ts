import { defineConfig, devices } from '@playwright/test';

/**
 * Playwright configuration for smoke tests.
 * Run `pnpm test` from apps/web/ to execute.
 * The dev server must be running or webServer.command will start it.
 *
 * Ref: https://playwright.dev/docs/test-configuration
 */
export default defineConfig({
	testDir: './tests',
	timeout: 60_000,
	expect: {
		timeout: 30_000
	},
	fullyParallel: false,
	forbidOnly: !!process.env.CI,
	retries: process.env.CI ? 2 : 0,
	workers: 1,
	reporter: process.env.CI ? [['github'], ['list']] : 'list',

	use: {
		baseURL: 'http://localhost:5173',
		// Record traces on retry in CI to help diagnose failures
		trace: 'on-first-retry',
		screenshot: 'only-on-failure'
	},

	projects: [
		{
			name: 'chromium',
			use: {
				...devices['Desktop Chrome'],
				launchOptions: {
					// WebGL requires a real GPU or software renderer in CI
					args: ['--use-gl=swiftshader', '--enable-features=Vulkan']
				}
			}
		}
	],

	webServer: {
		// Invoke Vite directly so Playwright owns and terminates one process tree on Windows.
		command: 'node node_modules/vite/bin/vite.js dev',
		url: 'http://localhost:5173',
		reuseExistingServer: !process.env.CI,
		stdout: 'pipe',
		stderr: 'pipe',
		timeout: 30_000
	}
});
