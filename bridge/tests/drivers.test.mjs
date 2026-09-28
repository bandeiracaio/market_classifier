import assert from "node:assert/strict";
import test from "node:test";
import { createBinanceDriver, BINANCE_MARKET_WS, BINANCE_PUBLIC_WS, DEPTH_URL, OI_POLL_MS } from "../.build/venues/binance.js";
import { createHyperliquidDriver, HL_WS, PING_MS } from "../.build/venues/hyperliquid.js";
import { MAX_FRAMES_PER_BATCH, MAX_RAW_FRAME_BYTES } from "../.build/raw-frame.js";

class FakeSocket {
  static instances = [];
  constructor(url) {
    this.url = url;
    this.sent = [];
    this.closed = false;
    FakeSocket.instances.push(this);
  }
  send(text) {
    this.sent.push(text);
  }
  close() {
    this.closed = true;
  }
  // test helpers
  open() {
    this.onopen?.({});
  }
  message(data) {
    this.onmessage?.({ data });
  }
  drop() {
    this.onclose?.({});
  }
}

function fakeFetch(routes) {
  const calls = [];
  const fn = async (url, init) => {
    calls.push({ url, body: init?.body });
    for (const [match, body] of routes) {
      if (url.includes(match) || (init?.body ?? "").includes(match)) {
        if (body instanceof Error) throw body;
        return { ok: true, status: 200, text: async () => (typeof body === "string" ? body : JSON.stringify(body)) };
      }
    }
    return { ok: false, status: 404, text: async () => "" };
  };
  fn.calls = calls;
  return fn;
}

function makeSink(overrides = {}) {
  const sink = {
    batches: [],
    events: [],
    failed: [],
    reconnect: false,
    snapshot: false,
    submitRaw(bytes) {
      this.batches.push(bytes);
      return 0;
    },
    socketEvent(venue, kind) {
      this.events.push([venue, kind]);
    },
    shouldReconnect() {
      return this.reconnect;
    },
    requestSnapshot() {
      const s = this.snapshot;
      this.snapshot = false;
      return s;
    },
    metadataFailed(venue) {
      this.failed.push(venue);
    },
    drops: [],
    framesDropped(venue, count) {
      this.drops.push([venue, count]);
    },
    ...overrides,
  };
  return sink;
}

const frameCount = (bytes) => bytes[12] | (bytes[13] << 8);
const tick = () => new Promise((r) => setImmediate(r));
async function settle() {
  for (let i = 0; i < 10; i++) await tick();
}

const exchangeInfo = { symbols: [{ symbol: "ETHUSDT" }, { symbol: "BTCUSDT", filters: [] }] };
const binanceRoutes = () => [
  ["exchangeInfo", exchangeInfo],
  ["klines", "[]"],
  ["depth", '{"lastUpdateId":1,"bids":[],"asks":[]}'],
  ["openInterest", '{"symbol":"BTCUSDT","openInterest":"1","time":1}'],
];

test("binance opens both routed sockets with exact URLs after metadata", async () => {
  FakeSocket.instances = [];
  const sink = makeSink();
  const fetch = fakeFetch(binanceRoutes());
  const driver = createBinanceDriver(sink, { WebSocket: FakeSocket, fetch, now: () => 1000 });
  driver.start();
  await settle();
  assert.deepEqual(
    FakeSocket.instances.map((s) => s.url),
    [BINANCE_PUBLIC_WS, BINANCE_MARKET_WS],
  );
  assert.equal(
    BINANCE_PUBLIC_WS,
    "wss://fstream.binance.com/public/stream?streams=btcusdt@depth@100ms/btcusdt@bookTicker",
  );
  assert.equal(
    BINANCE_MARKET_WS,
    "wss://fstream.binance.com/market/stream?streams=btcusdt@aggTrade/btcusdt@markPrice@1s/btcusdt@ticker/btcusdt@kline_1m/btcusdt@kline_5m/btcusdt@kline_15m/btcusdt@kline_1h/btcusdt@kline_4h/btcusdt@kline_1d/btcusdt@forceOrder",
  );
  // exchangeInfo trimmed to BTCUSDT + 6 kline preloads buffered.
  assert.equal(fetch.calls.filter((c) => c.url.includes("klines")).length, 6);
  FakeSocket.instances[0].open();
  assert.deepEqual(sink.events, []); // venue opens only when both routes are up
  FakeSocket.instances[1].open();
  assert.deepEqual(sink.events, [[0, 0]]);
  // REST responses are submitted immediately: exchangeInfo first, then 6 kline preloads.
  assert.equal(sink.batches.length, 7);
  const payload = new TextDecoder().decode(sink.batches[0]);
  assert.ok(payload.includes('"BTCUSDT"'));
  assert.ok(!payload.includes("ETHUSDT"));
});

