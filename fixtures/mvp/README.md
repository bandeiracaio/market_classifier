# MVP venue fixtures

Captured by `node tools/capture-fixtures.mjs` on **2026-09-27T05:48:47Z** (see
`capture-meta.json`) from public, unauthenticated endpoints. Protocol notes:
`docs/protocols/binance.md`, `docs/protocols/hyperliquid.md`.

- `binance/*.json` — first 5 frames per stream (combined-stream envelope
  `{"stream","data"}` for WS), REST responses as returned. `exchangeInfo` is trimmed to
  the `BTCUSDT` symbol; `depth_snapshot` to 50 levels per side.
- `binance/depth_sequence.jsonl` — line 1 `{"snapshot":...}` (REST depth, 50 levels/side,
  taken after ~20 diffs arrived), then 200 consecutive `depth@100ms` frames in arrival
  order. Used by the book-sync replay test.
- `binance/forceOrder.json` — real capture (not synthetic).
- `hyperliquid/*.json` — first 5 messages per channel; `metaAndAssetCtxs` trimmed to BTC;
  `subscription_ack.json` holds `subscriptionResponse` acks plus one `pong`.
- `malformed/*.json` — hand-made Binance aggTrade frames: wrong symbol, non-numeric price,
  70-char decimal, missing `q`, truncated JSON. The 2 MiB oversize frame is generated in
  tests, not committed.
- `raw-frame-batch-v1.hex` — RawFrameBatch golden bytes written by the TS encoder test.

Bounds: 256 KiB per file; 512 KiB for the `.jsonl` sequence.
