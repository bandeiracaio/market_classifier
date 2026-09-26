# Market Classifier — Live Perpetual Market Terminal Specification

Status: Architecture baseline for implementation  
Version: 0.1.0  
Date: 2026-09-24  
License: Apache License 2.0  
Primary implementation agent: Claude Code  
Architecture and review: Codex + project owner

## 1. Purpose

Market Classifier is an open-source, local-first web terminal for inspecting live perpetual-futures market data. The first release connects the browser directly to Binance USD-M Futures and Hyperliquid, normalizes their public feeds, and renders every supported stream through a suitable real-time visualization.

The application should feel like a native professional terminal: dense, dockable, responsive, and fast. Its core engine and operational interface will be C++ with Dear ImGui, compiled to WebAssembly through Emscripten and rendered with WebGL. A minimal SvelteKit shell will host the terminal, manage PWA lifecycle and compatibility, and display fatal loading/recovery states.

This release does not classify markets. It must, however, keep normalized events and deterministic derived calculations separate from rendering so a future feature-extraction/classification layer can consume them without rewriting ingestion.

## 2. Product principles

1. **Correct before clever.** Missing, stale, incomplete, or unsupported data must be visible; it must never be silently invented.
2. **Local first.** No account, application backend, tracking, cookies, or server-side user state.
3. **Demand driven.** Discover the complete perpetual universe, but subscribe to expensive feeds only when a visible panel needs them.
4. **One normalized core, faithful sources.** Normalize common semantics while preserving the venue, native symbol, source timestamp, receive timestamp, and source-specific limitations.
5. **Auditable calculations.** Every derived value has a documented formula, input set, window, and reset rule.
6. **Bounded resources.** Every queue, cache, time series, reconnect loop, and retained book has an explicit limit.
7. **Deterministic core.** Feed recordings must replay into the same normalized and derived outputs in native tests and WASM.
8. **Progressive disclosure.** Useful defaults for learners; dense, configurable controls for experts.
9. **Visual expression without ambiguity.** Expressive motion and color support interpretation but never become the only encoding.
10. **Measure before optimizing.** Maintain performance budgets and profiles; do not guess where the bottleneck is.

## 3. Goals

### 3.1 V1 goals

- Installable HTTPS PWA that runs without an application account.
- Direct public browser connections to Binance USD-M Futures and Hyperliquid mainnet.
- Discover every currently active perpetual market offered by both venues.
- Create independent, dockable panels for unrelated instruments in one workspace.
- Share identical subscriptions across panels instead of opening duplicate connections.
- Allow explicit single-venue, side-by-side, same-underlying composite, and manually mixed composites.
- Provide at least one live visualization for every supported raw or derived stream.
- Save layouts, watchlists, preferences, panel settings, and composite definitions locally.
- Use ordinary REST candle history for initial chart context.
- Expose connection health, data age, gaps, drops, memory usage, and processing latency.
- Provide a reproducible native and WASM build with automated tests.
- Establish a disciplined Claude Code implementation and review protocol.

### 3.2 Success criteria

V1 is successful when a new user can:

1. Open the app in a supported desktop browser.
2. Search all active Binance and Hyperliquid perpetual markets.
3. Open multiple live panels for multiple instruments.
4. Observe trades, order book, BBO/spread, prices, candles, funding, OI, and venue-supported liquidations.
5. Create live CVD, footprint, depth, imbalance, volume-at-price, and composite views.
6. Rearrange the workspace and recover it after reload.
7. See immediately when a stream is stale, reconnecting, incomplete, unsupported, or gap-affected.
8. Run the project locally and reproduce CI checks from documented commands.

## 4. Explicit non-goals for V1

- Trading or order entry
- Exchange credentials or private/user streams
- User accounts or cloud synchronization
- Application backend or hosted raw-data proxy
- Persistent raw trades/order books
- Historical order-flow archive or replay
- Alerts, email, push, Telegram, or audio notifications
- Custom scripting and user-authored indicators
- Backtesting
- Machine-learning or rules-based market classification
- Social sharing or public layouts
- Spot, dated futures, options, prediction markets, or on-chain wallet analysis
- Pixel-for-pixel or behavior-for-behavior replication of Cryexc

These belong in the roadmap in section 23 and must not leak into initial milestones.

## 5. User and platform assumptions

- Primary users: curious traders learning microstructure and experienced order-flow traders.
- Primary platform: desktop Chrome/Edge and Firefox on current stable releases.
- Secondary platform: current Safari desktop after compatibility verification.
- Mobile/tablet: simplified read-only fallback; docking and dense panels are not V1 mobile requirements.
- Input: mouse and keyboard first, touch fallback only.
- Time: store UTC epoch milliseconds; display local time by default with a UTC toggle.
- Numbers: monospaced, locale-aware display; calculations never parse formatted display strings.
- Privacy: no analytics or telemetry in production V1. Diagnostics remain on device unless the user manually exports them in a future feature.

