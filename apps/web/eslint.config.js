import js from '@eslint/js';
import tsPlugin from '@typescript-eslint/eslint-plugin';
import tsParser from '@typescript-eslint/parser';
import sveltePlugin from 'eslint-plugin-svelte';

/** @type {import('eslint').Linter.FlatConfig[]} */
export default [
	{
		languageOptions: {
			globals: {
				HTMLCanvasElement: 'readonly',
				console: 'readonly',
				document: 'readonly',
				process: 'readonly',
				setTimeout: 'readonly',
				clearTimeout: 'readonly',
				requestAnimationFrame: 'readonly',
				WebSocket: 'readonly',
				fetch: 'readonly',
				window: 'readonly'
			}
		}
	},
	// Base JavaScript rules
	js.configs.recommended,

	// TypeScript files
	{
		files: ['**/*.ts'],
		languageOptions: {
			parser: tsParser,
			parserOptions: {
				extraFileExtensions: ['.svelte']
			}
		},
		plugins: {
			'@typescript-eslint': tsPlugin
		},
		rules: {
			'no-unused-vars': 'off',
			'@typescript-eslint/no-unused-vars': ['error', { argsIgnorePattern: '^_' }],
			'@typescript-eslint/no-explicit-any': 'error'
		}
	},

	// Svelte files — spread the flat config preset from eslint-plugin-svelte
	...sveltePlugin.configs['flat/recommended'],
	{
		files: ['**/*.svelte'],
		languageOptions: {
			parserOptions: {
				parser: tsParser
			}
		}
	},

	// Ignored paths
	{
		ignores: ['.svelte-kit/', 'build/', 'node_modules/', 'static/']
	}
];
