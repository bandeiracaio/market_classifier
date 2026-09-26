<script lang="ts">
	import { onMount } from 'svelte';
	import { loadTerminal, type TerminalStatus } from '$lib/terminal';

	let status = $state<TerminalStatus>('loading');
	let errorMessage = $state<string | null>(null);

	onMount(() => {
		loadTerminal({
			onReady: () => {
				status = 'ready';
			},
			onError: (msg) => {
				status = 'error';
				errorMessage = msg;
			}
		});
	});
</script>

<svelte:head>
	<title>Market Classifier</title>
</svelte:head>

<div id="mc-root">
	<!-- The Emscripten SDL2 port targets the canvas with id="canvas" by default -->
	<canvas id="canvas" aria-label="Market Classifier terminal"></canvas>

	<div
		class="mc-overlay"
		class:hidden={status === 'ready'}
		role="status"
		aria-live="polite"
		data-testid="mc-overlay"
	>
		{#if status === 'loading'}
			<div class="mc-spinner" aria-hidden="true"></div>
			<span>Loading terminal…</span>
		{:else if status === 'error'}
			<p class="mc-error" data-testid="mc-error">
				{errorMessage ?? 'Failed to load terminal.'}
			</p>
			<button onclick={() => window.location.reload()}>Reload</button>
		{/if}
	</div>
</div>
