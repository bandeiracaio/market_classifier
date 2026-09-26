// Terminal bridge type stubs — M0 milestone.
//
// These types define the versioned boundary between the SvelteKit host and the
// C++ WASM terminal.  They are stubs only for M0; the full typed bridge is a
// M1 deliverable.  They are placed here to establish the module boundary and
// avoid coupling the web host to Emscripten internals.

/** Bridge protocol version — incremented when the JS/WASM contract changes. */
export const BRIDGE_VERSION = 1 as const;

/** Terminal lifecycle status visible to the host. */
export type TerminalLifecycle = 'initializing' | 'ready' | 'error' | 'shutdown';

/**
 * Minimal interface the Emscripten glue exposes after MODULARIZE=1 initialization.
 * Extended in M1 with typed message channels.
 */
export interface TerminalModule {
	readonly bridgeVersion: typeof BRIDGE_VERSION;
	lifecycle: TerminalLifecycle;
}
