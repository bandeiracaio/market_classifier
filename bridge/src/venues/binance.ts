// Binance USD-M BTCUSDT driver. Endpoints and limits: docs/protocols/binance.md
// (verified 2026-09-27). Streams are split across two routed sockets:
// /public (depth, bookTicker) and /market (aggTrade, markPrice, ticker, kline, forceOrder).
import { StreamTag } from "../raw-frame.js";
import {
  BINANCE,
  CANDLE_INTERVALS,
  FrameBuffer,
  PRELOAD_BARS,
  type DriverDeps,
  type EngineSink,
  type VenueDriver,
} from "./driver.js";

export const BINANCE_REST = "https://fapi.binance.com";
export const BINANCE_PUBLIC_WS =
  "wss://fstream.binance.com/public/stream?streams=btcusdt@depth@100ms/btcusdt@bookTicker";
export const BINANCE_MARKET_WS =
  "wss://fstream.binance.com/market/stream?streams=" +
  [
    "btcusdt@aggTrade",
    "btcusdt@markPrice@1s",
    "btcusdt@ticker",
    ...CANDLE_INTERVALS.map((i) => `btcusdt@kline_${i}`),
    "btcusdt@forceOrder",
  ].join("/");
/** Open interest is REST-only; polled at this cadence and labeled as such. */
export const OI_POLL_MS = 10_000;
/** Binance drops connections at 24 h; reconnect proactively before that. */
export const MAX_CONNECTION_MS = 23 * 3_600_000 + 50 * 60_000;
export const DEPTH_URL = `${BINANCE_REST}/fapi/v1/depth?symbol=BTCUSDT&limit=1000`;

type Phase = "idle" | "starting" | "open" | "closed";

export function createBinanceDriver(sink: EngineSink, deps: DriverDeps): VenueDriver {
  const buffer = new FrameBuffer(BINANCE);
  let sockets: WebSocket[] = [];
  let phase: Phase = "idle";
  let openCount = 0;
  let connectedAt = 0;
  let lastOiPoll = -Infinity;
  let metadataLoaded = false;
  let loading = false; // metadata fetch in flight
  let stopped = false;
  let generation = 0; // ignores callbacks from sockets of a previous connection

  async function get(url: string, tag: StreamTag, transform?: (text: string) => string): Promise<boolean> {
    try {
      const response = await deps.fetch(url);
      if (!response.ok) throw new Error(String(response.status));
      const text = await response.text();
      buffer.submitNow(sink, tag, deps.now(), transform ? transform(text) : text);
      return true;
    } catch {
      buffer.stats.restFailures++;
      return false;
    }
  }

  // exchangeInfo is ~1.1 MB; only the BTCUSDT entry is forwarded (512 KiB frame bound).
  function trimExchangeInfo(text: string): string {
    const info = JSON.parse(text) as { symbols?: { symbol?: string }[] };
    return JSON.stringify({ ...info, symbols: (info.symbols ?? []).filter((s) => s.symbol === "BTCUSDT") });
  }

  async function loadMetadata(): Promise<void> {
    const ok = await get(`${BINANCE_REST}/fapi/v1/exchangeInfo`, StreamTag.BinanceExchangeInfo, trimExchangeInfo);
    if (!ok) {
      sink.metadataFailed(BINANCE);
      return;
    }
    metadataLoaded = true;
    await Promise.all(
      CANDLE_INTERVALS.map((i) =>
        get(
          `${BINANCE_REST}/fapi/v1/klines?symbol=BTCUSDT&interval=${i}&limit=${PRELOAD_BARS}`,
          StreamTag.BinanceKlinesRest,
        ),
      ),
    );
  }

  function closeSockets(): void {
    generation++;
    for (const ws of sockets) {
      ws.onopen = ws.onmessage = ws.onclose = ws.onerror = null;
      try {
        ws.close();
      } catch {
        /* already closed */
      }
    }
    sockets = [];
    openCount = 0;
  }

  function onDisconnect(): void {
    if (phase === "closed" || stopped) return;
    phase = "closed";
    closeSockets(); // one venue feed: if either route drops, both reconnect together
    sink.socketEvent(BINANCE, 1);
  }

  function openSockets(): void {
    closeSockets();
    const gen = generation;
    phase = "starting";
    for (const url of [BINANCE_PUBLIC_WS, BINANCE_MARKET_WS]) {
      const ws = new deps.WebSocket(url);
      ws.onopen = () => {
        if (gen !== generation) return;
        openCount++;
        if (openCount === 2) {
          phase = "open";
          connectedAt = deps.now();
          sink.socketEvent(BINANCE, 0);
        }
      };
      ws.onmessage = (event: MessageEvent) => {
        if (gen !== generation || typeof event.data !== "string") return;
        buffer.push(StreamTag.BinanceWs, deps.now(), event.data);
      };
      ws.onerror = () => {
        if (gen === generation) onDisconnect();
      };
      ws.onclose = () => {
        if (gen === generation) onDisconnect();
      };
      sockets.push(ws);
    }
  }

  function connect(): void {
    if (!metadataLoaded) {
      if (loading) return;
      loading = true;
      void loadMetadata().then(() => {
        loading = false;
        if (!stopped && metadataLoaded) openSockets();
      });
      phase = "starting";
      return;
    }
    openSockets();
  }

  return {
    start() {
      stopped = false;
      connect();
    },
    stop() {
      stopped = true;
      closeSockets();
      buffer.clear();
      phase = "idle";
    },
    pump(nowMs: number) {
      if (stopped) return;
      if ((phase === "closed" || (!metadataLoaded && phase === "starting")) && sink.shouldReconnect(BINANCE)) {
        connect();
      }
      if (phase === "open") {
        if (sink.requestSnapshot(BINANCE)) {
          void get(DEPTH_URL, StreamTag.BinanceDepthSnapshot);
        }
        if (nowMs - lastOiPoll >= OI_POLL_MS) {
          lastOiPoll = nowMs;
          void get(`${BINANCE_REST}/fapi/v1/openInterest?symbol=BTCUSDT`, StreamTag.BinanceOpenInterestRest);
        }
        if (deps.now() - connectedAt >= MAX_CONNECTION_MS) {
          onDisconnect(); // backoff is short; data resumes on fresh sockets
        }
      }
      buffer.flush(sink);
    },
    stats: () => buffer.stats,
  };
}
