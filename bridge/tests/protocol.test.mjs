import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { decodeTradeBatch, encodeTradeBatch } from "../.build/protocol.js";

const sample = {
  venue: 0,
  symbol: "BTCUSDT",
  sourceTimeMs: 1700000000000n,
  receiveTimeMs: 1700000000007n,
  localSequence: 42n,
  quality: 0,
  sourceId: "trade-123",
  aggressorSide: 0,
  price: { mantissa: 4200025n, scale: 2 },
  quantity: { mantissa: 125n, scale: 3 },
  usdNotional: { mantissa: 525003125n, scale: 5 },
};

test("TypeScript codec matches the shared V1 golden fixture", async () => {
  const expected = (
    await readFile(
      new URL("../../fixtures/m1/trade-batch-v1.hex", import.meta.url),
      "utf8",
    )
  ).trim();
  assert.equal(
    Buffer.from(encodeTradeBatch([sample])).toString("hex"),
    expected,
  );
  const result = decodeTradeBatch(Buffer.from(expected, "hex"));
  assert.equal(result.ok, true);
  assert.deepEqual(result.ok && result.trades, [sample]);
});

test("TypeScript decoder fails closed on protocol and semantic corruption", () => {
  const valid = encodeTradeBatch([sample]);
  for (const [offset, value, error] of [
    [4, 2, "unsupported-version"],
    [6, 255, "unknown-kind"],
    [56, 19, "invalid-decimal"],
  ]) {
    const changed = valid.slice();
    changed[offset] = value;
    assert.deepEqual(decodeTradeBatch(changed), { ok: false, error });
  }
  assert.deepEqual(decodeTradeBatch(valid.subarray(0, 10)), {
    ok: false,
    error: "truncated",
  });
  assert.deepEqual(decodeTradeBatch(new Uint8Array(65537)), {
    ok: false,
    error: "oversized",
  });
});
