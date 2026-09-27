# MVP BTC Order-Flow Terminal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the BTC-only perpetual order-flow terminal (Binance USD-M `BTCUSDT` + Hyperliquid `BTC`) defined in the MVP packet, deployed as a static GitHub Pages site.

**Architecture:** The TypeScript bridge owns WebSocket/fetch/IndexedDB and forwards *raw, size-bounded venue frames* to C++ through a new versioned `RawFrameBatch` bridge message. C++ venue adapters parse and validate the JSON into existing `domain::NormalizedEvent` types, which flow through `BoundedIngress` into deterministic processors and bounded read models. Dear ImGui panels render read models only. Because adapters live in C++, the same fixtures drive native and WASM replay tests.

**Tech Stack:** C++20, Dear ImGui (docking) + ImPlot, SDL2 Emscripten port, WebGL 2, Catch2 (ADR-0002), fixed-point `Decimal` (ADR-0003), SvelteKit 2 + Svelte 5 + TypeScript, Playwright, CMake presets + Ninja, GitHub Actions + Pages. One new C++ JSON parser dependency, subject to Task 1 approval.

**Spec:** `docs/milestones/MVP_BTC_TERMINAL.md` (active packet) + `SPECIFICATION.md` (non-superseded sections). Read both, plus `CLAUDE.md`, before Task 1.

## Global Constraints

- Scope is exactly the packet. Instruments: Binance `BTCUSDT`, Hyperliquid `BTC`. Nothing else.
- No authenticated endpoints, credentials, trading, telemetry, analytics, PWA/service worker, pthreads, `SharedArrayBuffer`.
- No new dependency without owner approval **and** an ADR (`docs/adr/NNNN-*.md`).
- Price/quantity are `domain::Decimal`; `double` only for rendering and explicitly tolerant statistics.
- Every queue, cache, series, retry loop, log and diagnostic sample is bounded with a named `constexpr` limit.
- External data is untrusted: validate shape, size, numeric bounds, symbol identity, sequence semantics. Rejections are counted, never crash.
- Clocks are injected (`runtime::Clock`); no sleeping or wall-clock reads in processors or tests.
- Network callbacks only enqueue bounded batches; the frame loop drains within a budget (spec §7.2).
- Panels never read raw JSON; they read read models.
- Missing, stale, partial, gap-affected and unsupported are distinct visible states.
- Link every venue behavior to official docs with a verification date (in source comment or `docs/protocols/`).
- Conventional Commits; small buildable commits; handoff in `CLAUDE.md` format at the end of each task.
- Stop and ask the owner on: blocked/changed upstream endpoint, new dependency, incompatible schema/protocol change, weakening a bound.
- Every C++ target keeps `-Wall -Wextra -Wpedantic -Werror` (MSVC `/W4 /WX`).

## Standard commands

```bash
# Native build + tests
cmake --preset native-release && cmake --build --preset native-release && ctest --preset native
# WASM build (emsdk_env active)
emcmake cmake --preset wasm-release && cmake --build --preset wasm-release
# Web
cd apps/web && pnpm check && pnpm lint && pnpm test:bridge && pnpm test
```

"Run all checks" below means all four lines above, plus `clang-format --dry-run --Werror` on touched C++ files.

## File structure (target)

```
engine/include/market_classifier/
  bridge/raw_frame.hpp            RawFrameBatch wire format (C++ side)
  json/json.hpp                   thin wrapper over the chosen JSON parser
  venues/instruments.hpp          hardcoded BTC instrument table
  venues/stream.hpp               StreamTag enum shared by bridge & adapters
  venues/binance_adapter.hpp      Binance frame -> NormalizedEvent
  venues/hyperliquid_adapter.hpp  Hyperliquid frame -> NormalizedEvent
  venues/adapter_result.hpp       AdapterResult / AdapterError
  runtime/feed_state.hpp          per-venue connection/staleness state machine
  runtime/ring.hpp                fixed-capacity ring buffer
  runtime/engine.hpp              owns ingress, processors, read models; frame drain
  books/binance_book_sync.hpp     snapshot+diff sync with gap detection
  books/order_book.hpp            price-level book (Decimal keys)
  processors/trades.hpp           trade tape store
  processors/cvd.hpp              cumulative volume delta
  processors/footprint.hpp        footprint + volume-at-price
  processors/candles.hpp          candle series (preload + live)
  processors/heatmap.hpp          book-over-time ring
  processors/metrics.hpp          funding/OI/mark/basis series + overview
  read_models/*.hpp               immutable views consumed by panels
  ui/panel.hpp                    Panel interface + registry
  ui/panels/*.hpp                 one file per panel
  ui/workspace.hpp                layouts, presets, docking
  ui/workspace_codec.hpp          workspace <-> JSON
engine/src/...                    mirrors include/
engine/tests/...                  one test file per unit
bridge/src/raw-frame.ts           RawFrameBatch encoder (TS side)
bridge/src/venues/*.ts            WebSocket + REST drivers per venue
bridge/src/persistence.ts         IndexedDB store with fallback
apps/web/src/lib/terminal.ts      wires bridge <-> WASM exports
fixtures/mvp/{binance,hyperliquid}/*.json   captured payloads
tools/capture-fixtures.mjs        one-shot capture script
docs/protocols/{binance,hyperliquid}.md     endpoint notes + verification dates
docs/calculations/*.md            formulas not already in spec §11
```

---

### Task 1: Feasibility gate — CORS probe and JSON parser ADR

**Files:**
- Create: `tools/cors-probe.html`, `docs/protocols/cors-verification.md`
- Create: `docs/adr/0005-json-parser.md`
- Modify: `docs/adr/README.md` (index entry)

**Interfaces:**
- Produces: a verified list of browser-reachable endpoints; an accepted parser choice used by Task 3.

- [ ] **Step 1: Write the CORS probe page.** A static page that, from a browser origin, calls each REST endpoint and prints status + whether the response was readable:

```html
<!doctype html><meta charset="utf-8"><title>CORS probe</title><pre id="o"></pre>
<script>
const probes = [
  ['binance exchangeInfo', 'https://fapi.binance.com/fapi/v1/exchangeInfo', {}],
  ['binance depth', 'https://fapi.binance.com/fapi/v1/depth?symbol=BTCUSDT&limit=1000', {}],
  ['binance klines', 'https://fapi.binance.com/fapi/v1/klines?symbol=BTCUSDT&interval=1m&limit=1000', {}],
  ['binance openInterest', 'https://fapi.binance.com/fapi/v1/openInterest?symbol=BTCUSDT', {}],
  ['hl meta', 'https://api.hyperliquid.xyz/info', {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({type:'metaAndAssetCtxs'})}],
  ['hl candleSnapshot', 'https://api.hyperliquid.xyz/info', {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({type:'candleSnapshot',req:{coin:'BTC',interval:'1m',startTime:Date.now()-6e7,endTime:Date.now()}})}],
];
const o = document.getElementById('o');
for (const [name, url, init] of probes) {
  fetch(url, init).then(async r => { const t = await r.text(); o.textContent += `${name}: ${r.status} readable ${t.length} bytes\n`; })
    .catch(e => { o.textContent += `${name}: BLOCKED ${e}\n`; });
}
</script>
```

- [ ] **Step 2: Serve it from a real origin and record results.** Run `npx --yes http-server tools -p 8080` (or `python -m http.server 8080 -d tools`), open `http://localhost:8080/cors-probe.html` in Chrome and Firefox. Also open both WebSocket endpoints (`wss://fstream.binance.com/stream`, `wss://api.hyperliquid.xyz/ws`) from the devtools console. Write every result with today's date into `docs/protocols/cors-verification.md`.

- [ ] **Step 3: If any endpoint is BLOCKED, STOP and report to the owner.** Do not invent a proxy.

- [ ] **Step 4: Draft ADR-0005.** Compare at least `yyjson` (C, MIT, single file), `simdjson` (Apache-2.0; SIMD value on WASM is limited), `nlohmann/json` (MIT; large, exceptions). Criteria: license, WASM size delta (measure: build a hello-parse WASM with each, record `.wasm` bytes), exception-free API, bounded input, maintenance. Recommend one; pinned via `FetchContent` with a commit hash per ADR-0001. Status: `Proposed`.

- [ ] **Step 5: STOP and ask the owner to approve ADR-0005.** On approval set `Status: Accepted` with the date.

- [ ] **Step 6: Commit**

```bash
git add tools/cors-probe.html docs/protocols/cors-verification.md docs/adr/0005-json-parser.md docs/adr/README.md
git commit -m "docs(mvp): verify browser CORS and choose JSON parser"
```

---

### Task 2: Fixture capture

**Files:**
- Create: `tools/capture-fixtures.mjs`, `fixtures/mvp/README.md`
- Create: `fixtures/mvp/binance/{exchangeInfo,depth_snapshot,depth_diff,aggTrade,bookTicker,markPrice,ticker,kline,klines_rest,forceOrder,openInterest}.json`
- Create: `fixtures/mvp/hyperliquid/{metaAndAssetCtxs,l2Book,trades,bbo,activeAssetCtx,candle,candleSnapshot,subscription_ack}.json`
- Create: `fixtures/mvp/binance/depth_sequence.jsonl` (≥200 consecutive diffs + one snapshot taken during them, for book-sync replay)

**Interfaces:**
- Produces: committed fixture files consumed by Tasks 4, 5, 7, 8.

- [ ] **Step 1: Write the capture script** (Node 22, built-in `WebSocket` and `fetch`, no dependencies). It connects to both venues, subscribes to every stream in packet §4, writes the first N messages per stream (N=5, `depth_sequence` N=200), trims `exchangeInfo` / `meta` to BTC entries only, caps each file at 256 KiB, and records capture time in `fixtures/mvp/README.md`. Binance combined stream URL: `wss://fstream.binance.com/stream?streams=btcusdt@aggTrade/btcusdt@depth@100ms/btcusdt@bookTicker/btcusdt@markPrice@1s/btcusdt@ticker/btcusdt@kline_1m/btcusdt@forceOrder`. Hyperliquid: send `{"method":"subscribe","subscription":{"type":"trades","coin":"BTC"}}` and likewise for `l2Book`, `bbo`, `activeAssetCtx`, `candle` (`interval:"1m"`).
- [ ] **Step 2: Run it:** `node tools/capture-fixtures.mjs`. `forceOrder` may be quiet; wait up to 10 minutes, otherwise hand-write one message from the official docs example and mark it `"_synthetic": true` in the README.
- [ ] **Step 3: Add hand-made malformed fixtures** under `fixtures/mvp/malformed/`: wrong symbol, non-numeric price, 70-char decimal, missing field, truncated JSON, 2 MiB frame marker (generated in test, not committed).
- [ ] **Step 4: Write `docs/protocols/binance.md` and `docs/protocols/hyperliquid.md`**: for each stream, the official doc URL, verification date, field meanings used, and limits (Binance: 24h connection lifetime, ping every 3 min, 10 msg/s inbound limit; Hyperliquid: ping/`pong` heartbeat, subscription ack).
- [ ] **Step 5: Commit** `feat(tools): capture venue fixtures for MVP streams`.

---

### Task 3: Raw frame bridge message and JSON wrapper

**Files:**
- Create: `engine/include/market_classifier/venues/stream.hpp`, `engine/include/market_classifier/bridge/raw_frame.hpp`, `engine/src/bridge/raw_frame.cpp`
- Create: `engine/include/market_classifier/json/json.hpp`, `engine/src/json/json.cpp`
- Create: `bridge/src/raw-frame.ts`, `bridge/tests/raw-frame.test.mjs`
- Create: `engine/tests/test_raw_frame.cpp`, `engine/tests/test_json.cpp`
- Modify: `CMakeLists.txt` (FetchContent for parser), `engine/CMakeLists.txt`, `engine/tests/CMakeLists.txt`, `docs/adr/0004-bridge-batch-encoding.md` (append RawFrameBatch section)

**Interfaces:**
- Produces:

```cpp
namespace market_classifier::venues {
enum class StreamTag : std::uint8_t {
    BinanceExchangeInfo = 1, BinanceDepthSnapshot, BinanceWs /*combined stream frame*/,
    BinanceKlinesRest, BinanceOpenInterestRest,
    HyperliquidMeta = 32, HyperliquidWs, HyperliquidCandleSnapshot,
};
}
namespace market_classifier::bridge {
inline constexpr std::size_t k_max_raw_frame_bytes  = 512 * 1024; // depth snapshot 1000 levels fits
inline constexpr std::size_t k_max_frames_per_batch = 64;
struct RawFrame { venues::StreamTag tag; std::int64_t receive_time_ms; std::string payload; };
struct RawFrameDecode { std::vector<RawFrame> frames; DecodeError error = DecodeError::None;
                        explicit operator bool() const noexcept { return error == DecodeError::None; } };
[[nodiscard]] RawFrameDecode decode_raw_frames(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::uint8_t> encode_raw_frames(std::span<const RawFrame> frames);
}
namespace market_classifier::json {
// Owns a parsed document; all accessors are bounds/type checked and return std::optional.
class Document { public:
    static std::optional<Document> parse(std::string_view text, std::size_t max_bytes);
    Value root() const noexcept; };
class Value { public:
    std::optional<Value> get(std::string_view key) const noexcept;
    std::optional<Value> at(std::size_t index) const noexcept;
    std::size_t size() const noexcept;           // array/object length, 0 otherwise
    std::optional<std::string_view> string() const noexcept;
    std::optional<std::int64_t> int64() const noexcept;
    std::optional<bool> boolean() const noexcept;
    std::optional<domain::Decimal> decimal() const noexcept; // accepts JSON string or number text, via Decimal::parse
};
}
```