test("100 messages then pump submits exactly one batch of at most 64 frames", async () => {
  FakeSocket.instances = [];
  const sink = makeSink();
  const driver = createBinanceDriver(sink, { WebSocket: FakeSocket, fetch: fakeFetch(binanceRoutes()), now: () => 1 });
  driver.start();
  await settle();
  FakeSocket.instances.forEach((s) => s.open());
  driver.pump(0);
  sink.batches = [];
  for (let i = 0; i < 100; i++) FakeSocket.instances[1].message(`{"i":${i}}`);
  driver.pump(1);
  assert.equal(sink.batches.length, 1);
  assert.equal(frameCount(sink.batches[0]), MAX_FRAMES_PER_BATCH);
  assert.equal(driver.stats().framesDropped, 100 - MAX_FRAMES_PER_BATCH);
  // Evictions are reported to the engine so views show a gap instead of silent loss.
  assert.deepEqual(sink.drops, [[0, 100 - MAX_FRAMES_PER_BATCH]]);
  // Newest frames survive.
  assert.ok(new TextDecoder().decode(sink.batches[0]).includes('{"i":99}'));
});

test("oversize frame is dropped and counted", async () => {
  FakeSocket.instances = [];
  const sink = makeSink();
  const driver = createHyperliquidDriver(sink, {
    WebSocket: FakeSocket,
    fetch: fakeFetch([["metaAndAssetCtxs", '[{"universe":[]},[]]'], ["candleSnapshot", "[]"]]),
    now: () => 1,
  });
  driver.start();
  await settle();
  FakeSocket.instances[0].open();
  FakeSocket.instances[0].message("x".repeat(MAX_RAW_FRAME_BYTES + 1));
  assert.equal(driver.stats().oversizeDropped, 1);
  driver.pump(0);
  assert.deepEqual(sink.drops, [[1, 1]]);
});

test("hyperliquid subscribes to every BTC channel with exact JSON and pings", async () => {
  FakeSocket.instances = [];
  const sink = makeSink();
  let now = 0;
  const meta = [{ universe: [{ name: "ETH" }, { name: "BTC", szDecimals: 5 }] }, [{ markPx: "1" }, { markPx: "2" }]];
  const fetch = fakeFetch([["metaAndAssetCtxs", meta], ["candleSnapshot", "[]"]]);
  const driver = createHyperliquidDriver(sink, { WebSocket: FakeSocket, fetch, now: () => now });
  driver.start();
  await settle();
  assert.equal(FakeSocket.instances[0].url, HL_WS);
  FakeSocket.instances[0].open();
  assert.deepEqual(FakeSocket.instances[0].sent, [
    '{"method":"subscribe","subscription":{"type":"trades","coin":"BTC"}}',
    '{"method":"subscribe","subscription":{"type":"l2Book","coin":"BTC"}}',
    '{"method":"subscribe","subscription":{"type":"bbo","coin":"BTC"}}',
    '{"method":"subscribe","subscription":{"type":"activeAssetCtx","coin":"BTC"}}',
    '{"method":"subscribe","subscription":{"type":"candle","coin":"BTC","interval":"1m"}}',
    '{"method":"subscribe","subscription":{"type":"candle","coin":"BTC","interval":"5m"}}',
    '{"method":"subscribe","subscription":{"type":"candle","coin":"BTC","interval":"15m"}}',
    '{"method":"subscribe","subscription":{"type":"candle","coin":"BTC","interval":"1h"}}',
    '{"method":"subscribe","subscription":{"type":"candle","coin":"BTC","interval":"4h"}}',
    '{"method":"subscribe","subscription":{"type":"candle","coin":"BTC","interval":"1d"}}',
  ]);
  assert.equal(fetch.calls.filter((c) => c.body?.includes("candleSnapshot")).length, 6);
  now = PING_MS - 1;
  driver.pump(now);
  assert.equal(FakeSocket.instances[0].sent.length, 10);
  now = PING_MS;
  driver.pump(now);
  assert.equal(FakeSocket.instances[0].sent.at(-1), '{"method":"ping"}');
  // meta trimmed to BTC with its paired ctx.
  const text = new TextDecoder().decode(sink.batches[0]);
  assert.ok(text.includes('[{"universe":[{"name":"BTC","szDecimals":5}]},[{"markPx":"2"}]]'));
});

