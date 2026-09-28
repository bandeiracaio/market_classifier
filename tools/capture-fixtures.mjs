// One-shot fixture capture for the MVP (plan Task 2). Node >= 22, no dependencies.
// Captures the first N messages of every BTC stream on Binance USD-M and
// Hyperliquid (packet MVP_BTC_TERMINAL.md §4) into fixtures/mvp/.
//
// Endpoint routing verified 2026-09-27 (docs/protocols/cors-verification.md):
// Binance splits streams across /public (depth, bookTicker) and /market
// (aggTrade, markPrice, ticker, kline, forceOrder).
//
// Usage: node tools/capture-fixtures.mjs [--force-order-wait-s=600]
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', 'fixtures', 'mvp');
const MAX_FILE_BYTES = 256 * 1024;
// The 200-diff book-sync sequence is ~2 KB per diff; it gets its own larger cap.
const MAX_SEQUENCE_BYTES = 512 * 1024;
const N = 5;
const DEPTH_SEQUENCE_N = 200;
const FORCE_ORDER_WAIT_MS =
  Number((process.argv.find((a) => a.startsWith('--force-order-wait-s=')) ?? '=600').split('=')[1]) * 1000;

const BINANCE_REST = 'https://fapi.binance.com';
const BINANCE_WS = 'wss://fstream.binance.com';
const HL_INFO = 'https://api.hyperliquid.xyz/info';
const HL_WS = 'wss://api.hyperliquid.xyz/ws';

function write(rel, value) {
  const path = join(ROOT, rel);
  mkdirSync(dirname(path), { recursive: true });
  const text = typeof value === 'string' ? value : JSON.stringify(value, null, 1) + '\n';
  const cap = rel.endsWith('.jsonl') ? MAX_SEQUENCE_BYTES : MAX_FILE_BYTES;
  if (Buffer.byteLength(text) > cap) throw new Error(`${rel} exceeds ${cap} bytes`);
  writeFileSync(path, text);
  console.log(`wrote ${rel} (${Buffer.byteLength(text)} bytes)`);
}

async function getJson(url, init) {
  const r = await fetch(url, init);
  if (!r.ok) throw new Error(`${url} -> ${r.status}`);
  return r.json();
}

const hlPost = (body) =>
  getJson(HL_INFO, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });

/** Collects raw text frames from a socket until `done(frames)` returns true or timeout. */
function collect(url, { onOpen, done, timeoutMs }) {
  return new Promise((resolve) => {
    const frames = [];
    const ws = new WebSocket(url);
    const finish = () => {
      clearTimeout(timer);
      try {
        ws.close();
      } catch {}
      resolve(frames);
    };
    const timer = setTimeout(finish, timeoutMs);
    ws.onopen = () => onOpen?.(ws);
    ws.onmessage = (m) => {
      frames.push(String(m.data));
      if (done(frames)) finish();
    };
    ws.onerror = () => finish();
  });
}

const streamOf = (text) => JSON.parse(text).stream;
const count = (frames, pred) => frames.filter(pred).length;

