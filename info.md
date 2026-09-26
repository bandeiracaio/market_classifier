# Project research notebook

This repository starts from zero. The references below are inspiration for architecture and engineering choices only; we are not copying Cryexc's product, branding, UI, source, or proprietary behavior.

## Reference

- Original post: https://x.com/josedonato__/status/2063699907563729258
- Product: https://cryexc.josedonato.com/
- App: https://cryexc.josedonato.com/app
- Documentation: https://cryexc.josedonato.com/docs
- Current public history service: https://github.com/jose-donato/cryexc-history
- Deprecated reference backend: https://github.com/jose-donato/cryexc-backend

Research date: 2026-09-24.

## Executive summary

Cryexc is not a conventional React dashboard that sends every event through an application server. Its defining architectural choice is a local-first, high-performance terminal:

1. A lightweight web/PWA shell loads the product.
2. The terminal is written in C++ with Dear ImGui and compiled with Emscripten to WebAssembly.
3. The terminal renders mainly into a WebGL canvas.
4. Public live market data flows directly from exchanges to the browser over WebSockets.
5. CPU-heavy aggregation and rendering happen locally, close to the data.
6. Preferences, layouts, and journal-like data are persisted locally with `localStorage` and IndexedDB.
7. Historical data is treated as a separate concern and can come from a hosted source or a self-hosted Go + DuckDB service.

The important lesson is not "use C++ everywhere." It is to put each responsibility in the cheapest suitable layer: native-speed code for the hot path, a normal web shell for discovery and lifecycle, browser storage for private user state, direct public feeds for live data, and a small optional backend only where history or controlled persistence requires one.

## Confidence-ranked technology map

### Confirmed by the author or public documentation

| Layer | Technology / approach | Evidence |
|---|---|---|
| Core terminal | C++ | Product copy and author posts |
| Immediate-mode UI | Dear ImGui | Author post and Dear ImGui showcase discussion |
| Browser compilation | Emscripten + WebAssembly | Author/product statements; shipped `/wasm/index.js` loader |
| Rendering | WebGL; WebAssembly SIMD is required | Product browser requirements |
| Live transport | Direct exchange WebSockets from the browser | Product and venue documentation |
| Historical context | Exchange REST OHLC endpoints plus optional history services | Product documentation |
| Local persistence | `localStorage` and IndexedDB | Product privacy/FAQ copy |
| Installability | Progressive Web App | Shipped `manifest.webmanifest`; product copy |
| Current self-hosted history backend | Go 1.23+, standard `net/http`, DuckDB through CGO | Public `cryexc-history` repository |
| Older reference backend | Python, FastAPI, Uvicorn, Pydantic, DuckDB | Public archived `cryexc-backend` repository |
| Fonts | JetBrains Mono; Newsreader on parts of the site | Shipped Google Fonts stylesheets |

### Strongly inferred from shipped assets

| Layer | Likely technology | Why |
|---|---|---|
| Marketing/docs/app shell | SvelteKit/Svelte | Hashed assets under `/_app/immutable/nodes` and `/_app/immutable/chunks`, the characteristic SvelteKit production layout |
| Terminal host boundary | JavaScript bridge modules around WASM | Shipped scripts include `layout_library.js`, `touch_bridge.js`, `wallet_bridge.js`, signer bridges, and `custom.js` |
| Charts/integrations | A mixture of native terminal charts and third-party browser widgets | The app loads `s3.tradingview.com/tv.js`; the historical and order-flow UI also visibly renders inside the terminal canvas |
| Hosting/edge | Cloudflare is somewhere in the delivery/observability path | A Cloudflare Insights beacon is loaded by the live app |

These inferred items should be verified again before depending on exact framework APIs. Asset naming proves the delivery shape more strongly than it proves a particular source-level organization.

## Runtime architecture

```text
Exchange public WS feeds ──> browser feed adapters ──> normalized events
                                                        │
                                                        v
                                              WASM/C++ hot path
                                           aggregation + calculations
                                                        │
                                                        v
                                               ImGui/WebGL canvas

Exchange REST APIs ───────────────────────> ordinary OHLC history

Optional history service:
exchange WS ──> Go ingest ──> batched DuckDB persistence ──> versioned HTTP API
                                                          └─> browser backfill

Browser persistence:
settings/layouts/journal ──> localStorage + IndexedDB
```

### Browser side

- The terminal is canvas-first, which explains the desktop-like density, dockable panels, and low DOM overhead.
- Each exchange keeps its native symbol identity (`venue:symbol`) rather than relying on fragile symbol guessing.
- A primary instrument and an aggregate venue set are separate concepts. Some panels consume the aggregate; symbol-specific panels remain tied to the primary.
- Live feeds and historical backfill are explicitly separate. This avoids pretending a WebSocket can provide data from before the session began.
- Settings are saved before a reload when a source change would otherwise require complicated live ownership changes. This is a pragmatic way to reduce state-transition bugs.
- Per-panel settings and per-instance chart configuration prevent global state from coupling independent views.

### Optional Go history service

The public Go service deliberately uses a three-stage, single-process pipeline:

1. **Ingest:** exchange WebSocket clients receive trades, depth, and liquidations and reconnect when dropped.
2. **Persist:** data is inserted into DuckDB in batches, indexed by `(symbol, timestamp_ms)`, and pruned by a retention job.
3. **Serve:** versioned read endpoints expose discovery, trades, and liquidations.

Its deployment philosophy is well matched to a personal/self-hosted tool: one binary and one database file, graceful shutdown, explicit health endpoint, optional bearer token, loopback binding by default, and no Redis/message broker without a demonstrated need.

## Notable product and engineering patterns worth reusing

### 1. Optimize the hot path, not the whole product

Use C++/WASM only for sustained event processing, aggregation, and dense rendering. Keep content pages, routing, installation metadata, and ordinary browser integrations in the web layer. This retains native-like performance without forcing every feature through a low-level toolchain.

### 2. Normalize at explicit boundaries

Every venue has different symbol names and payloads. Give every adapter one job: validate upstream messages and emit a small internal event vocabulary such as `Trade`, `BookSnapshot`, `BookDelta`, `Liquidation`, and `Ticker`. Preserve the original venue and native symbol as identity fields.

### 3. Separate live state from history

Treat these as different data products:

- Live WebSocket data is ephemeral, ordered, reconnectable, and latency-sensitive.
- Historical data is queryable, bounded by retention, cacheable, and may be incomplete in dimensions that were never stored.

Never silently replace missing order-flow history with OHLC data while implying equal fidelity. Label provenance and limitations in the UI.

### 4. Make local-first the default

When data is public and user state is private, a browser-only default reduces infrastructure, latency, account management, and privacy risk. Introduce a backend only for a capability that cannot be honestly delivered locally.

### 5. Prefer a modular monolith at the beginning

A single process with clear internal packages is easier to operate than premature services. Split deployments only after measurements show independent scaling, fault isolation, or ownership needs.

### 6. Use bounded data and backpressure

High-frequency feeds must not create unbounded vectors, queues, IndexedDB tables, or canvas history. Define retention windows, maximum queue depth, batching intervals, and overflow behavior. The UI should expose dropped/gapped data rather than quietly becoming wrong.

### 7. Persist schemas, not implementation accidents

Version network protocols and local persisted state. Add migrations for IndexedDB/DuckDB. Keep message fields pinned and validate at the boundary so a frontend upgrade does not corrupt or reinterpret older data.

### 8. Design reconnects as a state machine

For each feed, track `disconnected -> connecting -> syncing snapshot -> live -> stale/reconnecting`. Apply sequence numbers where exchanges provide them, discard invalid deltas, and visibly mark gaps. Reconnection without gap fill is a documented limitation of the reference history service and should not become an invisible correctness bug in our product.

### 9. Build observability into the product

Show feed health, reconnect count, last event age, queue depth, processing latency, and history coverage. A green dot alone is insufficient for a data-intensive tool.

### 10. Test deterministic transformations heavily

The highest-value tests are replayable fixtures for normalization, order-book reconstruction, candle/footprint aggregation, timestamp boundaries, tick-size rounding, and reconnect/gap behavior. Keep rendering thin over tested models.

## Practices to improve or avoid inheriting blindly

- Do not claim "no analytics scripts" while loading an analytics beacon. Privacy copy, telemetry, and deployed assets must agree.
- Do not use permissive reflected CORS as a substitute for an explicit threat model. Loopback-only defaults help, but remote exposure should use strict origin policy, authentication, rate limits, and TLS at the edge.
- Do not default to floating-point values for accounting-sensitive price/quantity logic. Normalize to integer ticks/lots or decimal representations at ingestion; convert only for presentation.
- Do not rely on full-book snapshots forever if bandwidth or symbol count grows. Start simple, then profile and introduce deltas/shared buffers only when justified.
- Do not let direct exchange connectivity obscure rate limits, regional restrictions, sequence gaps, or inconsistent venue semantics.
- Do not assume WASM automatically makes an app fast. Allocation behavior, JS/WASM boundary crossings, event batching, texture uploads, and draw-call counts still need measurement.
- Do not adopt immediate-mode UI solely for visual similarity. It is excellent for dense tools but brings accessibility, text input, mobile behavior, SEO, and automated-testing tradeoffs.
- Do not copy the deprecated FastAPI backend as the target architecture. It remains useful as a readable protocol example; the newer Go service better represents the current deployment direction.

## Recommended stack for our new project

This is a starting hypothesis, not a commitment. The product requirements should decide whether the C++/WASM complexity is justified.

### If our product is similarly high-frequency and canvas-heavy

