export const PROTOCOL_VERSION = 1 as const;
export const HEADER_BYTES = 16;
export const MAX_MESSAGE_BYTES = 64 * 1024;
export const MAX_EVENTS_PER_BATCH = 256;
export const MAX_SYMBOL_BYTES = 64;
export const MAX_SOURCE_ID_BYTES = 128;

export const enum MessageKind {
  TradeBatch = 1,
}
export const enum Venue {
  BinanceUsdM = 0,
  Hyperliquid = 1,
}
export const enum DataQuality {
  Live,
  Delayed,
  Stale,
  Reconnecting,
  GapDetected,
  Partial,
  Unsupported,
  Failed,
}
export const enum AggressorSide {
  Buy,
  Sell,
  Unknown,
}

export interface WireDecimal {
  mantissa: bigint;
  scale: number;
}
export interface WireTrade {
  venue: Venue;
  symbol: string;
  sourceTimeMs: bigint;
  receiveTimeMs: bigint;
  localSequence: bigint;
  quality: DataQuality;
  sourceId: string;
  aggressorSide: AggressorSide;
  price: WireDecimal;
  quantity: WireDecimal;
  usdNotional: WireDecimal;
}

export type DecodeError =
  | "truncated"
  | "oversized"
  | "invalid-magic"
  | "unsupported-version"
  | "unknown-kind"
  | "invalid-flags"
  | "invalid-length"
  | "invalid-count"
  | "invalid-enum"
  | "invalid-decimal"
  | "invalid-instrument"
  | "invalid-metadata"
  | "invalid-source-id";
export type DecodeResult =
  | { ok: true; trades: WireTrade[] }
  | { ok: false; error: DecodeError };

const MAGIC = [0x4d, 0x43, 0x42, 0x31] as const;
const RECORD_FIXED_BYTES = 59;
const encoder = new TextEncoder();
const decoder = new TextDecoder("ascii", { fatal: true });

function validAscii(bytes: Uint8Array): boolean {
  return bytes.every((byte) => byte >= 0x21 && byte <= 0x7e);
}

export function encodeTradeBatch(trades: readonly WireTrade[]): Uint8Array {
  if (trades.length > MAX_EVENTS_PER_BATCH) return new Uint8Array();
  const encoded = trades.map((trade) => ({
    trade,
    symbol: encoder.encode(trade.symbol),
    sourceId: encoder.encode(trade.sourceId),
  }));
  let total = HEADER_BYTES;
  for (const item of encoded) {
    if (
      item.symbol.length === 0 ||
      !validAscii(item.symbol) ||
      item.symbol.length > MAX_SYMBOL_BYTES ||
      !validAscii(item.sourceId) ||
      item.sourceId.length > MAX_SOURCE_ID_BYTES ||
      item.trade.venue > Venue.Hyperliquid ||
      item.trade.quality > DataQuality.Failed ||
      item.trade.aggressorSide > AggressorSide.Unknown ||
      item.trade.sourceTimeMs < 0 ||
      item.trade.receiveTimeMs < 0 ||
      item.trade.localSequence === 0n ||
      item.trade.price.scale > 18 ||
      item.trade.quantity.scale > 18 ||
      item.trade.usdNotional.scale > 18
    )
      return new Uint8Array();
    total += RECORD_FIXED_BYTES + item.symbol.length + item.sourceId.length;
  }
  if (total > MAX_MESSAGE_BYTES) return new Uint8Array();
  const bytes = new Uint8Array(total);
  bytes.set(MAGIC);
  const view = new DataView(bytes.buffer);
  view.setUint16(4, PROTOCOL_VERSION, true);
  view.setUint8(6, MessageKind.TradeBatch);
  view.setUint32(8, total - HEADER_BYTES, true);
  view.setUint16(12, trades.length, true);
  let offset = HEADER_BYTES;
  for (const { trade, symbol, sourceId } of encoded) {
    view.setUint16(
      offset,
      RECORD_FIXED_BYTES + symbol.length + sourceId.length,
      true,
    );
    offset += 2;
    view.setUint8(offset++, trade.venue);
    view.setUint8(offset++, trade.quality);
    view.setUint8(offset++, trade.aggressorSide);
    view.setUint8(offset++, symbol.length);
    view.setUint8(offset++, sourceId.length);
    view.setUint8(offset++, 0);
    view.setBigInt64(offset, trade.sourceTimeMs, true);
    offset += 8;
    view.setBigInt64(offset, trade.receiveTimeMs, true);
    offset += 8;
    view.setBigUint64(offset, trade.localSequence, true);
    offset += 8;
    view.setBigInt64(offset, trade.price.mantissa, true);
    offset += 8;
    view.setUint8(offset++, trade.price.scale);
    view.setBigInt64(offset, trade.quantity.mantissa, true);
    offset += 8;
    view.setUint8(offset++, trade.quantity.scale);
    view.setBigInt64(offset, trade.usdNotional.mantissa, true);
    offset += 8;
    view.setUint8(offset++, trade.usdNotional.scale);
    bytes.set(symbol, offset);
    offset += symbol.length;
    bytes.set(sourceId, offset);
    offset += sourceId.length;
  }
  return bytes;
}