## 6. Technology decisions

### 6.1 Required stack

| Concern | Decision |
|---|---|
| Terminal and domain engine | C++20 minimum |
| Immediate-mode UI | Dear ImGui `docking` branch, pinned commit |
| Plot primitives | ImPlot, pinned commit, supplemented by custom ImDrawList rendering where necessary |
| Browser compilation | Pinned Emscripten SDK |
| Platform/window backend | SDL3 preferred after a milestone-0 browser spike; SDL2 is fallback only if a documented blocker is found |
| Graphics | OpenGL ES 3/WebGL 2 through Dear ImGui OpenGL3 backend |
| Web host/PWA | SvelteKit + TypeScript, pinned Node and pnpm versions |
| Build system | CMake presets + Ninja; `emcmake` for WASM |
| Unit tests | Catch2 or GoogleTest chosen in ADR-0002 after a minimal comparison; do not use two frameworks |
| Browser tests | Playwright |
| Formatting/lint | clang-format, clang-tidy, ESLint, Prettier |
| Local preferences | localStorage for small versioned preferences |
| Structured local data | IndexedDB through a narrow TypeScript bridge |
| CI | GitHub Actions |
| License | Apache-2.0 |

### 6.2 Browser threading decision

Start single-threaded in WASM. Do not enable pthreads in the first vertical slice. Browser pthreads require `SharedArrayBuffer` and cross-origin isolation headers (COOP/COEP), which constrain hosting and third-party assets. Establish event-rate and frame-time benchmarks first.

If the main-thread budget is exceeded after batching and allocation work:

1. Write an ADR with profiles and failing budgets.
2. Evaluate an Emscripten pthread build and a Web Worker/message-batch design.
3. Keep both a scalar/single-thread fallback and threaded build until browser support is proven.

### 6.3 Dependency policy

- Pin every native dependency to an exact commit or release.
- Pin the Emscripten SDK, Node, pnpm, and package lockfile.
- Prefer vendored Git submodules or CMake `FetchContent` with immutable commit hashes; choose one policy in ADR-0001.
- No dependency may be added without a short entry explaining purpose, license, maintenance status, and alternatives considered.
- Generate a third-party notices file in release builds.
- Run dependency and license checks in CI.

## 7. System architecture

```text
┌──────────────────────────── SvelteKit host ────────────────────────────┐
│ PWA lifecycle | canvas host | compatibility | fatal recovery | bridge │
└───────────────────────────────┬────────────────────────────────────────┘
                                │ typed, versioned JS/WASM boundary
┌───────────────────────────────▼────────────────────────────────────────┐
│                         C++ / WASM terminal                            │
│                                                                        │
│  Connection registry ──> venue adapters ──> validation/normalization  │
│          │                                      │                      │
│          │                                      ▼                      │
│          │                              normalized event bus           │
│          │                                      │                      │
│          ▼                                      ▼                      │
│  subscription leases                  deterministic processors        │
│                                                │                       │
│                                                ▼                       │
│                                      bounded read models/stores        │
│                                                │                       │
│                                                ▼                       │
│                                Dear ImGui panels + ImPlot/custom GL    │
└────────────────────────────────────────────────────────────────────────┘
```

### 7.1 Module boundaries

1. **Host shell:** owns HTML, service worker/PWA, canvas boot, build selection, browser compatibility, and fatal recovery only.
2. **Browser bridge:** owns WebSocket/Fetch/IndexedDB browser APIs and converts callbacks into bounded batches for C++.
3. **Venue adapters:** validate venue messages and emit normalized domain events. No rendering code.
4. **Subscription manager:** reference-counts logical subscriptions and multiplexes physical connections.
5. **Domain processors:** maintain books and derive candles, delta, CVD, footprint, volume-at-price, and health metrics.
6. **Read models:** bounded snapshot-friendly models consumed by UI panels.
7. **Panels:** render read models and issue subscription intents. Panels never parse raw JSON.
8. **Persistence:** versioned serialization for layouts and settings only.

### 7.2 Main-loop rule

Network callbacks must not mutate render-owned structures directly. The bridge submits batches to an ingress queue. At the start of each frame, the engine drains up to a time/event budget, updates processors, publishes read models, and renders. Remaining events stay bounded and visible in diagnostics. Overflow follows a documented stream-specific policy rather than unbounded allocation.

## 8. Venue scope and capability matrix

The matrix must be verified by adapter contract tests against captured official payloads. A dash means unavailable through the documented public interface, not zero.

