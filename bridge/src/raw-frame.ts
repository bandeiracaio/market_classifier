// RawFrameBatch encoder (ADR-0004 addendum). Mirrors
// engine/include/market_classifier/bridge/raw_frame.hpp; bounds must match.
import { HEADER_BYTES, PROTOCOL_VERSION } from "./protocol.js";

export const RAW_FRAME_BATCH_KIND = 2 as const;
export const MAX_RAW_FRAME_BYTES = 512 * 1024;
export const MAX_FRAMES_PER_BATCH = 64;
export const RAW_FRAME_FIXED_BYTES = 13; // u8 tag, i64 receive time, u32 length
export const MAX_RAW_BATCH_PAYLOAD_BYTES = 1024 * 1024;

export const enum StreamTag {
  BinanceExchangeInfo = 1,
  BinanceDepthSnapshot,
  BinanceWs,
  BinanceKlinesRest,
  BinanceOpenInterestRest,
  HyperliquidMeta = 32,
  HyperliquidWs,
  HyperliquidCandleSnapshot,
}

const VALID_TAGS = new Set<number>([1, 2, 3, 4, 5, 32, 33, 34]);

export interface RawFrame {
  tag: StreamTag;
  receiveTimeMs: bigint;
  payload: string;
}

const MAGIC = [0x4d, 0x43, 0x42, 0x31]; // "MCB1"
const encoder = new TextEncoder();

/** Encoded UTF-8 size of a payload, used by drivers to enforce bounds before buffering. */
export function payloadBytes(payload: string): number {
  return encoder.encode(payload).length;
}

/** Throws RangeError on any bound violation; never emits a batch the engine would reject. */
export function encodeRawFrames(frames: readonly RawFrame[]): Uint8Array {
  if (frames.length === 0 || frames.length > MAX_FRAMES_PER_BATCH) {
    throw new RangeError(`frame count ${frames.length} outside 1..${MAX_FRAMES_PER_BATCH}`);
  }
  const encoded = frames.map((frame) => {
    if (!VALID_TAGS.has(frame.tag)) throw new RangeError(`invalid stream tag ${frame.tag}`);
    const bytes = encoder.encode(frame.payload);
    if (bytes.length === 0 || bytes.length > MAX_RAW_FRAME_BYTES) {
      throw new RangeError(`frame payload ${bytes.length} bytes outside 1..${MAX_RAW_FRAME_BYTES}`);
    }
    return bytes;
  });
  const payloadTotal = encoded.reduce((sum, bytes) => sum + bytes.length, 0);
  if (payloadTotal > MAX_RAW_BATCH_PAYLOAD_BYTES) {
    throw new RangeError(`batch payload ${payloadTotal} bytes exceeds ${MAX_RAW_BATCH_PAYLOAD_BYTES}`);
  }
  const body = payloadTotal + frames.length * RAW_FRAME_FIXED_BYTES;
  const out = new Uint8Array(HEADER_BYTES + body);
  const view = new DataView(out.buffer);
  out.set(MAGIC, 0);
  view.setUint16(4, PROTOCOL_VERSION, true);
  view.setUint8(6, RAW_FRAME_BATCH_KIND);
  view.setUint8(7, 0);
  view.setUint32(8, body, true);
  view.setUint16(12, frames.length, true);
  view.setUint16(14, 0, true);
  let offset = HEADER_BYTES;
  frames.forEach((frame, index) => {
    const bytes = encoded[index];
    view.setUint8(offset, frame.tag);
    view.setBigInt64(offset + 1, frame.receiveTimeMs, true);
    view.setUint32(offset + 9, bytes.length, true);
    out.set(bytes, offset + RAW_FRAME_FIXED_BYTES);
    offset += RAW_FRAME_FIXED_BYTES + bytes.length;
  });
  return out;
}
