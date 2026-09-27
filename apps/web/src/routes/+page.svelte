<script lang="ts">
	import { onMount } from 'svelte';
	import { importWorkspaceFile, loadTerminal, type TerminalStatus } from '$lib/terminal';

	let status = $state<TerminalStatus>('loading');
	let errorMessage = $state<string | null>(null);
	let notice = $state<string | null>(null);
	let fileInput = $state<HTMLInputElement>();

	onMount(() => {
		loadTerminal({
			onReady: () => {
				status = 'ready';
			},
			onError: (msg) => {
				status = 'error';
				errorMessage = msg;
			},
			onNotice: (msg) => {
				notice = msg;
			},
			onImportRequested: () => fileInput?.click()
		});
	});
</script>

<svelte:head>
	<title>Market Classifier</title>
</svelte:head>

<div id="mc-root">
	{#if notice}
		<div class="mc-banner" role="alert" data-testid="mc-notice">
			<span>{notice}</span>
			<button onclick={() => (notice = null)} aria-label="Dismiss">×</button>
		</div>
	{/if}
	<input
		bind:this={fileInput}
		type="file"
		accept="application/json,.json"
		hidden
		data-testid="mc-import-input"
		onchange={(e) => importWorkspaceFile(e.currentTarget).then((n) => (notice = n))}
	/>
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