| Capability | Binance USD-M Futures | Hyperliquid perps | Normalized behavior |
|---|---|---|---|
| Instrument catalog | REST exchange metadata | Info API metadata/asset contexts | `InstrumentDefinition` |
| All-market summary | all-market ticker streams/REST | `allMids` / all asset contexts | screener/search summaries |
| Trades | aggregate trade stream | `trades` subscription | `Trade` |
| L2 order book | REST snapshot + diff-depth stream | `l2Book` snapshot feed | `BookSnapshot`/`BookDelta` semantics |
| Best bid/ask | book ticker | `bbo` | `Bbo` |
| Mark/index/funding | mark-price stream | `activeAssetCtx` / asset contexts | `AssetMetrics` |
| Open interest | REST polling unless an official public stream is documented during adapter work | `activeAssetCtx` / asset contexts | `OpenInterest` with cadence metadata |
| Candles | kline stream + REST preload | `candle` + candle snapshot REST/info request | `Candle` |
| Liquidations | force-order stream | No documented global public liquidation subscription | `Liquidation`; panel displays unsupported for Hyperliquid |

Important rules:

- Never infer Hyperliquid global liquidations from normal trades unless a future official field proves liquidation identity.
- Never label REST-polled OI as tick-by-tick streaming. Show its actual cadence and age.
- Preserve venue-native symbols. Cross-venue matches are explicit metadata, not string guessing.
- If an upstream capability changes, update this matrix, fixtures, and an ADR before implementation changes.

## 9. Normalized domain model

All external numeric strings are validated at ingress. Prices and quantities use fixed-point values expressed as integer mantissa plus scale, or a proven decimal type. Binary floating point is permitted only for rendering and explicitly tolerant statistical calculations.

### 9.1 Common identifiers

```cpp
enum class Venue : uint8_t { BinanceUsdM, Hyperliquid };

struct InstrumentId {
  Venue venue;
  std::string native_symbol;
};

struct Decimal {
  int64_t mantissa;
  int8_t scale;
};

struct EventMeta {
  InstrumentId instrument;
  int64_t source_time_ms;
  int64_t receive_time_ms;
  uint64_t local_sequence;
  DataQuality quality;
};
```

The concrete types may change through ADR before milestone 2, but their semantics may not be weakened.

### 9.2 Required normalized events

- `InstrumentDefinition`: tick size, quantity step, contract kind, quote/base, active status, venue metadata.
- `MarketSummary`: last/mid, 24h change, 24h volume, optional OI/funding, age.
- `Trade`: unique source identity, aggressor side, price, quantity, USD notional, timestamps.
- `BookSnapshot`: bids/asks with price, quantity, optional order count, sequence/cursor.
- `BookDelta`: changed price levels, sequence range, source timestamp.
- `Bbo`: best bid/ask price and quantity.
- `AssetMetrics`: mark, index/oracle, funding rate, next funding time, basis where available.
- `OpenInterest`: native quantity, USD notional if calculable, sample cadence.
- `Candle`: interval, open/close timestamps, OHLC, base volume, quote volume where available, trade count where available, closed flag.
- `Liquidation`: side, price, quantity, notional, source identity and method where available.
- `FeedStatus`: lifecycle state, last event age, reconnect attempt, gap status, error category.

### 9.3 Data quality state

Each stream/read model carries one of:

- `Live`
- `Delayed`
- `Stale`
- `Reconnecting`
- `GapDetected`
- `Partial`
- `Unsupported`
- `Failed`

UI panels must render these states consistently and must not use color alone.

## 10. Subscription and connection model

### 10.1 Logical subscription key

```text
(venue, native_symbol, stream_kind, stream_parameters)
```

Panels acquire a lease for a key. The manager reference-counts leases. The physical subscription starts on the first lease and stops after the last lease plus a short configurable grace period to avoid churn during layout changes.

### 10.2 Connections

- Use the smallest safe number of venue WebSockets while respecting official stream/subscription limits.
- Batch subscription changes where the venue permits.
- Rate-limit subscribe/unsubscribe messages.
- Send required heartbeats and detect half-open connections.
- Use exponential backoff with full jitter and a maximum delay.
- Reset backoff after a stable interval, not immediately after socket open.
- Make per-venue and per-stream status visible.
- Do not reconnect unrelated healthy venue legs when one stream fails.

### 10.3 Order-book synchronization

Binance and Hyperliquid require different algorithms:

- Binance: buffer diffs, fetch REST snapshot, align update IDs exactly per official procedure, apply continuous updates, and discard/rebuild on gaps.
- Hyperliquid: treat `l2Book` as a snapshot feed at its documented cadence; replace relevant book state atomically and preserve source age.

Book code must be adapter-independent after normalized events enter the core. Fixtures must cover duplicates, out-of-order data, snapshot races, crossed books, deletion by zero size, and sequence gaps.

### 10.4 Resource budgets

Initial configurable defaults, subject to milestone-6 benchmarks:

- Maximum active high-frequency logical subscriptions: 32.
- Warning threshold: 24.
- Trade retention per panel/source: 50,000 or 60 minutes, whichever comes first.
- Liquidation retention: 10,000 or 24 hours in-memory, whichever comes first.
- Book depth retained: venue-provided full supported depth, with a hard level cap per side documented by adapter.
- Rendered book levels: viewport-driven and grouped; never iterate invisible full depth unnecessarily.
- Ingress queue: bounded per connection and stream priority.
- UI time series: ring buffers.
- Default frame target: 60 FPS when visible; reduced cadence when backgrounded.

When limits are reached, the UI must explain what was rejected or dropped and how to recover.

## 11. Derived streams and exact semantics

Every derived stream must have a pure processor with replay tests.

### 11.1 Trade notional

For linear USD/USDT perps:

```text
notional = price × base_quantity
```

Any venue-specific contract multiplier must come from `InstrumentDefinition`. Never assume every quantity is base units.

### 11.2 Aggressor delta

```text
signed_notional = buy_aggressor ? +notional : -notional
window_delta = sum(signed_notional in window)
CVD(t) = CVD(previous) + signed_notional
```

CVD reset scope is explicit: session start, UTC day, manual reset, or visible range. Default is session start for V1.

### 11.3 Footprint

Bucket trades by:

- panel-defined bar rule: time initially; tick/volume/range/delta bars are later V1 milestones if budgets permit;
- normalized price group aligned to the instrument tick size;
- aggressor side.

Each cell exposes bid-hit volume, ask-lift volume, total, delta, and trade count. The active bar is marked incomplete.

### 11.4 Depth imbalance

For a configured range or top `N` levels:

```text
imbalance = (bid_notional - ask_notional) / (bid_notional + ask_notional)
```

Return unavailable when the denominator is zero or book quality is not acceptable. The UI displays chosen range/level count.

### 11.5 Spread and basis

```text
absolute_spread = best_ask - best_bid
spread_bps = absolute_spread / midpoint × 10,000
basis_bps = (mark - index_or_oracle) / index_or_oracle × 10,000
```

### 11.6 Composite rules

- A composite is an ordered list of explicit `InstrumentId` values.
- Default matching suggestions may use verified base-asset metadata, never symbol string mutation alone.
- Arbitrary mixed-underlying composites are allowed and visibly labeled `MIXED`.
- Additive measures such as USD notional volume may aggregate across mixed instruments.
- Price, spread, book depth, and quantity cannot aggregate across mixed underlyings without an explicitly selected normalization.
- Supported normalizations: raw side-by-side, percent-from-reference, z-score over a declared window, or USD notional where mathematically valid.
- The panel header always lists or expands to show included sources and hidden members.

## 12. Panel catalog and visual specifications

All panels have a common header: panel type, instrument/composite selector, venue/source chips, stream quality, age, settings, duplicate, and close controls.

### 12.1 Market explorer / screener

Inputs: instrument catalog + lightweight summaries.  
Visual: sortable/filterable table with favorites, venue, symbol, price, 24h change/volume, funding, OI, spread, and freshness.  
Behavior: searching never subscribes every market to full-depth/trades. Opening a panel creates a lease.

### 12.2 Trades tape

Input: `Trade`.  
Visual: scrolling tape with age/time, venue, side, price, size, notional, and proportional magnitude bar.  
Controls: minimum notional, side, venue mask, clustering window, pause view without pausing ingestion.

### 12.3 Flow pulse

Input: `Trade`.  
Visual: animated but bounded horizontal pulses plus rolling 10s/30s/1m/5m buy/sell totals and delta.  
Accessibility: tabular numbers remain available; reduced motion disables pulse travel.

### 12.4 Order-book ladder

Input: book + BBO + recent trades.  
Visual: centered DOM with bid/ask resting size, price, recent traded volume, cumulative depth, and optional order count.  
Controls: auto-center, grouping, raw/notional, visible depth, heat scaling.

### 12.5 Depth chart

Input: current book.  
Visual: cumulative bid/ask area curves around midpoint, with hover values and imbalance summary.

### 12.6 Liquidity heatmap

Input: sampled book states.  
Visual: time × price raster, intensity by resting notional, trade markers overlaid.  
Storage: bounded in-memory texture/ring buffer; display sampling cadence explicitly.  
V1 does not claim spoof or iceberg detection.

### 12.7 BBO and spread monitor

Input: `Bbo`.  
Visual: best bid/ask cards, live spread gauge, spread-bps sparkline, and stale indicator.

### 12.8 Price/mark/index chart

Input: BBO midpoint, last trade, mark, and index/oracle.  
Visual: synchronized lines with basis subplot and provenance legend.

### 12.9 Candlestick chart

Input: REST-preloaded and live `Candle`.  
Visual: OHLC candles, volume, mark/last overlays, crosshair, zoom/pan.  
Intervals: intersection of venue-supported intervals; adapter maps exact native values.

### 12.10 Funding monitor

Input: funding metrics.  
Visual: current funding, annualized equivalent, countdown to next funding where available, rolling line, and cross-venue comparison.  
Formula and venue cadence must be visible in help text.

