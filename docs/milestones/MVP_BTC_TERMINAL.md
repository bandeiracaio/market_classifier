# MVP — BTC Perpetual Order-Flow Terminal

Status: Active milestone packet (approved by owner 2026-09-27)
Parent specification: `SPECIFICATION.md` (this packet supersedes conflicting sections, see §2)
Prerequisite: M1 accepted locally (`76ec3c3`)
Scope owner: project owner

## 1. Objective

Ship a static, publicly reachable web terminal that shows the BTC perpetual on
Binance USD-M Futures (`BTCUSDT`) and Hyperliquid (`BTC`) using every public
stream each venue offers, rendered through dense, dockable Dear ImGui panels.
No accounts, no backend, no trading.

The MVP proves the full architecture end to end (venue adapters → normalization →
bounded runtime → deterministic processors → read models → panels → persisted
workspace) on one underlying, so that adding instruments later is a data change,
not a rewrite.

## 2. Relationship to `SPECIFICATION.md`

This packet replaces the M2–M9 milestone plan (spec §22) for the current phase.
Where it conflicts with the specification, this packet wins; all other spec rules
(principles §2, domain model §9, derived-stream semantics §11, security §17,
determinism §19.2, coding standards §21, Claude protocol §24) still apply.

Superseded or deferred spec items:

| Spec item | MVP decision |
|---|---|
| Discover the full perp universe (§3.1, §8 catalog) | Deferred. Instruments are hardcoded to Binance `BTCUSDT` and Hyperliquid `BTC`. Metadata is still fetched and validated at startup. |
| Market explorer / screener (§12.1) | Deferred. Replaced by the BTC Overview panel. |
| Watchlists, cross-venue mapping, composite definitions (§3.1, §11.6) | Deferred, except the fixed BTC cross-venue basis shown in Overview. |
| Installable PWA / service worker (§3.1, §7.1) | Deferred. Plain static HTTPS site. |
| Subscription leases / demand-driven subscriptions (§10) | Simplified: all BTC streams for both venues are opened at startup and kept open. The lease abstraction is not required. |
| Mobile fallback (§5) | Deferred. Desktop only. |

## 3. Non-goals

- Any instrument other than BTC perps on the two venues.
- Instrument discovery, scanner, watchlist, alerts, search.
- PWA, service worker, offline mode.
- CVD history backfill (CVD is live-only on both venues).
- Persisting market data of any kind.
- Trading, credentials, private streams, telemetry, analytics, classification.
- Pthreads / workers / `SharedArrayBuffer`.

## 4. Instruments and streams

Instrument identities are fixed in one config table in the engine (venue,
native symbol, display name). Tick size, quantity step and other metadata are
fetched at startup and validated; if metadata fetch fails, the venue is shown
as `Failed` with a retry, and the other venue is unaffected.

### 4.1 Binance USD-M (`BTCUSDT`)

| Stream | Source | Normalized event |
|---|---|---|
| Metadata | REST `exchangeInfo` | `InstrumentDefinition` |
| Trades | WS `aggTrade` | `Trade` |
| Order book | REST depth snapshot + WS diff-depth `@100ms` | `BookSnapshot` / `BookDelta` |
| BBO | WS `bookTicker` | `Bbo` |
| Mark/index/funding | WS `markPrice@1s` | `AssetMetrics` |
| 24h stats | WS `ticker` | `MarketSummary` |
| Candles | REST `klines` preload + WS `kline_<interval>` | `Candle` |
| Liquidations | WS `forceOrder` | `Liquidation` |
| Open interest | REST `openInterest` polling | `OpenInterest` (cadence + age shown; never labeled streaming) |

Book sync must follow Binance's documented snapshot/diff procedure: buffer
diffs, fetch snapshot, discard stale diffs, verify `U`/`u`/`pu` continuity; on
any gap mark the book `GapDetected`, discard it, and resync.

### 4.2 Hyperliquid (`BTC`)

| Stream | Source | Normalized event |
|---|---|---|
| Metadata | Info `meta` / `metaAndAssetCtxs` | `InstrumentDefinition` |
| Trades | WS `trades` | `Trade` |
| Order book | WS `l2Book` (full snapshots) | `BookSnapshot` |
| BBO | WS `bbo` | `Bbo` |
| Mark/oracle/funding/OI/24h | WS `activeAssetCtx` | `AssetMetrics`, `OpenInterest`, `MarketSummary` |
| Candles | Info `candleSnapshot` preload + WS `candle` | `Candle` |
| Liquidations | none documented | panel shows `Unsupported` |

