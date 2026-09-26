# ADR-0002: C++ Unit Test Framework

Status: Accepted  
Date: 2026-09-24

## Context

The project requires a native C++ test framework for pure unit tests (decimals, tick alignment, domain calculations, book invariants, sequence handling) and future property/fuzz tests.  The framework must integrate cleanly with CMake, compile fast, and not impose a second parallel framework as the test suite grows.

Two candidates were evaluated: Catch2 v3 and GoogleTest.

## Options considered

### Option A — Catch2 v3

- Single `Catch2WithMain` target via `FetchContent`; no separate `main()` or registry boilerplate.
- CMake: `catch_discover_tests(target)` integrates with CTest automatically.
- Test syntax: `TEST_CASE` / `SECTION` structure maps cleanly to domain concepts (e.g., `TEST_CASE("order book", "[book]") { SECTION("gap forces rebuild") { … } }`).
- Benchmarking: built-in `BENCHMARK` macro (useful for performance budget checks).
- Fuzz integration: third-party adapters exist (libFuzzer, AFL++) but are less mature than GoogleTest's.
- Compile time: module-based v3 compile is faster than v2 single-header; comparable to GoogleTest for typical unit test files.
- License: BSL-1.0 (permissive, Apache-2.0 compatible).
- Maintenance: active, well-maintained, 2k+ GitHub stars, regular releases.

### Option B — GoogleTest

- Two targets: `gtest` and `gtest_main`; requires linking the right combination.
- CMake: `gtest_discover_tests(target)` also integrates with CTest.
- Test syntax: `TEST(Suite, Case)` — flat structure requires naming discipline to achieve the grouping Catch2 provides with `SECTION`.
- Fuzz integration: first-class `FUZZ_TEST` macro through GoogleFuzzTest extension.
- Compile time: comparable; GoogleMock adds overhead if included.
- License: BSD-3-Clause (permissive, Apache-2.0 compatible).
- Maintenance: Google-backed, very active, dominant in large C++ projects.

## Decision

**Option A — Catch2 v3.**

Rationale:

1. `TEST_CASE` / `SECTION` hierarchies map naturally to domain fixtures (book state, sequence gaps, normalization variants) without requiring Suite naming conventions.
2. `REQUIRE` / `CHECK` semantics (stop-vs-continue per section) reduce false-negative cascades in multi-step tests.
3. The single `Catch2WithMain` target is simpler to wire in CMake than the gtest/gmock/main split.
4. Built-in benchmarking via `BENCHMARK` supports performance budget checks without a separate framework.
5. Fuzz testing can be added via `libFuzzer` adapter in a later milestone; the absence of native `FUZZ_TEST` is not a M0 blocker.
6. Both licenses are compatible; Catch2's BSL-1.0 is arguably more contributor-friendly.

Only one test framework must be used.  Do not mix Catch2 and GoogleTest.

## Consequences

**Positive:**
- Idiomatic section-based test organization.
- Single CMake target; no boilerplate.
- Built-in benchmark harness for future performance milestones.

**Negative:**
- Fuzz test integration requires a third-party adapter (acceptable for M0–M7; revisit in M8 if fuzzing strategy changes).
- Less familiar to contributors from large Google-style C++ projects; Catch2 docs link should be in CONTRIBUTING.md.

**Operational:**
- `list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)` and `include(Catch)` enable `catch_discover_tests`.
- Test binary is registered with CTest; run via `ctest --preset native` or `cmake --build --preset native-release --target test`.
- Catch2 must not be built for the WASM target; guard with `if(NOT EMSCRIPTEN)`.

## Evidence

- Catch2 documentation: https://github.com/catchorg/Catch2/blob/devel/docs/
- Catch2 CMake integration: https://github.com/catchorg/Catch2/blob/devel/docs/cmake-integration.md
- Catch2 v3 performance notes: https://github.com/catchorg/Catch2/blob/devel/docs/migrate-v2-to-v3.md