Wire format: header identical to ADR-0004 (magic, `k_protocol_version`, kind, flags, length) with `MessageKind::RawFrameBatch = 2`; body: `u16 count`, then per frame `u8 tag, i64 receive_time_ms, u32 length, bytes`. Little-endian.

TS side:

```ts
export const enum StreamTag { BinanceExchangeInfo = 1, BinanceDepthSnapshot, BinanceWs, BinanceKlinesRest, BinanceOpenInterestRest, HyperliquidMeta = 32, HyperliquidWs, HyperliquidCandleSnapshot }
export interface RawFrame { tag: StreamTag; receiveTimeMs: bigint; payload: string }
export function encodeRawFrames(frames: readonly RawFrame[]): Uint8Array; // throws RangeError on any bound violation
```

- [ ] **Step 1: Write failing C++ tests** (`test_raw_frame.cpp`):

```cpp
TEST_CASE("raw frame batch round-trips") {
    std::vector<bridge::RawFrame> in{{venues::StreamTag::BinanceWs, 1700000000000, R"({"a":1})"},
                                     {venues::StreamTag::HyperliquidWs, 1700000000001, "{}"}};
    const auto bytes = bridge::encode_raw_frames(in);
    const auto out   = bridge::decode_raw_frames(bytes);
    REQUIRE(out);
    REQUIRE(out.frames.size() == 2);
    CHECK(out.frames[0].payload == R"({"a":1})");
    CHECK(out.frames[1].tag == venues::StreamTag::HyperliquidWs);
}
TEST_CASE("raw frame rejects unknown tag, oversize frame, count overflow, truncation") {
    auto bytes = bridge::encode_raw_frames(std::vector<bridge::RawFrame>{{venues::StreamTag::BinanceWs, 1, "{}"}});
    auto bad_tag = bytes; bad_tag[bridge::k_header_bytes + 2] = 0xEE;
    CHECK(bridge::decode_raw_frames(bad_tag).error == bridge::DecodeError::InvalidEnum);
    auto truncated = bytes; truncated.pop_back();
    CHECK_FALSE(bridge::decode_raw_frames(truncated));
    std::vector<bridge::RawFrame> many(bridge::k_max_frames_per_batch + 1, {venues::StreamTag::BinanceWs, 1, "{}"});
    CHECK(bridge::encode_raw_frames(many).empty()); // encoder refuses; decoder test below crafts the header
}
TEST_CASE("TS-encoded fixture decodes identically") {
    // fixtures/mvp/raw-frame-batch-v1.hex written by bridge/tests/raw-frame.test.mjs
}
```

And `test_json.cpp`: parse valid object; `get` missing key → `nullopt`; `decimal()` on `"63000.10"` → mantissa 6300010 scale 2; on `"1e5"` → nullopt (Decimal grammar); input over `max_bytes` → `nullopt`; truncated JSON → `nullopt`; depth > 64 → `nullopt`.

- [ ] **Step 2: Run** `ctest --preset native -R "raw frame|json"` → FAIL (not defined).
- [ ] **Step 3: Implement** encoder/decoder mirroring `protocol.cpp` style (explicit little-endian readers, every length checked before read), and the JSON wrapper over the ADR-0005 parser (wrapper is the only file that includes the parser header).
- [ ] **Step 4: Write the TS encoder + `node --test`** that round-trips via a decoder in the test and writes `fixtures/mvp/raw-frame-batch-v1.hex`; fill in the C++ fixture test to decode it.
- [ ] **Step 5: Run all checks** → PASS.
- [ ] **Step 6: Commit** `feat(bridge): add versioned raw frame batch and JSON wrapper`.

---

### Task 4: Venue adapters and instrument table

**Files:**
- Create: `engine/include/market_classifier/venues/{instruments,adapter_result,binance_adapter,hyperliquid_adapter}.hpp` + `src/venues/*.cpp`
- Create: `engine/tests/test_binance_adapter.cpp`, `engine/tests/test_hyperliquid_adapter.cpp`
- Modify: `engine/include/market_classifier/domain/events.hpp` only if a field is truly missing — then STOP and ask (persisted/protocol contract).

**Interfaces:**
- Consumes: `bridge::RawFrame`, `json::Document`, `domain::*`.
- Produces:

```cpp
namespace market_classifier::venues {
struct InstrumentSpec { domain::Venue venue; std::string_view native_symbol; std::string_view display; };
inline constexpr std::array<InstrumentSpec, 2> k_instruments{{
    {domain::Venue::BinanceUsdM, "BTCUSDT", "BTC-PERP (Binance)"},
    {domain::Venue::Hyperliquid, "BTC", "BTC-PERP (Hyperliquid)"} }};

enum class AdapterError : std::uint8_t { None, Malformed, WrongSymbol, OutOfBounds, UnknownStream, Ignored /*acks, pongs*/ };
struct AdapterResult {
    std::vector<domain::NormalizedEvent> events;
    std::vector<std::uint64_t> binance_prev_final_update_ids; // aligned with events; 0 for non-BookDelta
    AdapterError error = AdapterError::None;
};

class BinanceAdapter {
  public:
    // local_sequence is assigned from a counter owned by the adapter.
    AdapterResult adapt(const bridge::RawFrame &frame);
};
class HyperliquidAdapter { public: AdapterResult adapt(const bridge::RawFrame &frame); };
}
```

