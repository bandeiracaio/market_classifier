// Hyperliquid BTC driver. Endpoints and limits: docs/protocols/hyperliquid.md
// (verified 2026-09-27). One socket; subscriptions are re-sent after every reconnect.
import { StreamTag } from "../raw-frame.js";
import {
  CANDLE_INTERVALS,
  FrameBuffer,
  HYPERLIQUID,
  INTERVAL_MS,
  PRELOAD_BARS,
  type DriverDeps,
  type EngineSink,
  type VenueDriver,
} from "./driver.js";

export const HL_INFO = "https://api.hyperliquid.xyz/info";
export const HL_WS = "wss://api.hyperliquid.xyz/ws";
/** Server idles out after 60 s without a client message. */
export const PING_MS = 30_000;

export const HL_SUBSCRIPTIONS: readonly object[] = [
  { type: "trades", coin: "BTC" },
  { type: "l2Book", coin: "BTC" },
  { type: "bbo", coin: "BTC" },
  { type: "activeAssetCtx", coin: "BTC" },
  ...CANDLE_INTERVALS.map((interval) => ({ type: "candle", coin: "BTC", interval })),
];

type Phase = "idle" | "starting" | "open" | "closed";

export function createHyperliquidDriver(sink: EngineSink, deps: DriverDeps): VenueDriver {
  const buffer = new FrameBuffer(HYPERLIQUID);
  let socket: WebSocket | null = null;
  let phase: Phase = "idle";
  let lastPing = 0;
  let metadataLoaded = false;
  let loading = false; // metadata fetch in flight
  let stopped = false;
  let generation = 0;

  async function post(body: object, tag: StreamTag, transform?: (text: string) => string): Promise<boolean> {
    try {
      const response = await deps.fetch(HL_INFO, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
      });
      if (!response.ok) throw new Error(String(response.status));
      const text = await response.text();
      buffer.submitNow(sink, tag, deps.now(), transform ? transform(text) : text);
      return true;
    } catch {
      buffer.stats.restFailures++;
      return false;
    }
  }

  // Keep only BTC while preserving universe[i] <-> assetCtxs[i] pairing.
  function trimMeta(text: string): string {
    const [meta, ctxs] = JSON.parse(text) as [{ universe: { name: string }[] }, unknown[]];
    const index = meta.universe.findIndex((u) => u.name === "BTC");
    if (index < 0) return JSON.stringify([{ ...meta, universe: [] }, []]);
    return JSON.stringify([{ ...meta, universe: [meta.universe[index]] }, [ctxs[index]]]);
  }

  async function loadMetadata(): Promise<void> {
    const ok = await post({ type: "metaAndAssetCtxs" }, StreamTag.HyperliquidMeta, trimMeta);
    if (!ok) {
      sink.metadataFailed(HYPERLIQUID);
      return;
    }
    metadataLoaded = true;
    const end = deps.now();
    await Promise.all(
      CANDLE_INTERVALS.map((interval) =>
        post(
          {
            type: "candleSnapshot",
            req: { coin: "BTC", interval, startTime: end - PRELOAD_BARS * INTERVAL_MS[interval], endTime: end },
          },
          StreamTag.HyperliquidCandleSnapshot,
        ),
      ),
    );
  }

  function closeSocket(): void {
    generation++;
    if (socket) {
      socket.onopen = socket.onmessage = socket.onclose = socket.onerror = null;
      try {
        socket.close();
      } catch {
        /* already closed */
      }
    }
    socket = null;
  }

  function onDisconnect(): void {
    if (phase === "closed" || stopped) return;
    phase = "closed";
    closeSocket();
    sink.socketEvent(HYPERLIQUID, 1);
  }

  function openSocket(): void {
    closeSocket();
    const gen = generation;
    phase = "starting";
    const ws = new deps.WebSocket(HL_WS);
    ws.onopen = () => {
      if (gen !== generation) return;
      for (const subscription of HL_SUBSCRIPTIONS) {
        ws.send(JSON.stringify({ method: "subscribe", subscription }));
      }
      phase = "open";
      lastPing = deps.now();
      sink.socketEvent(HYPERLIQUID, 0);
    };
    ws.onmessage = (event: MessageEvent) => {
      if (gen !== generation || typeof event.data !== "string") return;
      buffer.push(StreamTag.HyperliquidWs, deps.now(), event.data);
    };
    ws.onerror = () => {
      if (gen === generation) onDisconnect();
    };
    ws.onclose = () => {
      if (gen === generation) onDisconnect();
    };
    socket = ws;
  }

  function connect(): void {
    if (!metadataLoaded) {
      phase = "starting";
      if (loading) return;
      loading = true;
      void loadMetadata().then(() => {
        loading = false;
        if (!stopped && metadataLoaded) openSocket();
      });
      return;
    }
    openSocket();
  }

  return {
    start() {
      stopped = false;
      connect();
    },
    stop() {
      stopped = true;
      closeSocket();
      buffer.clear();
      phase = "idle";
    },
    pump(_nowMs: number) {
      if (stopped) return;
      if ((phase === "closed" || (!metadataLoaded && phase === "starting")) && sink.shouldReconnect(HYPERLIQUID)) {
        connect();
      }
      if (phase === "open" && socket && deps.now() - lastPing >= PING_MS) {
        lastPing = deps.now();
        socket.send(JSON.stringify({ method: "ping" }));
      }
      buffer.flush(sink);
    },
    stats: () => buffer.stats,
  };
}