Never infer Hyperliquid liquidations from trades.

All endpoints, field meanings and limits must be linked to official docs with a
verification date in the adapter source or `docs/protocols/`.

## 5. Panels

Every panel consumes read models only. Every panel shows its data-quality state
(`Live`, `Stale` with age, `Reconnecting`, `GapDetected`, `Partial`,
`Unsupported`, `Failed`) and never renders stale or missing data as live.

Each panel has a venue selector: `Binance`, `Hyperliquid`, or `Both` where
marked (side-by-side or overlaid, as the panel defines).

| # | Panel | Both? | Notes |
|---|---|---|---|
| 1 | Overview | always both | last, mark, index/oracle, 24h change/volume, funding + countdown, OI, spread, cross-venue basis (Binance mid − Hyperliquid mid, abs and bps) |
| 2 | Trades tape | yes | side colored + text marker, size filter, notional in USD |
| 3 | Order-book ladder | no | grouping by price increment, cumulative size |
| 4 | Depth chart | yes | cumulative bid/ask curves |
| 5 | Candlestick chart | yes (overlay close) | intervals 1m, 5m, 15m, 1h, 4h, 1d; volume sub-plot |
| 6 | CVD | yes | live from page load, optional reset at 00:00 UTC |
| 7 | Footprint | no | bid×ask volume per price bucket per candle; bucket $1/$5/$10/$25 |
| 8 | Volume-at-price | no | session profile, same bucket options, POC marker |
| 9 | Liquidity heatmap | no | book depth over time with trade overlay |
| 10 | Liquidations | yes | Binance feed; Hyperliquid shows `Unsupported` |
| 11 | BBO / spread | yes | spread in ticks and bps over time |
| 12 | Funding / OI | yes | funding rate (raw + annualized), OI time series with sample cadence |

Formulas follow spec §11 (trade notional, aggressor delta, footprint, depth
imbalance, spread and basis). Any formula not in §11 must be documented in
`docs/calculations/` before implementation.

## 6. Retention (in memory, all bounded)

| Store | Bound |
|---|---|
| Trades tape | last 5,000 trades per venue |
| Heatmap | 60 minutes at 250 ms columns, ring buffer |
| Candles | 1,000 REST-preloaded bars per interval + live, capped at 2,000 |
| CVD / footprint / volume-at-price | from page load; ring buffers sized for 24 h at 1m resolution |
| Ingress queues | fixed capacity per venue with documented overflow policy and visible drop counters |

Reload clears all market data. Only workspace state persists.

## 7. Workspace

- Dear ImGui docking. Panels can be created, closed, duplicated, and re-docked.
- Five built-in presets (read-only; can be duplicated into user layouts):
  1. **Overview** — Overview panel, BBO/spread, Funding/OI, candles.
  2. **Tape Reader** — candles (large), trades tape, ladder, CVD.
  3. **Footprint** — footprint (large), volume-at-price, CVD, liquidations.
  4. **Liquidity** — heatmap (large), depth chart, ladder, BBO/spread.
  5. **Derivatives** — candles (Both), Funding/OI (Both), liquidations, Overview.
- Users can save the current layout under a name, rename, delete, and switch.
- First run opens the Overview preset.

## 8. Persistence

- IndexedDB, through the narrow TypeScript bridge. Persisted: user layouts
  (dock geometry + panel list + per-panel settings incl. venue selector),
  active layout id, display preferences (UTC/local time).
- Every record carries a schema version; migrations are explicit functions with
  tests for each version step.
- Corrupt/unreadable latest record → fall back to last known good record → fall
  back to built-in defaults. The user is told which fallback happened.
- "Reset workspace" action restores defaults after in-app confirmation.
- JSON export/import of layouts and preferences. Import validates schema,
  version, and size before applying; invalid files are rejected with a reason.

## 9. Runtime and failure behavior

- One WebSocket connection per venue route carrying all BTC subscriptions:
  Hyperliquid uses one socket; Binance USD-M uses two (`/public` for depth and
  bookTicker, `/market` for aggTrade, markPrice, ticker, kline, forceOrder), sharing
  one venue feed state (owner decision 2026-09-27, see
  `docs/protocols/cors-verification.md`).
