import assert from "node:assert/strict";
import { readFile, writeFile } from "node:fs/promises";
import test from "node:test";
import {
  MAX_FRAMES_PER_BATCH,
  MAX_RAW_FRAME_BYTES,
  encodeRawFrames,
} from "../.build/raw-frame.js";

const FIXTURE = new URL("../../fixtures/mvp/raw-frame-batch-v1.hex", import.meta.url);
const sample = [
  { tag: 3, receiveTimeMs: 1700000000000n, payload: '{"stream":"btcusdt@aggTrade"}' },
  { tag: 33, receiveTimeMs: 1700000000001n, payload: '{"channel":"pong"}' },
];

// Test-only decoder: independent reading of the documented wire format.
function decode(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  assert.deepEqual([...bytes.subarray(0, 4)], [0x4d, 0x43, 0x42, 0x31]);
  assert.equal(view.getUint16(4, true), 1);
  assert.equal(view.getUint8(6), 2);
  assert.equal(view.getUint32(8, true), bytes.length - 16);
  const count = view.getUint16(12, true);
  const frames = [];
  let offset = 16;
  for (let i = 0; i < count; i++) {
    const tag = view.getUint8(offset);
    const receiveTimeMs = view.getBigInt64(offset + 1, true);
    const length = view.getUint32(offset + 9, true);
    const payload = new TextDecoder().decode(bytes.subarray(offset + 13, offset + 13 + length));
    frames.push({ tag, receiveTimeMs, payload });
    offset += 13 + length;
  }
  assert.equal(offset, bytes.length);
  return frames;
}

test("raw frame batch round-trips and writes the shared golden fixture", async () => {
  const bytes = encodeRawFrames(sample);
  assert.deepEqual(decode(bytes), sample);
  const hex = Buffer.from(bytes).toString("hex");
  let existing = null;
  try {
    existing = (await readFile(FIXTURE, "utf8")).trim();
  } catch {
    await writeFile(FIXTURE, hex + "\n");
  }
  if (existing !== null) assert.equal(hex, existing);
});

test("encoder refuses every bound violation", () => {
  assert.throws(() => encodeRawFrames([]), RangeError);
  assert.throws(
    () => encodeRawFrames(Array.from({ length: MAX_FRAMES_PER_BATCH + 1 }, () => sample[0])),
    RangeError,
  );
  assert.throws(
    () => encodeRawFrames([{ ...sample[0], payload: "x".repeat(MAX_RAW_FRAME_BYTES + 1) }]),
    RangeError,
  );
  assert.throws(() => encodeRawFrames([{ ...sample[0], payload: "" }]), RangeError);
  assert.throws(() => encodeRawFrames([{ ...sample[0], tag: 0xee }]), RangeError);
  // Three max-size frames exceed the 1 MiB batch payload cap.
  const big = { ...sample[0], payload: "x".repeat(MAX_RAW_FRAME_BYTES) };
  assert.throws(() => encodeRawFrames([big, big, big]), RangeError);
});