Mapping (normative): Binance combined stream `{"stream":"btcusdt@aggTrade","data":{...}}` → `Trade` (`m==true` ⇒ aggressor `Sell`; source id `"a"`; notional = price×qty, exact, rescale bounded by `Decimal::k_max_scale`); `depthUpdate` → `BookDelta` (`first_source_sequence=U`, `last_source_sequence=u`; `pu` goes to the side channel described below); `bookTicker` → `Bbo`; `markPriceUpdate` → `AssetMetrics` (`p` mark, `i` index, `r` funding, `T` next funding); `24hrTicker` → `MarketSummary`; `kline` → `Candle` (`x` closed); `forceOrder` → `Liquidation` (`S=="SELL"` ⇒ `Long` liquidated); REST depth → `BookSnapshot` (`source_sequence=lastUpdateId`); REST klines → `Candle[]` (closed except last); REST openInterest → `OpenInterest` (`sample_interval_ms` = poll cadence constant). Hyperliquid `{"channel":"trades","data":[...]}` → `Trade[]` (`side=="B"` ⇒ `Buy`, `"A"` ⇒ `Sell`; source id `tid`); `l2Book` → `BookSnapshot` (`levels[0]` bids, `levels[1]` asks, `n` order count); `bbo` → `Bbo`; `activeAssetCtx` → `AssetMetrics` (`markPx`, `oraclePx`, `funding`) + `OpenInterest` (`openInterest`, notional = OI×mark) + `MarketSummary` (`dayNtlVlm`, `prevDayPx`); `candle` → `Candle`; `subscriptionResponse`/`pong` → `Ignored`.

> Decision recorded here: `pu` (previous final update id) is required for Binance futures continuity. The adapter emits a Binance-specific `BookDelta` plus a side-channel `prev_final_update_id` via `AdapterResult::binance_prev_final_update_ids` (vector aligned to events). This avoids changing the shared domain contract. If the reviewer prefers a domain change, STOP and ask.

- [ ] **Step 1: Write failing tests per stream from fixtures.** Pattern (repeat for every file in `fixtures/mvp/binance/` and `fixtures/mvp/hyperliquid/`, asserting every mapped field of the first message against values read by hand from the fixture):

```cpp
TEST_CASE("binance aggTrade maps to Trade") {
    venues::BinanceAdapter adapter;
    const auto frame = load_frame("binance/aggTrade.json", venues::StreamTag::BinanceWs, 42);
    const auto r = adapter.adapt(frame);
    REQUIRE(r.error == venues::AdapterError::None);
    REQUIRE(r.events.size() == 1);
    const auto &t = std::get<domain::Trade>(r.events[0]);
    CHECK(t.meta.instrument().native_symbol() == "BTCUSDT");
    CHECK(t.meta.receive_time().value == 42);
    CHECK(t.price == domain::Decimal::parse("<p from fixture>").value);
    CHECK(t.aggressor_side == (fixture_m ? domain::AggressorSide::Sell : domain::AggressorSide::Buy));
}
TEST_CASE("adapters reject malformed input without throwing") {
    for (auto name : {"wrong_symbol", "bad_price", "long_decimal", "missing_field", "truncated"}) {
        CHECK(venues::BinanceAdapter{}.adapt(load_malformed(name)).error != venues::AdapterError::None);
    }
}
TEST_CASE("hyperliquid liquidation capability is unsupported") {
    CHECK_FALSE(venues::supports_liquidations(domain::Venue::Hyperliquid));
    CHECK(venues::supports_liquidations(domain::Venue::BinanceUsdM));
}
```

`load_frame` reads `MC_SOURCE_DIR "/fixtures/mvp/..."` into a `RawFrame`.

- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement** adapters; each field read via `json::Value` optionals; any `nullopt` ⇒ `Malformed`; symbol not in `k_instruments` ⇒ `WrongSymbol`; negative quantity, zero price, level count > `runtime::k_max_levels_per_event` ⇒ `OutOfBounds`. Add `supports_liquidations(Venue)`.
- [ ] **Step 4: Run all checks** → PASS.
- [ ] **Step 5: Commit** `feat(venues): add Binance and Hyperliquid BTC adapters`.

---

### Task 5: Order books and Binance sync

**Files:**
- Create: `engine/include/market_classifier/books/{order_book,binance_book_sync}.hpp` + src
- Create: `engine/tests/test_order_book.cpp`, `engine/tests/test_binance_book_sync.cpp`
- Create: `docs/calculations/book-sync.md`

**Interfaces:**

```cpp
namespace market_classifier::books {
inline constexpr std::size_t k_max_book_levels_per_side = 5000;
class OrderBook {
  public:
    void apply_snapshot(const domain::BookSnapshot &s);           // replaces
    bool apply_levels(std::span<const domain::BookLevel> bids, std::span<const domain::BookLevel> asks); // qty 0 removes; false if bound hit
    std::span<const domain::BookLevel> bids() const noexcept;      // descending price
    std::span<const domain::BookLevel> asks() const noexcept;      // ascending price
    std::optional<domain::BookLevel> best_bid() const noexcept;
    std::optional<domain::BookLevel> best_ask() const noexcept;
    bool crossed() const noexcept;
};
enum class SyncState : std::uint8_t { AwaitingSnapshot, Live, GapDetected };
struct SyncAction { bool request_snapshot = false; };
class BinanceBookSync {
  public:
    static constexpr std::size_t k_max_buffered_diffs = 2048;
    SyncAction on_delta(const domain::BookDelta &d, std::uint64_t prev_final_update_id);
    SyncAction on_snapshot(const domain::BookSnapshot &s);
    SyncState state() const noexcept;
    const OrderBook &book() const noexcept;
    std::uint64_t resync_count() const noexcept;
};
}
```

Algorithm (Binance USD-M "How to manage a local order book correctly", cite URL + date in `book-sync.md`): buffer diffs while `AwaitingSnapshot` (overflow ⇒ drop oldest, count, stay awaiting); on snapshot with `lastUpdateId=L`: drop buffered diffs with `u < L`; the first applied diff must satisfy `U <= L && u >= L`; afterwards each diff must have `pu == previous u`, else ⇒ `GapDetected`, clear book, `request_snapshot=true`. A crossed book after apply ⇒ also `GapDetected`.

- [ ] **Step 1: Failing tests**:

```cpp
TEST_CASE("sync applies first diff straddling snapshot then continuous diffs") {
    books::BinanceBookSync sync;
    CHECK(sync.on_delta(delta(/*U*/95, /*u*/105, {{"100.0","1"}}, {}), 90).request_snapshot);
    CHECK(sync.on_snapshot(snapshot(/*L*/100, {{"99.0","2"}}, {{"101.0","3"}})).request_snapshot == false);
    CHECK(sync.state() == books::SyncState::Live);
    CHECK(sync.book().best_bid()->price == dec("100.0"));
    sync.on_delta(delta(106, 110, {{"100.0","0"}}, {}), 105);
    CHECK(sync.book().best_bid()->price == dec("99.0"));
}
TEST_CASE("pu discontinuity triggers gap and resync request") {
    auto sync = live_sync_at(110);
    CHECK(sync.on_delta(delta(115, 120, {}, {}), /*pu*/112).request_snapshot);
    CHECK(sync.state() == books::SyncState::GapDetected);
    CHECK(sync.resync_count() == 1);
}
TEST_CASE("stale diffs before snapshot are discarded") { /* diffs with u < L never touch book */ }
TEST_CASE("buffer overflow is bounded and counted") { /* push k_max_buffered_diffs+10 */ }
TEST_CASE("replay of captured depth_sequence.jsonl yields uncrossed live book") {
    // feed fixtures/mvp/binance/depth_sequence.jsonl through BinanceAdapter then sync
}
TEST_CASE("order book level bound") { /* apply > k_max_book_levels_per_side distinct levels -> false */ }
```

- [ ] **Step 2: Run** → FAIL. **Step 3: Implement** (sorted `std::vector<BookLevel>` per side with binary-search insert; sufficient at ≤5000 levels). **Step 4: Run all checks** → PASS.
- [ ] **Step 5: Commit** `feat(books): add order book and Binance snapshot/diff sync`.

---

### Task 6: Feed state machine and engine drain

**Files:**
- Create: `engine/include/market_classifier/runtime/{feed_state,ring,engine}.hpp` + src
- Create: `engine/tests/test_feed_state.cpp`, `engine/tests/test_engine.cpp`, `engine/tests/test_ring.cpp`
- Modify: `engine/include/market_classifier/bridge/browser_api.hpp`, `engine/src/bridge/browser_api.cpp`
- Create: `docs/runtime/mvp-feeds.md` (states, thresholds, backoff table)

**Interfaces:**

```cpp
namespace market_classifier::runtime {
template <typename T, std::size_t N> class Ring {  // fixed capacity, overwrites oldest
  public: void push(T v); std::size_t size() const noexcept; const T &operator[](std::size_t i) const; // 0 = oldest
          std::uint64_t overwritten() const noexcept; void clear() noexcept; };

enum class FeedPhase : std::uint8_t { Connecting, Live, Stale, Reconnecting, Failed };
struct FeedConfig { std::int64_t stale_after_ms = 5000; std::int64_t backoff_base_ms = 500;
                    std::int64_t backoff_cap_ms = 30000; std::uint32_t max_attempts_before_failed = 20; };
class FeedState {
  public:
    FeedState(FeedConfig cfg, std::uint64_t jitter_seed);
    void on_open(std::int64_t now_ms); void on_message(std::int64_t now_ms);
    void on_close(std::int64_t now_ms);               // schedules reconnect
    void tick(std::int64_t now_ms);                   // Live -> Stale when silent
    bool should_reconnect(std::int64_t now_ms) const; // bridge asks each frame
    FeedPhase phase() const noexcept; std::int64_t age_ms(std::int64_t now_ms) const noexcept;
    std::uint32_t attempt() const noexcept; domain::DataQuality quality() const noexcept;
};

class Engine {   // single owner of all runtime state; one per app
  public:
    explicit Engine(const Clock &clock);
    SubmitResult submit_raw(std::span<const std::uint8_t> bytes); // decode_raw_frames -> adapters -> ingress
    void frame(std::int64_t budget_ms);   // drain ingress within budget, feed processors, tick feeds
    void on_socket_event(domain::Venue v, int kind /*0 open,1 close,2 error*/);
    const FeedState &feed(domain::Venue v) const;
    // read-model accessors added by Task 7
};
}
```

Backoff: `min(cap, base * 2^attempt) * (0.5 + 0.5 * u)` with `u` from a seeded `std::minstd_rand` (deterministic in tests). A venue's state never touches the other venue's objects.

New WASM exports (C ABI, added to `EXPORTED_FUNCTIONS`): `mc_submit_raw(ptr,len)`, `mc_socket_event(venue,kind)`, `mc_should_reconnect(venue)`, `mc_request_snapshot(venue)` (returns 1 when the Binance sync wants a REST snapshot).

- [ ] **Step 1: Failing tests**:

```cpp
TEST_CASE("silence moves Live to Stale with age") {
    runtime::FeedState f({.stale_after_ms = 5000}, 1);
    f.on_open(0); f.on_message(100); f.tick(5101);
    CHECK(f.phase() == runtime::FeedPhase::Stale);
    CHECK(f.age_ms(5101) == 5001);
    f.on_message(5200); CHECK(f.phase() == runtime::FeedPhase::Live);
}
TEST_CASE("backoff grows, caps, and is deterministic for a seed") { /* two instances, same seed, same schedule; delay <= cap */ }
TEST_CASE("disconnecting one venue leaves the other untouched") {
    runtime::FakeClock clock; runtime::Engine e(clock);
    feed_live(e, domain::Venue::BinanceUsdM); feed_live(e, domain::Venue::Hyperliquid);
    e.on_socket_event(domain::Venue::Hyperliquid, 1);
    CHECK(e.feed(domain::Venue::Hyperliquid).phase() == runtime::FeedPhase::Reconnecting);
    CHECK(e.feed(domain::Venue::BinanceUsdM).phase() == runtime::FeedPhase::Live);
}
TEST_CASE("engine frame drains at most budget and keeps remainder bounded") { /* flood submit, counters show drops */ }
TEST_CASE("ring overwrites oldest and counts") { /* Ring<int,3> push 5 -> [2,3,4], overwritten()==2 */ }
```

- [ ] **Step 2: Run** → FAIL. **Step 3: Implement.** Replace the M1 `DummyReadModel` use in `browser_api.cpp` with `Engine` (keep M1 exports working until Task 10 removes the dummy path; M1 tests must stay green). **Step 4: Run all checks** → PASS.
- [ ] **Step 5: Commit** `feat(runtime): add per-venue feed state and engine drain`.

---

### Task 7: Processors and read models

**Files:**
- Create: `engine/include/market_classifier/processors/{trades,cvd,footprint,candles,heatmap,metrics}.hpp` + src
- Create: `engine/include/market_classifier/read_models/views.hpp`
- Create: `engine/tests/test_processors_{trades,cvd,footprint,candles,heatmap,metrics}.cpp`, `engine/tests/test_replay_mvp.cpp`
- Create: `docs/calculations/{cvd,footprint,heatmap,basis}.md`
- Modify: `runtime/engine.hpp/.cpp` (own one processor set per venue; expose views)

**Interfaces (bounds are normative, from packet §6):**

