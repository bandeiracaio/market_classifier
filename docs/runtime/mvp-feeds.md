# MVP feed runtime

Date: 2026-09-27. Code: `engine/include/market_classifier/runtime/{feed_state,engine,ring}.hpp`.

## Data path

TS drivers buffer raw venue frames and flush one `RawFrameBatch` per animation frame
(`mc_submit_raw`). `Engine::submit_raw` only decodes the batch and enqueues frames into a
per-venue queue; it never touches books, processors or read models. `Engine::frame(budget)`
drains the queues round-robin (so one flooded venue cannot starve the other), adapts
frames to normalized events, updates books, then processors (Task 7), then ticks feed
state.

Why raw frames are queued rather than normalized batches in M1 `BoundedIngress`: a
legitimate 1,000-level Binance depth snapshot (~80 KB of levels) and a 1,000-candle
preload (> 256 events) exceed M1's 64 KiB / 256-event batch bounds; raising those would
weaken them. The raw queue has its own fixed bounds and the same drop-oldest policy.

## Bounds

| Resource | Limit |
|---|---:|
| Frames per batch | 64 (`bridge::k_max_frames_per_batch`) |
| Bytes per frame | 512 KiB |
| Batch payload | 1 MiB |
| Queued frames per venue | 512 (`k_max_queued_raw_frames`) |
| Queued bytes per venue | 4 MiB (`k_max_queued_raw_bytes`) |
| Frames adapted per `frame()` | 256 (`k_max_frames_per_drain`) plus the ms budget |

Overflow evicts the oldest queued frames of that venue, increments `frames_dropped`, and
latches a gap for that venue only (processors mark a discontinuity; Binance depth
continuity checks will force a resync if diffs were lost).

## Feed state machine (per venue)

`Connecting → Live` on socket open; `Live → Stale` when no frame for more than
`stale_after_ms`; any frame returns `Stale → Live`; close/error → `Reconnecting` with
backoff; after `max_attempts_before_failed` consecutive failures → `Failed` (user retry).
Frames from a socket already marked `Reconnecting` do not revive it. A close on one
venue never touches the other venue's objects (tested in `test_engine.cpp`).

| Setting | Value | Rationale |
|---|---:|---|
| `stale_after_ms` | 5,000 | Binance `markPrice@1s` and Hyperliquid `activeAssetCtx`/`bbo` update at least every few seconds; 5 s silence is abnormal |
| `backoff_base_ms` | 500 | |
| `backoff_cap_ms` | 30,000 | |
| `max_attempts_before_failed` | 20 | ≈ 8 minutes of capped retries |

Backoff: `delay = min(cap, base · 2^attempt) · (0.5 + 0.5·u)`, `u` from a seeded
`std::minstd_rand` (per-venue fixed seed; deterministic in tests). The attempt counter
resets when data flows again.

## Binance two-socket rule

Binance data arrives on two sockets (`/public`, `/market`) sharing one venue feed state.
If either socket closes, the driver closes both and reports one close; both reopen when
`mc_should_reconnect` returns 1. On close the Binance book is reset and the next diff
requests a new REST snapshot (`mc_request_snapshot`). Hyperliquid's book is cleared on close
and rebuilt by the next `l2Book` snapshot.

## Heartbeats

- Binance: server ping every 3 min, answered by the browser automatically.
- Hyperliquid: driver sends `{"method":"ping"}` every 30 s (server idles out at 60 s).
