# MVP BTC terminal — acceptance verification

Date: 2026-09-27. Packet: `docs/milestones/MVP_BTC_TERMINAL.md`. Plan:
`docs/superpowers/plans/2026-09-27-mvp-btc-terminal.md`. Branch `mvp/btc-terminal`,
draft PR #1.

Status: **criteria 1–7 and 9 met; criterion 8 (GitHub Pages) open** — Pages is not enabled
yet (owner decision). The packet is therefore not marked accepted.

## Acceptance criteria (packet §12)

| # | Criterion | Evidence | Status |
|---|---|---|---|
| 1 | Replay tests over committed real fixtures pass natively and in WASM: normalization, Binance book sync (normal, stale-diff discard, gap → resync), Hyperliquid snapshots, CVD, footprint, VAP, candles | Fixtures `fixtures/mvp/**` captured live 2026-09-27. Tests: `test_binance_adapter.cpp`, `test_hyperliquid_adapter.cpp` (every stream), `test_binance_book_sync.cpp` (straddle, stale discard, `U>L` gap, `pu` gap + recovery, crossed, overflow, 200-diff real replay), `test_engine.cpp` (HL `l2Book` snapshots), `test_processors_{cvd,footprint,candles}.cpp`, golden `test_replay_mvp.cpp` → `fixtures/mvp/replay-expected.txt`. Native: `ctest --preset native` 135/135. WASM: `ctest --preset wasm-tests` (Catch2 under Node, `MC_WASM_TESTS=ON`) 135/135 test cases (255,903 assertions) at `b655098`; CI job "WASM build" runs it on every push | Met |
| 2 | Malformed, oversize, out-of-bounds, unexpected-symbol payloads rejected and counted, never crash | `fixtures/mvp/malformed/*`; `test_binance_adapter.cpp` "adapters reject malformed input…", "…out-of-bounds values"; `test_hyperliquid_adapter.cpp` "acks and pongs are ignored, bad frames rejected"; `test_raw_frame.cpp`; `test_json.cpp` (size/depth/truncation); `test_engine.cpp` "engine rejects invalid batches and counts adapter errors"; Diagnostics panel shows per-kind rejection counters | Met |
| 3 | Simulated disconnect of one venue shows Reconnecting, backs off, recovers, leaves the other untouched | Native: `test_engine.cpp` "disconnecting one venue leaves the other untouched", `test_feed_state.cpp` backoff tests. Browser: `apps/web/tests/resilience.test.ts` "dropping Hyperliquid reconnects it and leaves Binance live" (scripted offline venues) | Met |
| 4 | A silent feed transitions to Stale with visible age | `test_feed_state.cpp` "silence moves Live to Stale with age", `test_engine.cpp` "silent feed becomes stale on frame tick"; browser `resilience.test.ts` "a silent Binance feed becomes Stale with age of at least 5 s"; badges render "[Stale 6.0s]" | Met |
| 5 | Every bounded store stays within its bound under a synthetic flood | `test_flood.cpp`: ~1M trades + ~100k diffs over two simulated hours, every store checked each minute; burst beyond queue capacity drops oldest, counted, isolated. Mutation check (unbounded `Ring`) makes it fail | Met |
| 6 | Playwright: app loads; each preset opens without console errors; saved layout survives reload; corrupt IndexedDB record falls back; export → reset → import restores | `apps/web/tests/{smoke,panels,workspace}.test.ts` — 14/14 pass locally against the real WASM build (clean worktree at `b655098`) and in CI "Playwright smoke tests". Native mirrors: `test_workspace_controller.cpp`, `test_workspace_codec.cpp` | Met |
| 7 | Native tests, WASM build, bridge tests, svelte-check, ESLint, Prettier, clang-format, clang-tidy pass locally and in GitHub Actions | Local: native 135/135, `pnpm test:bridge` 20/20, `pnpm check` 0 errors, `pnpm lint` clean, clang-format 18 clean, clang-tidy 18 zero findings (sources + headers). CI: PR #1 checks (ci.yml incl. clang-tidy and WASM tests) | Met (CI run 36346754837) |
| 8 | GitHub Pages deployment loads and shows live data from both venues | `.github/workflows/pages.yml` ready (base path from Pages config). Local subpath build verified. **Pages not enabled; no deployment yet** | **Open** |
| 9 | `docs/performance/mvp.md` records fps/frame-time percentiles per preset, WASM/bundle size, 1-hour heap soak | `docs/performance/mvp.md`: all presets 60 fps / p99 17 ms on live data; WASM 1.40 MB (540 KB gzip); 60-min soak WASM heap flat at 24.6 MB from minute 25 | Met |