```cpp
namespace market_classifier::processors {
inline constexpr std::size_t k_tape_capacity        = 5000;
inline constexpr std::size_t k_heatmap_columns      = 14400;   // 60 min at 250 ms
inline constexpr std::int64_t k_heatmap_column_ms   = 250;
inline constexpr std::size_t k_heatmap_levels       = 200;     // per side per column, nearest to mid
inline constexpr std::size_t k_candle_capacity      = 2000;    // per interval
inline constexpr std::size_t k_series_minutes       = 1440;    // CVD/footprint/metrics 24h at 1m
inline constexpr std::array<std::int64_t, 6> k_candle_intervals_ms{60'000, 300'000, 900'000, 3'600'000, 14'400'000, 86'400'000};
inline constexpr std::array<std::string_view, 4> k_bucket_sizes{"1", "5", "10", "25"}; // USD

class TradeTape { public: void on_trade(const domain::Trade &); const runtime::Ring<domain::Trade, k_tape_capacity> &trades() const; };
class Cvd { public:  // Buy adds qty, Sell subtracts, Unknown ignored & counted
    void on_trade(const domain::Trade &); void reset(); void mark_gap(std::int64_t t_ms);
    domain::Decimal value() const; /* per-minute closes */ const runtime::Ring<CvdPoint, k_series_minutes> &series() const;
    bool daily_reset_enabled = false; /* at 00:00 UTC via source_time */ };
class Footprint { public:
    explicit Footprint(domain::Decimal bucket);   // price floored to bucket
    void on_trade(const domain::Trade &); void set_interval(std::int64_t ms); void mark_gap(std::int64_t t_ms);
    std::span<const FootprintCandle> candles() const;  // bounded k_series_minutes
    const VolumeProfile &session_profile() const;      // bid/ask vol per bucket + POC
};
class CandleSeries { public:
    void preload(std::span<const domain::Candle>); void on_candle(const domain::Candle &); // upsert by open_time
    std::span<const domain::Candle> candles(std::int64_t interval_ms) const; };
class Heatmap { public:
    void on_book(const books::OrderBook &, std::int64_t t_ms); // sample once per column boundary
    void on_trade(const domain::Trade &); const HeatmapView &view() const; };
class Metrics { public:
    void on_asset_metrics(const domain::AssetMetrics &); void on_open_interest(const domain::OpenInterest &);
    void on_summary(const domain::MarketSummary &); void on_bbo(const domain::Bbo &);
    const MetricsView &view() const; };
// Cross-venue basis (Overview): Binance mid − Hyperliquid mid, and in bps of Hyperliquid mid.
std::optional<BasisView> cross_venue_basis(const Metrics &binance, const Metrics &hyperliquid);
}
```

Every view carries `domain::DataQuality quality` and `std::int64_t last_update_ms`. Decimal arithmetic for sums (add/sub helpers checked for overflow; on overflow mark `Failed` for that series rather than wrap). `double` conversion happens only inside views for plotting.

- [ ] **Step 1: Failing tests** — one file per processor. Required cases:
  - CVD: buy 1.5 → +1.5; sell 0.5 → 1.0; Unknown ignored and counted; daily reset at 00:00 UTC boundary using `source_time` 86'400'000·k; gap marker inserted on `mark_gap`.
  - Footprint: bucket `"5"`, trades at 63002.4 (buy 1) and 63004.9 (sell 2) land in bucket 63000 with bid 2 / ask 1; new interval starts new candle; POC is max total bucket; capacity bound holds after 2000 minutes.
  - Candles: preload 1000 then live update for same `open_time` replaces, next `open_time` appends; capacity 2000 holds.
  - Heatmap: two book samples within one 250 ms column keep one column; 14401 columns → 14400 retained; only 200 levels per side kept.
  - Metrics/basis: known mids produce exact basis and bps (document rounding: bps rounded half-even to 2 dp, presentation only).
  - Tape: 5001 trades → 5000 retained, oldest dropped.
- [ ] **Step 2: Replay test** `test_replay_mvp.cpp`: feed every fixture through `Engine::submit_raw` with a `FakeClock`, run `frame()`, snapshot key view values into a string, and compare against `fixtures/mvp/replay-expected.txt` (generated once, reviewed by hand, committed). Run twice in one process and assert identical output (determinism).
- [ ] **Step 3: Run** → FAIL. **Step 4: Implement.** **Step 5: Run all checks** → PASS.
- [ ] **Step 6: Commit** in two commits: `feat(processors): add tape, CVD, footprint, candles` and `feat(processors): add heatmap, metrics, cross-venue basis`.

---

### Task 8: TypeScript venue drivers (WebSocket + REST)

**Files:**
- Create: `bridge/src/venues/binance.ts`, `bridge/src/venues/hyperliquid.ts`, `bridge/src/venues/driver.ts`
- Create: `bridge/tests/drivers.test.mjs`
- Modify: `apps/web/src/lib/terminal.ts`

**Interfaces:**

```ts
export interface EngineSink {           // implemented by terminal.ts over WASM exports
  submitRaw(bytes: Uint8Array): number;
  socketEvent(venue: Venue, kind: 0 | 1 | 2): void;
  shouldReconnect(venue: Venue): boolean;
  requestSnapshot(venue: Venue): boolean;
}
export interface VenueDriver { start(): void; stop(): void; pump(nowMs: number): void } // pump called once per animation frame
export function createBinanceDriver(sink: EngineSink, deps: { WebSocket: typeof WebSocket; fetch: typeof fetch; now: () => number }): VenueDriver;
export function createHyperliquidDriver(sink: EngineSink, deps: {...same}): VenueDriver;
```

Rules: frames are buffered in the driver (cap 64 frames / 1 MiB; overflow drops oldest and increments a counter exposed via `sink`) and flushed once per `pump` via `encodeRawFrames`. Frames larger than `k_max_raw_frame_bytes` are dropped and counted. Reconnect only when `sink.shouldReconnect(venue)` (backoff lives in C++). Binance: on `requestSnapshot` fetch REST depth (`limit=1000`); poll `openInterest` every 10 s (constant `OI_POLL_MS`, reported as cadence); reconnect proactively before the 24 h limit. Hyperliquid: send `{"method":"ping"}` every 30 s; resubscribe all streams after reconnect. Startup: fetch metadata, klines/candleSnapshot preload for all 6 intervals (Binance weight budget documented in `docs/protocols/binance.md`).