export function decodeTradeBatch(bytes: Uint8Array): DecodeResult {
  if (bytes.length < HEADER_BYTES) return { ok: false, error: "truncated" };
  if (bytes.length > MAX_MESSAGE_BYTES)
    return { ok: false, error: "oversized" };
  if (!MAGIC.every((byte, index) => bytes[index] === byte))
    return { ok: false, error: "invalid-magic" };
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint16(4, true) !== PROTOCOL_VERSION)
    return { ok: false, error: "unsupported-version" };
  if (view.getUint8(6) !== MessageKind.TradeBatch)
    return { ok: false, error: "unknown-kind" };
  if (view.getUint8(7) !== 0 || view.getUint16(14, true) !== 0)
    return { ok: false, error: "invalid-flags" };
  if (view.getUint32(8, true) !== bytes.length - HEADER_BYTES)
    return { ok: false, error: "invalid-length" };
  const count = view.getUint16(12, true);
  if (count > MAX_EVENTS_PER_BATCH)
    return { ok: false, error: "invalid-count" };
  const trades: WireTrade[] = [];
  let offset = HEADER_BYTES;
  for (let index = 0; index < count; index++) {
    if (bytes.length - offset < RECORD_FIXED_BYTES)
      return { ok: false, error: "truncated" };
    const start = offset;
    const recordBytes = view.getUint16(offset, true);
    offset += 2;
    const venue = view.getUint8(offset++);
    const quality = view.getUint8(offset++);
    const side = view.getUint8(offset++);
    const symbolLength = view.getUint8(offset++);
    const sourceIdLength = view.getUint8(offset++);
    const reserved = view.getUint8(offset++);
    const sourceTimeMs = view.getBigInt64(offset, true);
    offset += 8;
    const receiveTimeMs = view.getBigInt64(offset, true);
    offset += 8;
    const localSequence = view.getBigUint64(offset, true);
    offset += 8;
    const price = {
      mantissa: view.getBigInt64(offset, true),
      scale: view.getUint8(offset + 8),
    };
    offset += 9;
    const quantity = {
      mantissa: view.getBigInt64(offset, true),
      scale: view.getUint8(offset + 8),
    };
    offset += 9;
    const usdNotional = {
      mantissa: view.getBigInt64(offset, true),
      scale: view.getUint8(offset + 8),
    };
    offset += 9;
    if (
      reserved !== 0 ||
      recordBytes !== RECORD_FIXED_BYTES + symbolLength + sourceIdLength ||
      bytes.length - start < recordBytes
    )
      return { ok: false, error: "invalid-length" };
    if (
      venue > Venue.Hyperliquid ||
      quality > DataQuality.Failed ||
      side > AggressorSide.Unknown
    )
      return { ok: false, error: "invalid-enum" };
    if (price.scale > 18 || quantity.scale > 18 || usdNotional.scale > 18)
      return { ok: false, error: "invalid-decimal" };
    if (symbolLength === 0 || symbolLength > MAX_SYMBOL_BYTES)
      return { ok: false, error: "invalid-instrument" };
    if (sourceIdLength === 0 || sourceIdLength > MAX_SOURCE_ID_BYTES)
      return { ok: false, error: "invalid-source-id" };
    if (sourceTimeMs < 0 || receiveTimeMs < 0 || localSequence === 0n)
      return { ok: false, error: "invalid-metadata" };
    const symbolBytes = bytes.subarray(offset, offset + symbolLength);
    offset += symbolLength;
    const sourceIdBytes = bytes.subarray(offset, offset + sourceIdLength);
    offset += sourceIdLength;
    if (!validAscii(symbolBytes))
      return { ok: false, error: "invalid-instrument" };
    if (!validAscii(sourceIdBytes))
      return { ok: false, error: "invalid-source-id" };
    trades.push({
      venue,
      quality,
      aggressorSide: side,
      sourceTimeMs,
      receiveTimeMs,
      localSequence,
      price,
      quantity,
      usdNotional,
      symbol: decoder.decode(symbolBytes),
      sourceId: decoder.decode(sourceIdBytes),
    });
  }
  return offset === bytes.length
    ? { ok: true, trades }
    : { ok: false, error: "invalid-length" };
}