- Per-venue connection state machine: connecting → live → stale → reconnecting
  (exponential backoff with jitter and cap) → live, or failed.
- Heartbeat/ping per venue protocol; staleness thresholds per stream documented.
- A failure or reconnect on one venue never resets the other venue.
- After reconnect, affected read models show a gap marker; CVD and footprint mark
  the discontinuity rather than silently continuing.
- Network callbacks only enqueue bounded batches; the frame loop drains within a
  time/event budget (spec §7.2).
- A Diagnostics panel (not in presets, always available from the menu) shows
  per-venue state, message rates, queue depth, drops, reconnect count, last event
  age, and frame time.

## 10. Stack and dependencies

Unchanged from M0/M1: C++20 engine, Dear ImGui docking + ImPlot, pinned
Emscripten, WebGL 2, SvelteKit host, CMake presets, existing test framework
(ADR-0002), fixed-point decimal (ADR-0003). WebSocket/fetch/IndexedDB live in the
TypeScript bridge; C++ receives validated bounded batches.

Any new dependency (expected: a C++ JSON parser) requires an ADR with purpose,
license, maintenance status, WASM size impact, and alternatives. Ask the owner
before adding it.

## 11. Deployment

- GitHub Pages static site, built and deployed by GitHub Actions from `main`.
- No COOP/COEP requirement (single-threaded WASM).
- Correct base path handling for the Pages subpath.

## 12. Acceptance criteria

Correctness

1. Replay tests over committed fixtures of real payloads captured from both
   venues pass natively and in WASM, covering: normalization, Binance book sync
   (normal, stale-diff discard, gap → resync), Hyperliquid book snapshots, CVD,
   footprint, volume-at-price, candle updates.
2. Malformed, oversize, out-of-bounds, and unexpected-symbol payloads are
   rejected and counted, never crash.

Resilience

3. Simulated disconnect of one venue shows `Reconnecting`, backs off, recovers,
   and leaves the other venue untouched.
4. A silent feed transitions to `Stale` with visible age.
5. Every bounded store stays within its bound under a synthetic flood test.

Workspace

6. Playwright: app loads; each preset opens without console errors; a saved
   layout survives reload; corrupt IndexedDB record falls back correctly;
   export → reset → import restores the layout.

Build and deploy

7. Native tests, WASM build, bridge tests, svelte-check, ESLint, Prettier,
   clang-format, clang-tidy all pass locally and in GitHub Actions.
8. The GitHub Pages deployment loads and shows live data from both venues.

Performance (measured, reported, not blocking)

9. `docs/performance/mvp.md` records fps and frame-time percentiles for each
   preset, WASM/bundle size, and heap growth over a 1-hour soak in Chrome.

## 13. Build order (slices)

Each slice ends with passing checks, updated docs, a handoff (CLAUDE.md format),
and small Conventional Commits.

1. **Feasibility checks.** Verify browser CORS for every REST endpoint in §4
   from a page origin (record results + date). If an endpoint is blocked, stop
   and report. Pick a C++ JSON parser via ADR (owner approval).
2. **Fixtures.** Capture and commit representative payload samples for every
   stream in §4 (a small capture tool; bounded size; documented date).
3. **Adapters + normalization.** Both venues, all streams, contract tests on
   fixtures.
4. **Connection runtime.** Bridge WebSockets/REST, per-venue state machine,
   heartbeat, backoff, staleness, diagnostics counters, Diagnostics panel.
5. **Books.** Binance snapshot/diff sync with gap resync; Hyperliquid snapshot
   book; shared book read model.
6. **Processors.** Trades store, CVD, footprint, volume-at-price, candles
   (preload + live), heatmap store, funding/OI series, basis.
7. **Panels.** All 12 panels plus Diagnostics against read models.
8. **Workspace.** Panel registry, docking, presets, user layouts, IndexedDB
   persistence with migrations and fallback, reset, export/import.
9. **CI + deploy.** GitHub Actions for all checks; Pages deploy.
10. **Performance report.** Measure and write `docs/performance/mvp.md`.

## 14. Stop-and-ask conditions for the builder

- An upstream endpoint is unavailable from the browser, or behaves differently
  from this packet.
- A new dependency is needed.
- A persisted schema or bridge protocol version must change incompatibly.
- Meeting a requirement would weaken a correctness, security, or resource bound.