- [ ] **Step 1: Failing tests** with a fake `WebSocket` and fake `fetch`: open → subscription messages sent (assert exact JSON for Hyperliquid; exact URL for Binance combined stream); 100 messages then `pump` → exactly one `submitRaw` with ≤64 frames; oversize frame dropped and counted; close → `socketEvent(venue,1)` and no reconnect until `shouldReconnect` returns true; `requestSnapshot` true → one depth fetch.
- [ ] **Step 2: Run** `pnpm test:bridge` → FAIL. **Step 3: Implement.** **Step 4: Wire** in `terminal.ts`: create both drivers after `onRuntimeInitialized`, call `pump` from `requestAnimationFrame` before the WASM frame. **Step 5: Run all checks** → PASS; manual `pnpm dev` shows non-zero ingress counters for both venues in the existing debug overlay.
- [ ] **Step 6: Commit** `feat(bridge): add Binance and Hyperliquid browser drivers`.

---

### Task 9: Panel framework and the 13 panels

**Files:**
- Create: `engine/include/market_classifier/ui/panel.hpp`, `ui/quality_badge.hpp`, `ui/panels/{overview,trades_tape,ladder,depth,candles,cvd,footprint,volume_profile,heatmap,liquidations,bbo_spread,funding_oi,diagnostics}.hpp` + src
- Create: `engine/src/app/app.cpp` (moves UI loop out of `main.cpp`; `main.cpp` keeps only platform setup)
- Create: `engine/tests/test_panel_registry.cpp`
- Modify: `engine/CMakeLists.txt` (new `mc_ui` library linking `mc_domain`, `mc_imgui`, `mc_implot`)

**Interfaces:**

```cpp
namespace market_classifier::ui {
enum class VenueSelection : std::uint8_t { Binance, Hyperliquid, Both };
enum class PanelKind : std::uint8_t { Overview, TradesTape, Ladder, Depth, Candles, Cvd, Footprint,
    VolumeProfile, Heatmap, Liquidations, BboSpread, FundingOi, Diagnostics };
struct PanelSettings { VenueSelection venue = VenueSelection::Binance; std::int64_t interval_ms = 60'000;
    std::uint8_t bucket_index = 1; double min_trade_usd = 0; bool cvd_daily_reset = false; };
class Panel { public: virtual ~Panel() = default;
    virtual PanelKind kind() const = 0; virtual void draw(const runtime::Engine &, PanelSettings &) = 0; };
struct PanelTraits { PanelKind kind; std::string_view title; bool supports_both; };
std::span<const PanelTraits> panel_traits();          // one entry per PanelKind
std::unique_ptr<Panel> make_panel(PanelKind);
void draw_quality_badge(domain::DataQuality q, std::int64_t age_ms); // text + color, never color alone
}
```

Panels whose `supports_both` is false clamp `Both` to `Binance`. Liquidations with Hyperliquid draws `Unsupported` badge and an explanatory line. Every panel draws `draw_quality_badge` in its header row.

- [ ] **Step 1: Failing test** `test_panel_registry.cpp`: `panel_traits()` has 13 entries, unique kinds, `supports_both` matches packet §5 table (Overview/Tape/Depth/Candles/CVD/Liquidations/BBO/FundingOi true; Ladder/Footprint/VolumeProfile/Heatmap false; Diagnostics false); a `make_panel` for every kind is non-null. (Registry lives in `mc_ui` but the traits table is pure data and testable natively; link `mc_ui` into tests with ImGui headless context created in the test.)
- [ ] **Step 2: Run** → FAIL. **Step 3: Implement registry + badge.**
- [ ] **Step 4: Implement panels one at a time**, each its own commit `feat(ui): add <panel> panel`, drawing only from views. Visual rules: buy/sell use both color and `▲/▼` or `B/S`; numbers monospaced; heatmap drawn with `ImDrawList` rects colored by log-scaled quantity, trade dots overlaid; footprint as per-candle bucket cells `bid x ask`; ladder grouping by selected increment.
- [ ] **Step 5: Headless smoke** — extend Playwright (`apps/web/tests/panels.test.ts`): for each `PanelKind`, call a debug export `mc_debug_open_panel(kind)` and assert no console errors and `data-frame-count` keeps increasing for 2 s.
- [ ] **Step 6: Run all checks** → PASS.

---

### Task 10: Workspace, presets, persistence, export/import

**Files:**
- Create: `engine/include/market_classifier/ui/{workspace,workspace_codec}.hpp` + src, `ui/presets.cpp`
- Create: `engine/tests/test_workspace_codec.cpp`
- Create: `bridge/src/persistence.ts`, `bridge/tests/persistence.test.mjs`
- Modify: `apps/web/src/lib/terminal.ts`, `apps/web/src/routes/+page.svelte` (hidden file input for import, download for export — host UI only)
- Remove: M1 dummy read-model path from `browser_api.cpp` and its exports (keep M1 unit tests of protocol/ingress)

**Interfaces:**

```cpp
namespace market_classifier::ui {
inline constexpr std::uint32_t k_workspace_schema_version = 1;
inline constexpr std::size_t k_max_user_layouts = 32, k_max_panels_per_layout = 32, k_max_workspace_json_bytes = 256 * 1024;
struct PanelInstance { std::uint32_t id; PanelKind kind; PanelSettings settings; };
struct Layout { std::string name; bool builtin; std::vector<PanelInstance> panels; std::string imgui_ini; };
struct Workspace { std::vector<Layout> layouts; std::size_t active = 0; bool utc_time = false; };
Workspace default_workspace();                            // the 5 presets from packet §7, active = Overview
enum class CodecError : std::uint8_t { None, TooLarge, Malformed, UnsupportedVersion, OutOfBounds };
std::string encode_workspace(const Workspace &);          // {"schema":1,"layouts":[...],"active":0,"utcTime":false}
struct DecodedWorkspace { Workspace value; CodecError error; };
DecodedWorkspace decode_workspace(std::string_view json); // runs migrations up to current version
}
```

WASM exports: `mc_workspace_export() -> const char*` (JSON), `mc_workspace_import(ptr,len) -> int` (CodecError), `mc_workspace_reset()`, `mc_workspace_dirty() -> int`.

TS persistence (`persistence.ts`): IndexedDB db `market-classifier`, store `workspace`, keys `latest` and `last-good`. Save: when `mc_workspace_dirty`, debounce 1 s, write `latest`; after a successful decode on next load, copy it to `last-good`. Load order: `latest` → decode OK? use : `last-good` → OK? use + notify "Restored last good workspace" : defaults + notify "Workspace reset to defaults (saved data was unreadable)". Notifications render in the Svelte host as a dismissible banner.

