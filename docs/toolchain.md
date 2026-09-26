# Toolchain Reference — M0

Last updated: 2026-09-24  
Milestone: M0 (toolchain spike)

This file is the authoritative record of pinned tool and dependency versions.
Update it whenever a `GIT_TAG` or tool version changes and include the verification date.

## Host tools (install manually; not managed by CMake or pnpm)

| Tool | Pinned version | Install method | Notes |
|------|----------------|----------------|-------|
| Node.js | 22.9.0 | https://nodejs.org/ or nvm / volta | LTS; verify with `node --version` |
| pnpm | 9.11.0 | `corepack enable && corepack prepare pnpm@9.11.0 --activate` | Workspace manager |
| CMake | 3.29.6 | https://cmake.org/download/ | Minimum 3.28 enforced in CMakeLists.txt |
| Ninja | 1.12.1 | https://github.com/ninja-build/ninja/releases | Required by all CMake presets |
| Emscripten SDK | 3.1.67 | `./emsdk install 3.1.67 && ./emsdk activate 3.1.67` | Must be sourced before WASM build |
| clang-format | 18.x | LLVM releases or package manager | Project .clang-format targets 18 syntax |
| clang-tidy | 18.x | LLVM releases or package manager | Matches .clang-tidy configuration |

### Installing Emscripten on Windows

```powershell
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install 3.1.67
./emsdk activate 3.1.67
./emsdk_env.bat   # sets EMSDK, PATH, etc. for the current session
```

### Installing Emscripten on Linux (CI)

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install 3.1.67
./emsdk activate 3.1.67
source ./emsdk_env.sh
```

## C++ dependencies (managed by CMake FetchContent — ADR-0001)

| Library | Pinned ref | Purpose | License | Verified |
|---------|-----------|---------|---------|---------|
| Dear ImGui | `3bae66c735670619baf51391eba7f3d90a25d125` (docking) | Immediate-mode UI | MIT | 2026-09-24 |
| ImPlot | `v0.17` | Plot primitives | MIT | 2026-09-24 |
| SDL2 | `release-2.30.8` | Windowing + input (native builds) | Zlib | 2026-09-24 |
| Catch2 | `v3.7.1` | C++ test framework (native only) | BSL-1.0 | 2026-09-24 |

The Dear ImGui revision was resolved directly from the official docking branch with
`git ls-remote` and is immutable. Update it only through the documented dependency
review procedure.

## SDL backend decision

M0 uses **SDL2 + Emscripten's built-in SDL2 port** (`-sUSE_SDL=2`).

Rationale for SDL2 over SDL3 in M0:
- Emscripten 3.1.x includes a production-quality SDL2 port as a first-class system library.  Activating it requires only `-sUSE_SDL=2` with no additional build step.
- SDL3 does not have a built-in Emscripten system-library port.  Building SDL3 from source with `emcmake cmake` is possible but adds toolchain complexity that is inappropriate for a feasibility spike where build reproducibility is a primary acceptance criterion.
- Dear ImGui's official Emscripten example (`examples/example_emscripten_opengl3`) targets SDL2, providing a direct reference implementation.

Migration to SDL3 is deferred to M1 only if profiling or a specific capability requires it, with an updated ADR entry.

## JavaScript/TypeScript dependencies (managed by pnpm lockfile)

Pin these via `pnpm-lock.yaml` committed to the repository.

| Package | Version | Purpose |
|---------|---------|---------|
| @sveltejs/kit | 2.5.28 | Web host and routing |
| svelte | 5.1.3 | Component framework |
| @sveltejs/adapter-static | 3.0.5 | Static site output |
| vite | 5.4.8 | Build tool and dev server |
| typescript | 5.5.4 | Type checking |
| svelte-check | 4.0.4 | Svelte type checking |
| @playwright/test | 1.47.2 | Browser smoke tests |
| prettier | 3.3.3 | Formatting |
| eslint | 9.11.1 | Linting |

Run `pnpm install --frozen-lockfile` in CI to use the lockfile exactly.

## Reference desktop (performance baseline)

The M0 baseline in `docs/performance/m0-baseline.md` was recorded on the machine
specified in that file.  Measurements from other machines are not substitutes.

## Update procedure

1. Change the `GIT_TAG` in `CMakeLists.txt`.
2. Update the version and verification date in this table.
3. Delete `build/` to force a clean FetchContent re-download.
4. Build and test to verify the new version is compatible.
5. Commit `CMakeLists.txt` and `docs/toolchain.md` together.
