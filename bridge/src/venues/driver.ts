// Shared driver plumbing: the engine sink contract and a bounded frame buffer.
// Drivers never parse venue payloads; C++ adapters validate everything (plan Task 8).
import {
  MAX_FRAMES_PER_BATCH,
  MAX_RAW_BATCH_PAYLOAD_BYTES,
  MAX_RAW_FRAME_BYTES,
  StreamTag,
  encodeRawFrames,
  payloadBytes,
  type RawFrame,
} from "../raw-frame.js";

/** 0 = Binance USD-M, 1 = Hyperliquid (matches domain::Venue). */
export type VenueId = 0 | 1;
export const BINANCE: VenueId = 0;
export const HYPERLIQUID: VenueId = 1;

/** 0 open, 1 close, 2 error (matches runtime::SocketEvent). */
export type SocketEventKind = 0 | 1 | 2;

/** Implemented by terminal.ts over the WASM exports. */
export interface EngineSink {
  submitRaw(bytes: Uint8Array): number;
  socketEvent(venue: VenueId, kind: SocketEventKind): void;
  shouldReconnect(venue: VenueId): boolean;
  requestSnapshot(venue: VenueId): boolean;
  /** Startup metadata could not be fetched or validated: venue shows Failed with retry. */
  metadataFailed(venue: VenueId): void;
  /** Frames evicted or rejected before submission: the engine marks a gap. */
  framesDropped(venue: VenueId, count: number): void;
}

export interface DriverDeps {
  WebSocket: typeof WebSocket;
  fetch: typeof fetch;
  /** Wall-clock milliseconds, used for frame receive times. */
  now: () => number;
}

export interface DriverStats {
  framesBuffered: number;
  framesDropped: number; // buffer overflow (oldest evicted)
  oversizeDropped: number;
  batchesSubmitted: number;
  restFailures: number;
}

/** Pump is called once per animation frame; it flushes at most one batch. */
export interface VenueDriver {
  start(): void;
  stop(): void;
  pump(nowMs: number): void;
  stats(): DriverStats;
}

/**
 * Bounded FIFO of raw frames (cap 64 frames / 1 MiB payload). Overflow evicts the oldest
 * frames: newest data wins, and the engine's sequence checks surface the loss as a gap.
 */
export class FrameBuffer {
  private frames: RawFrame[] = [];
  private sizes: number[] = [];
  private bytes = 0;
  private droppedSinceFlush = 0;

  constructor(private readonly venue: VenueId) {}
  readonly stats: DriverStats = {
    framesBuffered: 0,
    framesDropped: 0,
    oversizeDropped: 0,
    batchesSubmitted: 0,
    restFailures: 0,
  };

  push(tag: StreamTag, receiveTimeMs: number, payload: string): void {
    const size = payloadBytes(payload);
    if (size === 0 || size > MAX_RAW_FRAME_BYTES) {
      this.stats.oversizeDropped++;
      this.droppedSinceFlush++;
      return;
    }
    while (
      this.frames.length > 0 &&
      (this.frames.length >= MAX_FRAMES_PER_BATCH || this.bytes + size > MAX_RAW_BATCH_PAYLOAD_BYTES)
    ) {
      this.frames.shift();
      this.bytes -= this.sizes.shift() ?? 0;
      this.stats.framesDropped++;
      this.droppedSinceFlush++;
    }
    this.frames.push({ tag, receiveTimeMs: BigInt(Math.trunc(receiveTimeMs)), payload });
    this.sizes.push(size);
    this.bytes += size;
    this.stats.framesBuffered = this.frames.length;
  }

  /** Encodes and submits everything buffered as one batch; reports evictions first. */
  flush(sink: EngineSink): void {
    if (this.droppedSinceFlush > 0) {
      sink.framesDropped(this.venue, this.droppedSinceFlush);
      this.droppedSinceFlush = 0;
    }
    if (this.frames.length === 0) return;
    const bytes = encodeRawFrames(this.frames);
    this.frames = [];
    this.sizes = [];
    this.bytes = 0;
    this.stats.framesBuffered = 0;
    this.stats.batchesSubmitted++;
    sink.submitRaw(bytes);
  }

  /**
   * REST responses (metadata, preloads, snapshots, OI) are large and rare: submit each
   * immediately as its own batch so they never compete with stream frames for buffer space.
   */
  submitNow(sink: EngineSink, tag: StreamTag, receiveTimeMs: number, payload: string): void {
    const size = payloadBytes(payload);
    if (size === 0 || size > MAX_RAW_FRAME_BYTES) {
      this.stats.oversizeDropped++;
      this.droppedSinceFlush++;
      return;
    }
    this.stats.batchesSubmitted++;
    sink.submitRaw(encodeRawFrames([{ tag, receiveTimeMs: BigInt(Math.trunc(receiveTimeMs)), payload }]));
  }

  clear(): void {
    this.frames = [];
    this.sizes = [];
    this.bytes = 0;
    this.stats.framesBuffered = 0;
  }
}

/** Candle intervals preloaded and streamed on both venues (processors::k_candle_intervals_ms). */
export const CANDLE_INTERVALS = ["1m", "5m", "15m", "1h", "4h", "1d"] as const;
export const INTERVAL_MS: Record<(typeof CANDLE_INTERVALS)[number], number> = {
  "1m": 60_000,
  "5m": 300_000,
  "15m": 900_000,
  "1h": 3_600_000,
  "4h": 14_400_000,
  "1d": 86_400_000,
};
/** Bars preloaded per interval (packet §6: 1,000 REST-preloaded bars). */
export const PRELOAD_BARS = 1000;