- [ ] **Step 1: Failing C++ tests**: default workspace has 5 builtin layouts with the exact panel kinds from packet §7; encode→decode round-trips; oversize JSON → `TooLarge`; `schema: 99` → `UnsupportedVersion`; 33 layouts → `OutOfBounds`; unknown panel kind → `Malformed`; builtin layouts cannot be deleted or renamed (operation returns false); a v0 fixture (`fixtures/mvp/workspace-v0.json`, invented pre-release shape without `utcTime`) migrates to v1 with `utc_time=false`.
- [ ] **Step 2: Failing TS tests** with `fake-indexeddb`-free fake (a Map-backed stub implementing the tiny subset used): corrupt `latest` falls back to `last-good`; both corrupt → defaults with notice.
- [ ] **Step 3: Run** → FAIL. **Step 4: Implement**, including ImGui docking: each layout stores `ImGui::SaveIniSettingsToMemory()` in `imgui_ini`; switching layout calls `LoadIniSettingsFromMemory`. Presets ship with hand-authored ini strings built by arranging panels once in the app and exporting. Menu bar: Layouts (switch / save as / rename / delete / duplicate / reset), Panels (add any kind, including Diagnostics), View (UTC toggle), File (Export / Import).
- [ ] **Step 5: Playwright** `apps/web/tests/workspace.test.ts`: load → Overview active; open each preset → no console errors; save layout "Mine" → reload → "Mine" exists and is active; write garbage to IndexedDB `latest` → reload → banner "Restored last good workspace"; export → reset → import → layout "Mine" is back.
- [ ] **Step 6: Run all checks** → PASS.
- [ ] **Step 7: Commit** `feat(ui): add workspace presets, persistence, export and import`.

---

### Task 11: CI and GitHub Pages deploy

**Files:**
- Create: `.github/workflows/ci.yml`, `.github/workflows/pages.yml`
- Modify: `apps/web/svelte.config.js` (`paths.base` from `process.env.BASE_PATH ?? ''`), `apps/web/src/lib/terminal.ts` (`WASM_JS_PATH` and `locateFile` prefixed with `base` from `$app/paths`)
- Modify: `README.md` (status → MVP, run/deploy instructions), `.gitignore` (ensure `apps/web/build/`, `apps/web/static/wasm/*.wasm|.js`, `bridge/.build/`, `apps/web/test-results/`, `*.log` are ignored; `git rm --cached` any tracked build output after asking the owner)

**Interfaces:** none (infrastructure).

- [ ] **Step 1: `ci.yml`** on push/PR: ubuntu-latest; pinned Node 22.9.0, pnpm 9.11.0 (corepack), CMake 3.29.6, Ninja 1.12.1, emsdk 3.1.67 (`mymindstorm/setup-emsdk` pinned by commit SHA, or clone emsdk at tag); steps = standard commands + clang-format check + clang-tidy on `engine/src`; Playwright with `npx playwright install --with-deps chromium`.
- [ ] **Step 2: `pages.yml`** on push to `main`: build WASM + web with `BASE_PATH=/<repo-name>`, `actions/upload-pages-artifact`, `actions/deploy-pages` (all actions pinned by SHA).
- [ ] **Step 3: Verify locally** `BASE_PATH=/market_classifier pnpm build && pnpm preview` serves app at `/market_classifier/` with WASM loading.
- [ ] **Step 4: Ask the owner** to enable Pages (Settings → Pages → Source: GitHub Actions) and push; confirm the deployed URL shows live data from both venues (acceptance #8). Record URL + date in README.
- [ ] **Step 5: Commit** `ci: add checks and GitHub Pages deploy`.

---

### Task 12: Resilience and bound verification

**Files:**
- Create: `engine/tests/test_flood.cpp`, `apps/web/tests/resilience.test.ts`

- [ ] **Step 1: Flood test (native):** submit 1,000,000 synthetic trades and 100,000 book deltas through `Engine` across simulated 2 hours with `FakeClock`; after each simulated minute assert every store size ≤ its constant (tape 5000, heatmap 14400, candles 2000, series 1440, ingress queue ≤ `k_max_queued_batches`).
- [ ] **Step 2: Playwright resilience:** use `page.routeWebSocket` (Playwright ≥1.48 — if the pinned 1.47.2 lacks it, STOP and ask to bump; alternative is a debug export `mc_debug_socket_event`) to close the Hyperliquid socket → Diagnostics shows Hyperliquid `Reconnecting`, Binance `Live`; stop delivering Binance messages for 6 s → Binance shows `Stale` with age ≥ 5 s.
- [ ] **Step 3: Run all checks** → PASS. **Step 4: Commit** `test: verify bounds under flood and venue isolation`.

---

### Task 13: Performance report

**Files:**
- Modify: `apps/web/tools/measure-runtime.mjs` (iterate presets via debug export, 30 s each)
- Create: `docs/performance/mvp.md`

- [ ] **Step 1:** Extend the runtime measurer to report mean/p95/p99 frame time and fps per preset, WASM bytes, gzip bundle size, and `performance.memory.usedJSHeapSize` + WASM heap every minute for 60 minutes (soak mode flag `--soak`).
- [ ] **Step 2:** Run against live data in Chrome on the owner's machine; record machine specs, browser version, date, and results in `docs/performance/mvp.md`. Flag any preset under 60 fps or any monotonically rising heap as a follow-up (non-blocking per packet).
- [ ] **Step 3: Commit** `docs(perf): record MVP runtime measurements`.

---

### Task 14: Milestone acceptance handoff

- [ ] Run all checks from a clean clone (`git clone` to a temp dir, follow README).
- [ ] Walk packet §12 criteria 1–9; for each, cite the test or document proving it.
- [ ] Update packet status to `Accepted locally <date>`; write `docs/reviews/mvp-verification.md` with the CLAUDE.md handoff block.
- [ ] Commit `docs(mvp): record acceptance and handoff`.

---

## Self-review notes

- Coverage: packet §4 → Tasks 2,4,8; §5 → 9; §6 → 7,12; §7–8 → 10; §9 → 6,8,12; §10 → 1,3; §11 → 11; §12 → 5,7,10,11,12,13,14; §13 slices map 1:1 (slice 7 → Task 9, slice 8 → Task 10).
- Known open decision flagged in Task 4: how Binance `pu` reaches book sync without changing the shared domain contract. Default chosen; reviewer may escalate.
- Owner gates: Task 1 (ADR-0005), Task 11 (untracking build outputs, enabling Pages), Task 12 (possible Playwright bump).