test("close reports once and reconnects only when the engine allows, resubscribing", async () => {
  FakeSocket.instances = [];
  const sink = makeSink();
  const driver = createHyperliquidDriver(sink, {
    WebSocket: FakeSocket,
    fetch: fakeFetch([["metaAndAssetCtxs", '[{"universe":[]},[]]'], ["candleSnapshot", "[]"]]),
    now: () => 1,
  });
  driver.start();
  await settle();
  FakeSocket.instances[0].open();
  FakeSocket.instances[0].drop();
  FakeSocket.instances[0].onerror?.({});
  assert.deepEqual(sink.events, [[1, 0], [1, 1]]);
  driver.pump(1);
  assert.equal(FakeSocket.instances.length, 1);
  sink.reconnect = true;
  driver.pump(2);
  assert.equal(FakeSocket.instances.length, 2);
  FakeSocket.instances[1].open();
  assert.equal(FakeSocket.instances[1].sent.length, 10);
});

test("binance drop on either route closes both and reconnects both", async () => {
  FakeSocket.instances = [];
  const sink = makeSink();
  const driver = createBinanceDriver(sink, { WebSocket: FakeSocket, fetch: fakeFetch(binanceRoutes()), now: () => 1 });
  driver.start();
  await settle();
  FakeSocket.instances.forEach((s) => s.open());
  FakeSocket.instances[0].drop();
  assert.ok(FakeSocket.instances[1].closed);
  assert.deepEqual(sink.events, [[0, 0], [0, 1]]);
  sink.reconnect = true;
  driver.pump(1);
  assert.equal(FakeSocket.instances.length, 4);
});

test("requestSnapshot triggers one depth fetch and OI polls at cadence", async () => {
  FakeSocket.instances = [];
  const sink = makeSink();
  const fetch = fakeFetch(binanceRoutes());
  const driver = createBinanceDriver(sink, { WebSocket: FakeSocket, fetch, now: () => 1 });
  driver.start();
  await settle();
  FakeSocket.instances.forEach((s) => s.open());
  sink.snapshot = true;
  driver.pump(0);
  driver.pump(1);
  assert.equal(fetch.calls.filter((c) => c.url === DEPTH_URL).length, 1);
  const oi = () => fetch.calls.filter((c) => c.url.includes("openInterest")).length;
  assert.equal(oi(), 1);
  driver.pump(OI_POLL_MS - 1);
  assert.equal(oi(), 1);
  driver.pump(OI_POLL_MS);
  assert.equal(oi(), 2);
});

test("metadata failure is reported and retried when the engine allows", async () => {
  FakeSocket.instances = [];
  const sink = makeSink();
  const fetch = fakeFetch([["exchangeInfo", new Error("offline")]]);
  const driver = createBinanceDriver(sink, { WebSocket: FakeSocket, fetch, now: () => 1 });
  driver.start();
  await settle();
  assert.deepEqual(sink.failed, [0]);
  assert.equal(FakeSocket.instances.length, 0);
  assert.equal(driver.stats().restFailures, 1);
  sink.reconnect = true;
  driver.pump(0);
  await settle();
  assert.equal(fetch.calls.filter((c) => c.url.includes("exchangeInfo")).length, 2);
});

test("REST responses bypass the frame buffer so large preloads are never evicted", async () => {
  FakeSocket.instances = [];
  const sink = makeSink();
  const big = "[" + '[1,"1","1","1","1","1",2,"1",1,"1","1","0"],'.repeat(6000) + "[]]"; // ~300 KB
  const fetch = fakeFetch([["exchangeInfo", exchangeInfo], ["klines", big]]);
  const driver = createBinanceDriver(sink, { WebSocket: FakeSocket, fetch, now: () => 1 });
  driver.start();
  await settle();
  // exchangeInfo + 6 kline payloads, each submitted as its own batch before any pump.
  assert.equal(sink.batches.length, 7);
  assert.ok(sink.batches.every((b) => frameCount(b) === 1));
  assert.deepEqual(sink.drops, []);
});
