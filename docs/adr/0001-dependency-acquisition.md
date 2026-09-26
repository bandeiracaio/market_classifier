# ADR-0001: Dependency Acquisition Policy

Status: Accepted  
Date: 2026-09-24

## Context

The project requires reproducible builds across Windows development machines and Linux CI without relying on a developer's ambient toolchain for C++ dependencies.  Two policies were evaluated: Git submodules and CMake `FetchContent` with pinned immutable references.

Emscripten and Node/pnpm are managed separately through their own version managers (emsdk and corepack/volta) and are not in scope for this ADR.

## Options considered

### Option A — Git submodules (pinned commits)

- Submodules are checked out at a fixed commit via `.gitmodules`.
- Advantages: explicit audit trail in `.gitmodules`; offline builds possible after cloning with `--recurse-submodules`; exact contents verified by tree hash.
- Disadvantages: every contributor and CI agent must run `git submodule update --init --recursive`; `git clone` without `--recurse-submodules` silently omits sources; nested submodules (e.g., SDL2's own `cmake/` submodules) require careful nested init; updating a dependency means a `git add` on a submodule pointer, which is opaque in diffs.

### Option B — CMake FetchContent with pinned immutable commits/tags

- Each `FetchContent_Declare` specifies an exact `GIT_TAG` (either an immutable release tag or a full commit SHA).
- Advantages: a clean clone + `cmake --preset` downloads and builds everything automatically; no extra git step; dependencies are explicit and visible in the root `CMakeLists.txt`; updating a dependency is a clear one-line diff; CMake's `FETCHCONTENT_BASE_DIR` and `GIT_SHALLOW TRUE` reduce CI fetch cost.
- Disadvantages: network access is required on first configure (mitigated by CI cache on `~/.cmake/packages` and `_deps/`); SHA-only pins cannot be guessed from a changelog — must be recorded in `docs/toolchain.md`; HTTPS fetch is authenticated only by TLS, not a content hash (mitigated by immutable tags/SHAs on trusted repositories).

## Decision

**Option B — CMake FetchContent with pinned immutable tags or commit SHAs.**

Rationale:

1. The CI-from-clean-clone acceptance criterion is simpler to satisfy without a mandatory `git submodule` step.
2. A single `CMakeLists.txt` is the canonical dependency manifest; it is reviewed in the same PR as the code that uses it.
3. CMake 3.28 `EXCLUDE_FROM_ALL` prevents transitive target pollution.
4. `GIT_SHALLOW TRUE` keeps clone cost low for CI without sacrificing pinning.
5. Dependencies pinned by full SHA are as reproducible as submodules while being friendlier to contributors unfamiliar with git submodule workflows.

All `GIT_TAG` values must be:
- a release tag (e.g., `v3.7.1`) that the upstream project treats as immutable, OR
- a full 40-character commit SHA if no immutable tag exists.

Mutable branch names (e.g., `main`, `docking`) are permitted only during early feasibility spikes and must be replaced with a pinned SHA before milestone acceptance.

## Consequences

**Positive:**
- Clean clone to working build requires only documented commands.
- Dependency intent is reviewable in normal code review.
- CI caching is straightforward with `_deps/` cache key.

**Negative:**
- First configure requires internet access (or a pre-populated cache).
- Developers must update `docs/toolchain.md` when a `GIT_TAG` changes.
- Transitive license obligations must be reviewed manually; there is no `git subtree log` equivalent.

**Operational:**
- Add `build/` and `_deps/` to `.gitignore`.
- In CI, cache `~/.cmake` and `build/_deps` keyed on `CMakeLists.txt` hash.
- Record each dependency's version, pinned tag/SHA, purpose, license, and last verification date in `docs/toolchain.md`.

## Evidence

- CMake FetchContent documentation: https://cmake.org/cmake/help/latest/module/FetchContent.html
- Catch2 CMake integration: https://github.com/catchorg/Catch2/blob/devel/docs/cmake-integration.md
- Dear ImGui BACKENDS.md notes on CMake integration
