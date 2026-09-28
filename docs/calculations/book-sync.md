# Binance USD-M local order book sync

Source: "How to manage a local order book correctly",
https://developers.binance.com/docs/derivatives/usds-margined-futures/websocket-market-streams/How-to-manage-a-local-order-book-correctly
(read 2026-09-27). Implementation: `engine/src/books/binance_book_sync.cpp`.

## Procedure

1. Open `btcusdt@depth@100ms` on the `/public` route and buffer diffs
   (`k_max_buffered_diffs = 2048`; overflow drops the oldest and increments
   `buffer_overflows`). The first buffered diff returns `request_snapshot = true`.
2. Fetch `GET /fapi/v1/depth?symbol=BTCUSDT&limit=1000`; `lastUpdateId = L`.
3. Drop buffered diffs with `u < L` (counted in `discarded_stale`).
4. The first applied diff must satisfy `U <= L <= u`. If the next diff has `U > L`,
   updates are missing ⇒ gap. If no diff has reached `L` yet, the snapshot is held and
   later diffs are evaluated against it.
5. Afterwards each diff must have `pu == previous u`; otherwise ⇒ gap.
6. Quantity `0` removes a level.

## Gap handling

A gap (`pu` discontinuity, `U > L` on the first diff, missing snapshot sequence, or a
crossed/locked book after applying a diff) sets `GapDetected`, clears the book and the
buffer, increments `resync_count`, and returns `request_snapshot = true`. Diffs keep
buffering until the new snapshot arrives. A book is never shown as Live while in
`GapDetected` or `AwaitingSnapshot`.

## Bounds

- `k_max_book_levels_per_side = 5000`. A snapshot keeps the 5000 levels nearest the touch;
  a diff that would add a level beyond the bound is refused and counted
  (`level_bound_hits`) — removals and updates still apply.
- A crossed or locked book (best bid ≥ best ask) is treated as corruption.

## Verification

`engine/tests/test_binance_book_sync.cpp` covers normal straddle, stale-diff discard,
held snapshot, `U > L` gap, `pu` gap with recovery, crossed-book gap, buffer overflow,
and a replay of `fixtures/mvp/binance/depth_sequence.jsonl` (200 real diffs + snapshot).