### 12.11 Open-interest monitor

Input: `OpenInterest`.  
Visual: OI value, change from session start, sampled line, price overlay, and actual sample cadence/age.  
Do not interpolate missing points as observed data.

### 12.12 Liquidations

Input: `Liquidation`.  
Visual: tape plus price/time bubbles sized by notional and rolling long/short totals.  
Hyperliquid behavior: render `Unsupported by public global feed` instead of an empty live state.

### 12.13 CVD

Input: trades.  
Visual: CVD line or bars, venue-separated or composite, with session reset marker and optional price overlay.

### 12.14 Footprint chart

Input: trades, optionally REST OHLC as visually distinct context only.  
Visual: bid × ask cells per price row, delta/total modes, POC, bar volume/delta.  
Honesty rule: preloaded OHLC candles contain no reconstructed per-price order flow and must never be rendered as true footprint cells.

### 12.15 Volume-at-price profile

Input: session trades.  
Visual: horizontal histogram, POC, configurable value area, buy/sell split, and coverage start time.

### 12.16 Depth imbalance

Input: book.  
Visual: signed gauge plus time series, with the selected top-N or percentage range shown in the header.

### 12.17 Composite comparison

Input: any compatible read models.  
Visual: small multiples or normalized overlay; never merge incompatible price axes silently.

### 12.18 Diagnostics

Input: connection and engine metrics.  
Visual: sockets, logical leases, message/event rates, queue utilization, drops, parse failures, reconnects, last ages, frame time, WASM heap, and build/version data.

## 13. Workspace and interaction model

- Enable Dear ImGui docking in a full-window dockspace.
- Users may create, close, duplicate, tab, resize, and rearrange panels.
- Provide named local layouts and autosave the active layout.
- Ship three defaults: `Welcome`, `Order Flow`, and `Cross-Venue`.
- Panel identity is a stable UUID; settings are per instance.
- Keyboard command palette opens panels and switches instruments.
- Workspace recovery retains the last known-good serialized layout if the newest version cannot load.
- Provide reset-layout and safe-mode startup paths.
- Dear ImGui multi-viewport/native external windows are out of scope for browser V1.

## 14. Visual system

The design combines terminal discipline with expressive data graphics.

- Background: neutral charcoal layers with clear elevation.
- Typography: monospaced numerals; readable UI sans/mono chosen with an open-source license and bundled locally.
- Direction palette: color-blind-safe cyan/blue versus orange/magenta, configurable. Never encode side using hue alone.
- Expressive elements: restrained glow, heat gradients, flow trails, and transitions limited to data regions.
- Numeric tables: minimal decoration and stable column alignment.
- Motion: capped and deterministic; honor reduced-motion preference.
- Contrast: target WCAG AA for shell/status text where the canvas technology permits; document Dear ImGui accessibility limitations honestly.
- Scaling: support browser/device pixel ratio and configurable UI scale without blurry text.

## 15. Persistence

Persist only:

- schema version;
- layouts and panel definitions;
- panel settings;
- composites;
- favorites/watchlists;
- theme, time-zone display, number format, and UI scale;
- last known-good configuration.

Do not persist in V1:

- raw trades;
- book events or snapshots;
- liquidation history;
- derived session series beyond the open session;
- credentials or secrets.

Use localStorage only for tiny boot preferences and migration pointers. Use IndexedDB for versioned structured workspace state. All persistence operations cross one bridge API and return explicit success/failure results.

## 16. Error handling and recovery

- Categorize errors as configuration, network, rate-limit, protocol, validation, gap, resource-limit, storage, rendering, or internal invariant.
- Never display raw upstream payloads in normal UI error messages.
- Sanitize logs and cap their retention.
- A failed panel must not crash the terminal.
- A failed venue must not reset healthy venues.
- Protocol parse failures increment metrics and retain a bounded redacted diagnostic sample.
- On WebGL context loss, pause ingestion within bounds, attempt renderer recovery, and offer reload if recovery fails.
- On WASM out-of-memory risk, stop acquiring new subscriptions and show an actionable resource warning.
- Service-worker updates must not silently reload an active session; notify and let the user reload.

## 17. Security and privacy

- Public read-only endpoints only.
- No API keys, wallets, order signing, or authenticated exchange endpoints.
- Strict Content Security Policy compatible with the WASM build and direct venue endpoints.
- Restrict `connect-src` to documented exchange/API hosts.
- Bundle fonts/assets locally where licensing permits.
- No third-party analytics, tracking pixels, tag managers, or remote error reporting.
- Escape/sanitize all upstream strings before display or logging.
- Enforce message size limits before parsing.
- Validate JSON shape and numeric bounds.
- Produce release hashes and a software bill of materials.
- Document that exchange endpoints see the user's IP because connections are direct.
- Document regional exchange restrictions and surface them as connection errors.

