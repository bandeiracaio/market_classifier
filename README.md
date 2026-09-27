# Market Classifier

An open-source, local-first web terminal for inspecting live perpetual-futures market data.  The terminal engine is C++20 with Dear ImGui, compiled to WebAssembly via Emscripten and rendered through WebGL 2.  A minimal SvelteKit shell hosts the terminal.

**Status: MVP — BTC perpetual order-flow terminal** (`docs/milestones/MVP_BTC_TERMINAL.md`).
Live public data for Binance USD-M `BTCUSDT` and Hyperliquid `BTC`: overview, trades tape,
order book, depth, candles, CVD, footprint, volume profile, liquidity heatmap, liquidations,
BBO/spread and funding/OI panels in five dockable presets, with layouts saved in the browser.
No accounts, no backend, no trading. Market data is never persisted.

---

## Quick start

### Prerequisites

Install the tools below.  Exact versions are in `docs/toolchain.md`.

| Tool | Version | Install |
|------|---------|---------|
| Node.js | 22.9.0 | https://nodejs.org/ |
| pnpm | 9.11.0 | `corepack enable && corepack prepare pnpm@9.11.0 --activate` |
| CMake | ≥ 3.28 (3.29.6 pinned) | https://cmake.org/download/ |
| Ninja | 1.12.1 | https://github.com/ninja-build/ninja/releases |
| Emscripten SDK | 3.1.67 | See below |
| clang-format / clang-tidy | 18.x | OS package manager, https://releases.llvm.org/, or `pip install clang-format==18.1.8 clang-tidy==18.1.8` |

#### Install Emscripten (Windows)

```powershell
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install 3.1.67
./emsdk activate 3.1.67
./emsdk_env.bat      # run this in each new terminal session
```

#### Install Emscripten (Linux/macOS)

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install 3.1.67
./emsdk activate 3.1.67
source ./emsdk_env.sh   # run this in each new terminal session
```

---

## Build and test

### 1. Install JavaScript dependencies

```bash
cd apps/web
pnpm install
```

### 2. Native C++ build and unit tests

```bash
# From the repository root
cmake --preset native-release
cmake --build --preset native-release
ctest --preset native
```

Expected output: `All tests passed` with the version test.

### 3. WASM build

Ensure Emscripten is activated in the current terminal (`emsdk_env`).

```bash
# From the repository root
emcmake cmake --preset wasm-release
cmake --build --preset wasm-release
```

Artifacts are written to `apps/web/static/wasm/`.

### 4. Run the web host

```bash
cd apps/web
pnpm dev
# Open http://localhost:5173 in Chrome or Firefox
```

The browser should display a full-window Dear ImGui canvas with a dockable Lissajous plot.

### 5. Web typecheck and lint

```bash
cd apps/web
pnpm check   # svelte-check + TypeScript
pnpm lint    # ESLint + Prettier
```

### 6. Playwright smoke tests

The dev server must be running (step 4) or Playwright will start it automatically.

```bash
cd apps/web
pnpm test
```

### 7. Measure bundle size

```bash
# From the repository root (after WASM build and web build)
bash tools/measure-bundle.sh
```

### 8. Measure browser runtime

With the development server running:

```bash
cd apps/web
pnpm measure:runtime
```

This reports five-sample cold and warm readiness, render-time, and WASM-heap summaries.
It complements, but does not replace, an interactive browser performance trace.

### All-in-one CI equivalent (Linux)

```bash
cmake --preset native-release && cmake --build --preset native-release && ctest --preset native
source /path/to/emsdk/emsdk_env.sh
emcmake cmake --preset wasm-release && cmake --build --preset wasm-release
cd apps/web && pnpm install --frozen-lockfile && pnpm check && pnpm lint && pnpm build && pnpm test
```

---

## Repository structure

```
apps/web/            SvelteKit browser host (installable PWA work is deferred to M9)
engine/include/      Public C++ headers
engine/src/          Terminal boot and render loop
engine/tests/        Native unit tests (Catch2)
bridge/src/          Typed TypeScript/WASM bridge stubs
docs/adr/            Architecture decision records
docs/milestones/     Milestone packets
docs/performance/    Baseline measurements
tools/               Build and measurement helpers
.github/workflows/   CI pipeline
```

---

## Architecture

The architecture is defined in `SPECIFICATION.md`.  Key decisions:

- **Native engine**: C++20, Dear ImGui docking branch, ImPlot, SDL2 (M0 spike; SDL3 migration deferred — see `docs/toolchain.md`)
- **WASM compilation**: Emscripten 3.1.67, WebGL 2 / OpenGL ES 3, single-threaded (no SharedArrayBuffer requirement)
- **Dependencies**: CMake FetchContent pinned to immutable tags/SHAs (ADR-0001)
- **Test framework**: Catch2 v3 (ADR-0002)
- **Web host**: SvelteKit 2 + Svelte 5, adapter-static, no SSR

---

## License

Apache License 2.0.  See `LICENSE`.

Third-party dependencies and their licenses:

| Library | License |
|---------|---------|
| Dear ImGui | MIT |
| ImPlot | MIT |
| SDL2 | Zlib |
| Catch2 | BSL-1.0 |
| SvelteKit / Svelte | MIT |
| Vite | MIT |
| Playwright | Apache-2.0 |

---

## Contributing

See `CONTRIBUTING.md`.


---

## Deployment (GitHub Pages)

`.github/workflows/pages.yml` builds the WASM terminal and the static site on every push to
`main` and deploys it to GitHub Pages under `/<repo>` (the build sets `BASE_PATH` from the
Pages configuration). One-time setup: repository Settings → Pages → Source: **GitHub Actions**.

Local check of the subpath build:

```bash
cd apps/web
BASE_PATH=/market_classifier pnpm build && pnpm preview   # open http://localhost:4173/market_classifier/
```

Browser requirements: WebGL 2, WebSocket, IndexedDB. The terminal connects directly from the
browser to `fapi.binance.com` / `fstream.binance.com` and `api.hyperliquid.xyz`; Binance
restricts some regions, in which case its panels show `Failed`/`Reconnecting` while
Hyperliquid keeps working.