CI evidence: GitHub Actions run 36346754837 on `b655098` — all six jobs pass (native build +
tests + clang-tidy on Linux, clang-format, WASM build + WASM unit/replay tests on Node, web
typecheck/lint/bridge tests, web build, Playwright).

## Final review

A fresh-context reviewer read the whole branch (base `76ec3c3`). It rated it "with fixes":
2 Critical, 5 Important and 6 Minor findings. The fix pass (`b655098`) fixed both Critical
findings and the Important ones re-graded as material, each with a test that failed first:

- driver buffer evictions now reach the engine and mark a gap
- Binance aggTrade id jumps mark a gap
- replayed Hyperliquid trades are dropped
- unanswered snapshot requests retry after 10 s
- a full book side evicts its farthest level
- REST responses bypass the frame buffer
- rejected metadata fails the venue
- a crossed Hyperliquid snapshot is not shown Live

The remaining minor items are listed under "Known risks / follow-ups" below.

## Handoff

```text
Milestone / slice: MVP BTC perpetual order-flow terminal (packet slices 1–10, plan Tasks 1–14)
Outcome: Terminal implemented end to end (venue adapters → normalization → bounded runtime →
  processors → read models → 13 panels → persisted workspace); criteria 1–7 and 9 met;
  criterion 8 awaits enabling GitHub Pages and merging to main.
Changed files: engine/** (venues, json, books, runtime, processors, ui, app), bridge/src/**
  (raw-frame, venues, persistence), apps/web/** (host wiring, tests, tools), fixtures/mvp/**,
  docs/{adr/0005,protocols,calculations,runtime,performance,reviews}, .github/workflows/*,
  CMake presets/options, README.
Tests added or updated: 26 native test files (135 cases), 4 bridge test files (20 cases),
  Playwright smoke/panels/workspace/resilience (14 cases), WASM (Node) run of the native
  suite.
Commands run and results: ctest --preset native 135/135; ctest --preset wasm-tests 135/135;
  pnpm test:bridge 20/20; pnpm check 0; pnpm lint clean; pnpm test 14/14 (clean worktree);
  clang-format 18 clean; clang-tidy 18 0 findings; fresh clone native 128/128 (pre-fix);
  CI on PR #1.
Performance impact: 60 fps all presets (live data), WASM 1.40 MB / 540 KB gzip, WASM heap
  flat 24.6 MB over 60 min (docs/performance/mvp.md).
Protocol/docs consulted: Binance USD-M WebSocket market streams (routes, connect rules,
  local order book procedure), Binance REST depth/klines/openInterest/exchangeInfo;
  Hyperliquid WebSocket subscriptions/heartbeats and info endpoint — all read 2026-09-27
  (docs/protocols/*.md).
Decisions or deviations: owner-approved — Binance /public + /market sockets (packet §9
  amended), ADR-0005 yyjson, exchangeInfo trimming, domain default initializers. Executor
  rulings are listed in the PR description / ledger (raw-frame queue instead of M1 ingress,
  compact heatmap storage, $1/1m footprint base, layout uid scoping, WASM test preset,
  clang-tidy k_ scoping, M0 no-network smoke test replaced, Playwright fake venues instead of
  a Playwright bump).
Known risks / follow-ups: Pages deployment (criterion 8); Binance geo-restrictions for some
  regions; JS heap average drift during soak (docs/performance/mvp.md); deferred minors —
  duplicate panel ids on import, Connecting→Live promotion by a pre-open buffered frame,
  gap marks on wall vs source clock, late trades credited to newest footprint bar,
  data_gap() latch; local env: a permission-locked apps/web/static/wasm/market_classifier.js
  in the main checkout (built and tested from a clean worktree instead).
Suggested commit message: docs(mvp): record acceptance status and handoff
```