## 18. Performance budgets

Budgets are testable requirements, not aspirations. Milestone 0 records the reference hardware/browser.

| Metric | Initial budget |
|---|---|
| Warm app interactive | ≤ 2.5 s on reference desktop/broadband |
| Compressed initial terminal payload | target ≤ 8 MB; hard warning at 12 MB |
| Idle frame CPU | ≤ 3 ms average when no active motion |
| Active render frame | p95 ≤ 16.7 ms with reference workspace |
| Ingress-to-read-model latency | p95 ≤ 50 ms under reference replay |
| UI-visible data age | source-dependent, always displayed if over threshold |
| Dropped normalized trades | 0 under reference replay load |
| Heap growth | plateau under a 60-minute soak; no unbounded growth |
| Layout save | ≤ 100 ms and off critical render path |

Reference workspace:

- BTC on Binance and Hyperliquid;
- trades tape, Binance DOM, composite CVD, footprint, liquidity heatmap, funding/OI, and diagnostics;
- recorded burst test at a documented multiple of observed peak input.

## 19. Testing strategy

### 19.1 Test pyramid

1. **Pure unit tests:** decimals, tick alignment, bucketing, delta/CVD, candles, profiles, composites, status transitions.
2. **Adapter fixture tests:** raw official payload -> normalized event or explicit rejection.
3. **Property/fuzz tests:** JSON parsing, fixed-point conversion, book invariants, sequence handling, arbitrary ordering.
4. **Replay tests:** timestamped recordings through adapters/processors with golden summaries.
5. **Native integration tests:** subscription leases, reconnect state machines, persistence migrations.
6. **WASM/browser tests:** boot, canvas render, bridge messages, WebSocket mocks, IndexedDB, layout reload.
7. **Visual regression:** stable seeded/read-only panel frames at fixed viewport and DPI.
8. **Performance/soak:** event bursts, one-hour bounded-memory run, repeated reconnects.

### 19.2 Determinism rules

- Inject clocks; do not call wall time inside calculations.
- Seed random jitter in tests.
- Record source and receive timestamps.
- Golden files contain normalized semantic outputs, not brittle full UI dumps.
- Update goldens only with an explained protocol/calculation change.

### 19.3 Live tests

Live exchange tests are opt-in and never required for deterministic unit CI. A scheduled CI job may run read-only smoke checks with conservative rate limits. Failures due to geography or upstream availability are reported separately from product regressions.

## 20. Repository structure

```text
market_classifier/
├─ apps/
│  └─ web/                         # SvelteKit host and PWA
├─ engine/
│  ├─ include/market_classifier/
│  │  ├─ domain/
│  │  ├─ feeds/
│  │  ├─ processing/
│  │  ├─ runtime/
│  │  ├─ storage/
│  │  └─ ui/
│  ├─ src/
│  └─ tests/
├─ bridge/
│  ├─ src/                         # typed TS browser bridge
│  └─ tests/
├─ fixtures/
│  ├─ binance/
│  ├─ hyperliquid/
│  └─ replay/
├─ tools/                          # fixture validation and benchmarks
├─ docs/
│  ├─ adr/
│  ├─ protocols/
│  ├─ calculations/
│  └─ runbooks/
├─ third_party/
├─ .github/workflows/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ pnpm-workspace.yaml
├─ CLAUDE.md
├─ CONTRIBUTING.md
├─ LICENSE
├─ README.md
├─ SPECIFICATION.md
└─ info.md
```

No file should become a generic dumping ground. Venue-specific code remains under its adapter. UI panels consume interfaces/read models, not adapters.

## 21. Coding and documentation standards

- Prefer small types with explicit invariants and ownership.
- Avoid shared mutable globals and hidden singleton state.
- Use RAII and smart pointers; raw owning pointers are forbidden.
- Comments explain why, invariants, protocols, and non-obvious math—not syntax.
- Public interfaces and calculations require concise documentation.
- Every venue behavior links to the official documentation and records the verification date.
- Treat compiler warnings as errors in project code.
- Run sanitizers in native CI where supported.
- Keep functions focused and names domain-specific.
- Use structured error types or result values at recoverable boundaries; exceptions must not cross JS/WASM boundaries.
- Log structured categories and stable event codes.
- Record architectural changes in ADRs.
- Keep `SPECIFICATION.md` authoritative for scope; implementation details belong in ADRs and protocol docs.

## 22. Milestone plan

Each milestone begins with failing tests/acceptance checks where practical, ends with all relevant checks green, updates documentation, and produces one or more small commits. Do not begin the next milestone until review accepts the previous one.

### M0 — Feasibility and toolchain spike

Deliverables:

- pinned C++/Emscripten/Node/pnpm/CMake/Ninja toolchain;
- minimal SvelteKit host loading a Dear ImGui docking canvas;
- SDL3 + OpenGL3/WebGL2 viability result;
- one ImPlot demo panel;
- native and WASM test commands;
- bundle-size and frame-time baseline;
- ADR-0001 dependencies and ADR-0002 testing framework.