| Concern | Recommended choice |
|---|---|
| Repository | Monorepo with explicit `web`, `engine`, `server`, `protocol`, and `docs` boundaries |
| Web shell | SvelteKit + TypeScript |
| High-performance engine | C++20/23 compiled with Emscripten to WASM; SIMD enabled after a scalar fallback or clear compatibility check |
| Dense terminal UI | Dear ImGui docking branch or a maintained docking-capable integration, rendered through WebGL |
| JS/WASM contract | Small, versioned, typed message boundary; batch events rather than crossing per tick |
| Live data | Browser WebSockets through venue adapters; REST for metadata/snapshots/history |
| Local state | `localStorage` only for tiny preferences; IndexedDB for structured/large state, behind a repository interface |
| Optional backend | Go with standard library HTTP/WebSocket support unless a framework earns its cost |
| Embedded analytics store | DuckDB for append/batch analytical workloads; use another store if transactional/multi-user needs dominate |
| Schemas | JSON Schema, Protobuf, or generated TypeScript/C++/Go types from one protocol definition |
| Build | CMake presets for native/WASM builds; pnpm for web workspace; pinned toolchain versions |
| Delivery | Static/edge-hosted PWA plus separately deployable history binary |
| Quality | Unit tests + recorded feed replays + property tests + browser smoke tests + native/WASM benchmarks |
| CI | Format, lint, typecheck, tests, reproducible WASM build, size budget, dependency/security scan |

### If event volume is moderate

Start with SvelteKit + TypeScript + Canvas/WebGL (or a focused charting library) and keep the processing model isolated behind an interface. Add Rust/C++ WASM only after profiling proves the JavaScript implementation misses a concrete latency or throughput budget. This is usually the better first milestone.

## Proposed repository shape

```text
/
├─ apps/
│  ├─ web/                 # SvelteKit shell/PWA
│  └─ server/              # optional Go history/API process
├─ engine/
│  ├─ include/             # stable engine API
│  ├─ src/                 # aggregation/domain code
│  └─ tests/               # deterministic replay tests
├─ packages/
│  ├─ protocol/            # schemas + generated types
│  ├─ feed-adapters/       # venue/source-specific boundaries
│  └─ test-fixtures/       # sanitized recorded streams
├─ docs/
│  ├─ adr/                 # architecture decision records
│  ├─ protocol.md
│  └─ data-provenance.md
├─ CMakePresets.json
├─ pnpm-workspace.yaml
└─ README.md
```

Keep domain calculations independent from ImGui, WebSockets, storage, and clocks. Pass time in, inject storage/feed interfaces, and make recorded streams replayable in both native and WASM test targets.

## Initial engineering rules for this repository

These are the "good practice memories" to carry forward for this project:

1. Measure first; optimize the demonstrated bottleneck.
2. Keep domain logic pure and deterministic where possible.
3. Validate all external data at the boundary.
4. Represent prices/quantities precisely and define rounding explicitly.
5. Make ownership and lifetimes obvious; avoid shared mutable global state.
6. Bound every queue, cache, retention window, and retry loop.
7. Version protocols and persisted schemas from the first public build.
8. Expose data provenance, staleness, and gaps to the user.
9. Prefer one deployable unit until independent scaling is necessary.
10. Use privacy-preserving defaults and keep documentation consistent with reality.
11. Keep secrets and authenticated trading separate from public market-data ingestion.
12. Add dependencies only for a concrete benefit; pin and audit them.
13. Automate formatting, linting, tests, and reproducible builds in CI.
14. Record architectural decisions and their tradeoffs in short ADRs.
15. Maintain a performance budget for startup time, WASM size, frame time, memory, and event latency.

## Questions to answer before scaffolding

- What are we actually building, and which user workflow must the first release complete?
- Is it read-only analytics, simulation, alerts, journaling, execution, or something else?
- What peak events/second, retained history, number of sources, and target frame time are required?
- Must it work offline, on mobile, or across multiple devices?
- Does any feature require accounts, private API keys, payments, or server-side secrets?
- Is local-first a product requirement or merely an implementation preference?
- Which browsers and hardware form the support floor?
- What accuracy rules apply to timestamps, price increments, quantity increments, and cross-source aggregation?

Do not scaffold until these answers establish whether WASM/ImGui is necessary. Architecture should follow constraints, not admiration for a reference implementation.

## Research notes and caveats

- The main Cryexc frontend source is not publicly available in the repositories found during this review. Frontend structure beyond the author's statements and deployed assets is inference.
- `cryexc-backend` is archived and explicitly deprecated in favor of `cryexc-history`.
- The public Go history service currently documents single-exchange/single-symbol limitations and no REST gap fill after WebSocket reconnects.
- The product changes quickly. Revalidate deployed assets and public docs before treating library/version details as fixed.
- TradingView and Cloudflare scripts are visible in the deployed app. Their exact functional scope cannot be established from filenames alone.

## Sources

- Cryexc product and FAQ: https://cryexc.josedonato.com/
- Cryexc docs: https://cryexc.josedonato.com/docs
- Primary/aggregation model: https://cryexc.josedonato.com/docs/primary-aggregation
- Venue/feed model: https://cryexc.josedonato.com/docs/venues-and-composite
- History model: https://cryexc.josedonato.com/docs/history-sources
- Footprint behavior: https://cryexc.josedonato.com/docs/footprint-chart
- Historical chart behavior: https://cryexc.josedonato.com/docs/historical-chart
- Go history service: https://github.com/jose-donato/cryexc-history
- Archived Python reference: https://github.com/jose-donato/cryexc-backend
- Dear ImGui discussion identifying C++/ImGui/Emscripten: https://github.com/ocornut/imgui/issues/9288
