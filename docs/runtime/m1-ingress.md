# M1 Clock and Bounded Ingress Contract

Date: 2026-09-25

M1 uses an injected `Clock` interface with separate wall and monotonic milliseconds.
Production reads `system_clock` and `steady_clock`; deterministic tests and replay use
`FakeClock`, which advances without sleeping and rejects negative or overflowing advances.

The single-threaded WASM runtime separates submission from render-owned consumption with a
FIFO queue. Submission never directly mutates a read model.

## Limits and overflow policy

| Resource | Limit | Rationale |
|---|---:|---|
| Events per batch | 256 | Matches bridge V1 and bounds per-callback work |
| Encoded bytes per batch | 64 KiB | Matches bridge V1 before queue admission |
| Queued batches | 8 | Bounds burst retention and queue metadata |
| Queued encoded bytes | 512 KiB | Eight maximum-size batches; explicit memory accounting |
| Book levels per event | 1,024 per side | Prevents nested vectors from bypassing batch bounds |
| Dynamic text | 128 bytes per field | Bounds source IDs, cursors, and asset strings |

Invalid or empty batches are rejected without consuming capacity. When a valid incoming
batch would exceed either queue bound, the oldest batches are dropped until it fits. The
newest data is retained, dropped batch/event counters increase, and nonterminal quality is
latched to `GapDetected`. Returning to `Live` requires an explicit legal transition through
`Partial` or `Reconnecting`; existing `Unsupported` or `Failed` states are never replaced
by `GapDetected` merely because of queue pressure.

Counters expose accepted, rejected, and dropped batches and events. Queue status also
reports current batch, event, and encoded-byte occupancy. The byte count is the validated
wire size used for admission accounting. Before admission, the queue independently measures
owned event storage, rejects batches above 64 KiB, caps nested book vectors and text fields,
and copies accepted events into fresh storage so caller-controlled excess capacity is not
retained.