async function captureBinance(meta) {
  const info = await getJson(`${BINANCE_REST}/fapi/v1/exchangeInfo`);
  write('binance/exchangeInfo.json', { ...info, symbols: info.symbols.filter((s) => s.symbol === 'BTCUSDT') });
  write('binance/klines_rest.json', await getJson(`${BINANCE_REST}/fapi/v1/klines?symbol=BTCUSDT&interval=1m&limit=5`));
  write('binance/openInterest.json', await getJson(`${BINANCE_REST}/fapi/v1/openInterest?symbol=BTCUSDT`));

  // /public: depth diffs + bookTicker. A depth snapshot is taken after some diffs
  // have arrived so the sequence straddles it (book-sync replay, Task 5).
  let snapshot = null;
  let snapshotRequested = false;
  const pub = await collect(`${BINANCE_WS}/public/stream?streams=btcusdt@depth@100ms/btcusdt@bookTicker`, {
    timeoutMs: 120_000,
    done: (f) => {
      const diffs = count(f, (t) => streamOf(t) === 'btcusdt@depth@100ms');
      if (diffs >= 20 && !snapshotRequested) {
        snapshotRequested = true;
        getJson(`${BINANCE_REST}/fapi/v1/depth?symbol=BTCUSDT&limit=1000`).then((s) => {
          snapshot = s;
        });
      }
      return diffs >= DEPTH_SEQUENCE_N && snapshot !== null && count(f, (t) => streamOf(t) === 'btcusdt@bookTicker') >= N;
    },
  });
  const depth = pub.filter((t) => streamOf(t) === 'btcusdt@depth@100ms').slice(0, DEPTH_SEQUENCE_N);
  write('binance/depth_diff.json', depth.slice(0, N).map((t) => JSON.parse(t)));
  write('binance/bookTicker.json', pub.filter((t) => streamOf(t) === 'btcusdt@bookTicker').slice(0, N).map((t) => JSON.parse(t)));
  // Snapshot trimmed to 50 levels per side to keep fixture small; replay only
  // needs levels near the touch plus lastUpdateId.
  const trimmed = { ...snapshot, bids: snapshot.bids.slice(0, 50), asks: snapshot.asks.slice(0, 50) };
  write('binance/depth_snapshot.json', trimmed);
  // depth_sequence.jsonl: line 1 = {"snapshot":...}, remaining lines = raw diff frames in arrival order.
  write('binance/depth_sequence.jsonl', [JSON.stringify({ snapshot: trimmed }), ...depth.map((t) => JSON.stringify(JSON.parse(t)))].join('\n') + '\n');

  const marketStreams = ['aggTrade', 'markPrice@1s', 'ticker', 'kline_1m'];
  const mkt = await collect(`${BINANCE_WS}/market/stream?streams=${marketStreams.map((s) => 'btcusdt@' + s).join('/')}`, {
    timeoutMs: 120_000,
    done: (f) => marketStreams.every((s) => count(f, (t) => streamOf(t) === 'btcusdt@' + s) >= N),
  });
  const fileFor = { aggTrade: 'aggTrade', 'markPrice@1s': 'markPrice', ticker: 'ticker', kline_1m: 'kline' };
  for (const s of marketStreams) {
    write(`binance/${fileFor[s]}.json`, mkt.filter((t) => streamOf(t) === 'btcusdt@' + s).slice(0, N).map((t) => JSON.parse(t)));
  }

  const liq = await collect(`${BINANCE_WS}/market/stream?streams=btcusdt@forceOrder`, {
    timeoutMs: FORCE_ORDER_WAIT_MS,
    done: (f) => f.length >= 1,
  });
  if (liq.length > 0) {
    write('binance/forceOrder.json', liq.slice(0, N).map((t) => JSON.parse(t)));
    meta.forceOrderSynthetic = false;
  } else {
    // Shape from the official docs example
    // (https://developers.binance.com/docs/derivatives/usds-margined-futures/websocket-market-streams/Liquidation-Order-Streams).
    const now = Date.now();
    write('binance/forceOrder.json', [
      {
        stream: 'btcusdt@forceOrder',
        data: {
          e: 'forceOrder',
          E: now,
          o: { s: 'BTCUSDT', S: 'SELL', o: 'LIMIT', f: 'IOC', q: '0.014', p: '84000.10', ap: '84010.20', X: 'FILLED', l: '0.014', z: '0.014', T: now },
        },
      },
    ]);
    meta.forceOrderSynthetic = true;
  }
}

async function captureHyperliquid() {
  const [metaInfo, ctxs] = await hlPost({ type: 'metaAndAssetCtxs' });
  const idx = metaInfo.universe.findIndex((u) => u.name === 'BTC');
  write('hyperliquid/metaAndAssetCtxs.json', [{ ...metaInfo, universe: [metaInfo.universe[idx]] }, [ctxs[idx]]]);
  const end = Date.now();
  write('hyperliquid/candleSnapshot.json', await hlPost({ type: 'candleSnapshot', req: { coin: 'BTC', interval: '1m', startTime: end - 5 * 60_000, endTime: end } }));

  const subs = [
    { type: 'trades', coin: 'BTC' },
    { type: 'l2Book', coin: 'BTC' },
    { type: 'bbo', coin: 'BTC' },
    { type: 'activeAssetCtx', coin: 'BTC' },
    { type: 'candle', coin: 'BTC', interval: '1m' },
  ];
  const channels = ['trades', 'l2Book', 'bbo', 'activeAssetCtx', 'candle'];
  const frames = await collect(HL_WS, {
    timeoutMs: 120_000,
    onOpen: (ws) => {
      for (const s of subs) ws.send(JSON.stringify({ method: 'subscribe', subscription: s }));
      ws.send(JSON.stringify({ method: 'ping' }));
    },
    done: (f) => channels.every((c) => count(f, (t) => JSON.parse(t).channel === c) >= N) && f.some((t) => JSON.parse(t).channel === 'pong'),
  });
  const by = (c) => frames.filter((t) => JSON.parse(t).channel === c).map((t) => JSON.parse(t));
  for (const c of channels) write(`hyperliquid/${c}.json`, by(c).slice(0, N));
  write('hyperliquid/subscription_ack.json', [...by('subscriptionResponse').slice(0, N), ...by('pong').slice(0, 1)]);
}

const meta = { capturedAt: new Date().toISOString() };
await captureHyperliquid();
await captureBinance(meta);
write('capture-meta.json', meta);
