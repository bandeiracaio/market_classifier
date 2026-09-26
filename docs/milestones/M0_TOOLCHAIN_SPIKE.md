# M0 — Toolchain and Architecture Feasibility Spike

Status: Accepted locally on 2026-09-25; first remote CI run pending  
Parent specification: `SPECIFICATION.md`, section 22  
Scope owner: project owner  
Architecture review: Codex  
Implementation: Claude Code

## Objective

Prove that the selected C++/Dear ImGui/Emscripten/SvelteKit architecture builds reproducibly and runs in a browser before any exchange integration is written.

This is a feasibility milestone, not a production terminal milestone.

## Required outcomes

1. A pinned, documented toolchain for Windows development and Linux CI.
2. A minimal SvelteKit browser host that boots a WASM module into a full-window canvas. Installable PWA behavior remains an M9 deliverable.
3. A C++20 Dear ImGui dockspace using the pinned `docking` branch commit.
4. One dockable ImPlot demonstration panel fed by deterministic generated data.
5. Native unit-test execution independent of a browser.
6. A Playwright smoke test proving the terminal loads and renders a known marker/state.
7. Baseline measurements for payload size, startup, and frame time.
8. Initial CI running formatting/lint, native tests, WASM build, web build, and smoke test.
9. ADRs for dependency acquisition and C++ test framework.

## Non-goals

- No Binance or Hyperliquid connections.
- No generic feed architecture beyond minimal seams required to avoid throwaway demo code.
- No domain market events.
- No IndexedDB persistence beyond verifying the host can access required browser APIs if useful.
- No production theme or full panel system.
- No pthreads, SharedArrayBuffer, COOP/COEP requirement, service worker tuning, or deployment provider selection.
- No premature performance optimization.

## Decisions to validate

### Platform and renderer

Test the official Dear ImGui SDL3 + OpenGL3 backend with Emscripten/WebGL 2 first. Use SDL2 only if SDL3 has a reproducible blocker. A fallback requires:

- exact failure;
- minimal reproduction or upstream reference;
- impact assessment;
- ADR decision.

Do not switch to WebGPU in M0 unless the specified WebGL path is proven infeasible and the owner approves a scope change.

### Dependency acquisition

Compare only these options:

- Git submodules pinned to commits;
- CMake `FetchContent` pinned to immutable commits.

Select one consistent policy in ADR-0001. Consider reproducibility, offline/CI cache behavior, auditability, update workflow, and contributor ergonomics.

### Native test framework

Compare Catch2 and GoogleTest briefly. Choose one in ADR-0002. Criteria: compile cost, discovery, CMake integration, readability, maintenance, and future property/fuzz compatibility.

## Required pinned tools

Pin exact versions rather than using `latest`:

- Node.js
- pnpm
- Emscripten SDK
- CMake
- Ninja
- Dear ImGui commit from maintained docking branch
- ImPlot commit/release compatible with the selected Dear ImGui commit
- SvelteKit and TypeScript dependencies through the lockfile
- Playwright and browser version through its normal managed installation
- clang-format and clang-tidy expectations

Record the selection and verification date in `docs/toolchain.md`.

## Proposed scaffold

```text
apps/web/                    # minimal SvelteKit host/PWA
engine/include/              # stable public engine headers
engine/src/                  # terminal boot/render loop
engine/tests/                # native unit tests
bridge/src/                  # minimal TypeScript/WASM host bridge
docs/adr/                    # accepted decisions
docs/milestones/             # milestone packets
tools/                       # repeatable build/measurement helpers
```

Adjust only when needed and explain deviations in the handoff.

## Test-first slices

### Slice 1 — Native build

- Add a trivial domain-free C++ library and failing native test.
- Establish CMake presets and make the test pass.
- Enable warnings-as-errors for project code, not third-party code.

### Slice 2 — Web host

- Scaffold the minimal SvelteKit host.
- Add a deterministic loading/failure state.
- Add a smoke test for host boot before WASM integration.

### Slice 3 — ImGui/WASM

- Integrate pinned Dear ImGui and selected SDL backend.
- Compile with Emscripten for WebGL 2.
- Render a full-window dockspace and a stable automation marker.
- Keep the animation loop browser-friendly; no blocking native loop.

### Slice 4 — ImPlot

- Add a dockable panel rendering seeded deterministic data.
- Avoid market-looking random data that could be mistaken for a feed.
- Expose an observable ready state for Playwright without coupling tests to pixels alone.

### Slice 5 — Baseline and CI

- Measure compressed/uncompressed relevant output sizes.
- Record cold/warm startup methodology and frame-time methodology.
- Add CI with caching that does not weaken version pinning.
- Document local commands matching CI.

## Acceptance checklist

- [ ] Clean clone builds using documented Windows commands.
- [ ] CI Linux build uses the same pinned dependency/tool versions.
- [ ] Native C++ unit test passes.
- [ ] SvelteKit typecheck/lint passes.
- [ ] WASM build succeeds from a CMake preset or documented wrapper.
- [ ] Browser opens a full-window Dear ImGui docking canvas.
- [ ] At least two ImGui windows can dock/tab/resize.
- [ ] ImPlot panel renders deterministic seeded data.
- [ ] Playwright verifies host boot, WASM ready state, and a render heartbeat/marker.
- [ ] Browser console has no unexplained errors or warnings.
- [ ] No market-data network request exists.
- [ ] No analytics or telemetry dependency exists.
- [ ] Toolchain versions are documented and pinned.
- [ ] ADR-0001 and ADR-0002 are accepted or explicitly awaiting review.
- [ ] Bundle and runtime baseline is recorded with methodology.
- [ ] README contains exact setup/build/test commands.
- [ ] Apache-2.0 headers/notices are handled appropriately.
- [ ] Claude handoff follows `CLAUDE.md`.

## Baseline report template

Create `docs/performance/m0-baseline.md` containing:

```text
Date and commit:
Machine CPU/RAM/GPU:
OS:
Browser/version:
Build type and flags:
Uncompressed JS/WASM/assets:
Compressed JS/WASM/assets:
Cold interactive time and method:
Warm interactive time and method:
Idle frame time:
Active demo frame time:
WASM heap after boot:
Known measurement limitations:
```

Do not manufacture measurements in CI or copy values from another machine.

## Required handoff

Use the handoff template in `CLAUDE.md` and additionally include:

- exact toolchain versions;
- direct links to official integration documentation used;
- chosen SDL backend and why;
- dependency and test-framework ADR summaries;
- generated artifact sizes;
- screenshots or test artifacts only if produced by the automated test workflow;
- anything that should change before M1.

## Stop conditions

Stop and request architecture review if:

- the official SDL/OpenGL backend cannot produce a stable WebGL 2 build;
- satisfying the PWA host requires duplicating ownership of the render loop;
- a dependency has an incompatible license;
- cross-origin isolation becomes required in the single-threaded build;
- a required tool cannot be pinned reproducibly;
- the compressed payload exceeds the 12 MB hard warning before application code exists;
- the implementation requires exchange or classification scope.