Acceptance:

- clean clone builds via documented commands on Windows and CI Linux;
- browser displays dockable demo panel;
- native unit test and Playwright smoke test pass;
- no network market data yet.

### M1 — Domain core and bridge contract

Deliverables:

- normalized identifiers, decimal policy, events, quality states;
- versioned TypeScript/C++ bridge envelope;
- bounded ingress batches and diagnostics counters;
- injected clock and replay harness.

Acceptance:

- serialization/validation fixtures round-trip where appropriate;
- malformed/oversize messages reject safely;
- reference replay can feed a dummy panel deterministically.

### M2 — Instrument discovery and workspace foundation

Deliverables:

- Binance and Hyperliquid catalog adapters;
- searchable market explorer;
- panel registry, docking, create/close/duplicate;
- IndexedDB layout persistence and migrations;
- default layouts and safe reset.

Acceptance:

- all active perps returned by upstream metadata appear with native identity;
- duplicate names across venues remain unambiguous;
- workspace restores after reload and recovers from a corrupt latest record.

### M3 — Connection/subscription runtime

Deliverables:

- venue connection state machines;
- reference-counted logical leases;
- batching, heartbeat, reconnect/backoff, resource budgets;
- diagnostics panel.

Acceptance:

- two panels requesting one key produce one logical upstream subscription;
- final lease release unsubscribes after grace period;
- simulated disconnect/reconnect never resets the other venue;
- queue pressure is bounded and visible.

### M4 — Trades, BBO, ticker and candles

Deliverables:

- both venue trade adapters;
- BBO/market summary/mark-index adapters;
- candle REST preload and live updates;
- trades tape, flow pulse, BBO/spread, price/basis, candlestick panels.

Acceptance:

- captured official payloads normalize correctly;
- aggressor side and notional are fixture-tested;
- REST history and live candle transition avoids duplicates;
- all panels show freshness and source identity.

### M5 — Books and depth visualization

Deliverables:

- Binance snapshot/diff synchronization;
- Hyperliquid snapshot-book adapter;
- DOM, depth chart, heatmap, imbalance panels;
- grouping and raw/notional controls.

Acceptance:

- book invariant suite passes;
- a sequence gap forces a visible Binance resync;
- Hyperliquid snapshots never masquerade as deltas;
- heatmap and history buffers remain bounded in soak tests.

### M6 — Funding, OI and liquidations

Deliverables:

- funding and OI adapters with true cadence metadata;
- Binance liquidation adapter;
- funding, OI, and liquidation panels;
- explicit unsupported Hyperliquid global-liquidation state.

Acceptance:

- no missing value renders as zero;
- annualization formulas are documented/tested per venue cadence;
- REST-polled values show sample time and age;
- Hyperliquid liquidation panel never claims a live global stream.

### M7 — Derived order flow

Deliverables:

- delta/CVD;
- time-based footprint;
- session volume-at-price and POC/value area;
- venue masks and composites;
- CVD, footprint, and volume profile panels.

Acceptance:

- processors pass golden replay fixtures;
- composite membership/provenance remains visible;
- mixed-underlying composites reject misleading operations;
- OHLC preload is visually distinct from true footprint history.

### M8 — Polish, accessibility and performance

Deliverables:

- final visual system, UI scaling, reduced motion;
- command palette, onboarding hints, contextual definitions;
- mobile read-only fallback/unsupported-panel messaging;
- performance profiling and budget enforcement;
- one-hour soak suite.

Acceptance:

- reference workspace meets agreed budgets or deviations have approved ADRs;
- no unbounded heap growth in soak;
- keyboard-accessible common commands;
- color is not the sole directional/status signal.

### M9 — Release engineering

Deliverables:

- service worker and controlled update flow;
- CSP and production headers;
- SBOM, third-party notices, Apache-2.0 license;
- CI release artifacts and hashes;
- contributor guide and operations/troubleshooting docs.

Acceptance:

- fresh clone and hosted production build pass the release checklist;
- app installs as a PWA;
- no analytics/network destinations outside the allowlist;
- release version is visible in diagnostics.

## 23. Deferred roadmap

These are recorded, not scheduled:

1. Local history recorder/service and order-flow replay.
2. Alerts: price, trade size, liquidation, volume/delta, divergence, absorption, walls, and velocity.
3. Custom indicators or a sandboxed scripting language.
4. Backtesting against recorded/replayed streams.
5. Market-regime feature extraction, labeling, and classification.
6. Optional accounts and encrypted cross-device layout synchronization.
7. Trade execution, isolated into a separately threat-modeled component with explicit user confirmation and secret handling.
8. Additional perpetual venues through the same adapter contract.
9. Export/import of layouts and sanitized diagnostic bundles.
10. Social/shared layouts only after privacy and abuse review.

The future classifier consumes versioned normalized/derived snapshots; it must not depend on ImGui widgets or venue JSON.

## 24. Claude Code implementation protocol

Claude Code must read, in order:

1. `SPECIFICATION.md`
2. `CLAUDE.md`
3. relevant ADRs and protocol documents
4. existing tests for the area being changed

### 24.1 Before coding

Claude must report:

- milestone and acceptance criterion being addressed;
- files/modules expected to change;
- assumptions and specification conflicts;
- tests it will add first;
- official documentation it relies on for protocol behavior.

If a requirement is ambiguous or conflicts with observed upstream behavior, stop and ask. Do not silently choose a new product behavior.

### 24.2 During coding

- Work only on the current milestone or explicitly assigned slice.
- Preserve unrelated user changes.
- Add or update tests with implementation.
- Do not weaken assertions to make failures disappear.
- Do not add dependencies or change protocols without approval and an ADR.
- Keep adapters, domain logic, and rendering separated.
- Avoid broad refactors not required by the acceptance criterion.
- Format and lint touched code.
- Use official primary sources for exchange/tool behavior.

### 24.3 Handoff format

Every handoff must contain:

```text
Milestone / slice:
Outcome:
Changed files:
Tests added or updated:
Commands run and results:
Performance impact:
Protocol/docs consulted:
Decisions or deviations:
Known risks / follow-ups:
Suggested commit message:
```

Do not state that work is complete unless acceptance criteria have been executed or a concrete environment limitation is documented.

### 24.4 Review protocol for Codex

Codex reviews:

- scope compliance;
- architectural boundaries;
- correctness of protocol normalization and calculations;
- bounded memory/queue behavior;
- error and stale-data semantics;
- test quality and missing adversarial cases;
- build reproducibility and documentation;
- unnecessary complexity or dependencies.

Review findings use severity:

- `S0`: safety/security/data-corruption blocker;
- `S1`: correctness or architecture blocker;
- `S2`: material reliability/performance/maintainability issue;
- `S3`: minor improvement.

Claude resolves findings with targeted commits and repeats relevant checks.

### 24.5 Commit discipline

- Every accepted milestone ends with a clean working tree and reviewed commits.
- Commits are small, coherent, and buildable.
- Use imperative messages, e.g. `feat(feeds): normalize Binance aggregate trades`.
- Do not mix formatting of unrelated files with behavior changes.
- Never rewrite shared history without explicit approval.

## 25. Definition of done

A task is done only when:

- behavior matches this specification and current ADRs;
- tests cover normal, boundary, malformed, stale, and reconnect cases appropriate to the change;
- native and relevant WASM/browser checks pass;
- formatting/lint/static analysis pass;
- resources remain bounded;
- data provenance and failure states are visible;
- documentation and fixtures are updated;
- no unrelated changes are included;
- the handoff template is complete;
- review findings at S0/S1 are resolved.

## 26. Open implementation decisions

These are intentionally delegated to milestone-0 ADRs rather than guessed now:

- SDL3 versus SDL2 fallback after browser spike.
- Catch2 versus GoogleTest.
- Submodule versus immutable `FetchContent` dependency policy.
- Exact decimal/fixed-point implementation.
- Exact JS/WASM batch encoding: structured typed arrays, FlatBuffers, or another measured format.
- WebGL heatmap texture strategy.
- Single-thread versus pthread/worker build after profiling.
- Static hosting provider, provided it supports required headers and privacy constraints.

## 27. Source references

Verify again when implementing; upstream APIs change.

- Binance USD-M Futures market streams: https://developers.binance.com/docs/derivatives/usds-margined-futures/websocket-market-streams
- Binance USD-M REST market data: https://developers.binance.com/docs/derivatives/usds-margined-futures/market-data/rest-api
- Hyperliquid WebSocket: https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/websocket
- Hyperliquid subscriptions and types: https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/websocket/subscriptions
- Hyperliquid info endpoint: https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/info-endpoint
- Emscripten documentation: https://emscripten.org/docs/
- Emscripten pthread constraints: https://emscripten.org/docs/porting/pthreads.html
- Dear ImGui getting started: https://github.com/ocornut/imgui/wiki/Getting-Started
- Dear ImGui backends: https://github.com/ocornut/imgui/blob/master/docs/BACKENDS.md
- Dear ImGui docking: https://github.com/ocornut/imgui/wiki/Docking
- ImPlot: https://github.com/epezent/implot
- SvelteKit: https://svelte.dev/docs/kit
- Playwright: https://playwright.dev/docs/intro

## 28. First action after approval

Create `CLAUDE.md` from section 24, add the Apache-2.0 license and contribution skeleton, then execute M0 only. Do not implement exchange adapters until the toolchain, browser canvas, testing path, build reproducibility, and initial performance measurements are accepted.
