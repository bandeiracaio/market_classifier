# M1 Bridge Codec Measurement

Date: 2026-09-25  
Machine: M0 reference development machine  
Runtime: Node.js v24.11.0, production TypeScript codec output

## Method

A batch of 256 copies of the synthetic BTC trade used by the shared golden fixture was
encoded and decoded 100 times for warmup, then 2,000 timed iterations per operation using
`performance.now()`. The benchmark performs full buffer allocation/encoding and full
semantic decode/owned-object construction. It performs no network or filesystem work.

## Result

| Events | Encoded bytes | Mean encode | Mean decode |
|---:|---:|---:|---:|
| 256 | 19,216 | 0.243 ms | 0.124 ms |

The maximum-size event-count batch is far below the 16.7 ms active-frame budget. This is a
microbenchmark, not an ingress-to-render latency claim; Slice 5 replay will measure the full
path. Re-run and record distribution percentiles before using this result as a regression
gate.

## Browser/WASM proof

On 2026-09-26, the Playwright Chromium smoke path submitted the 91-byte shared one-trade
fixture through the C++ WASM decoder, bounded ingress queue, and dummy read model. It accepted
one event with zero dropped batches. The test then submitted a copy with an unsupported
protocol version and received the `unsupported-version` diagnostic; frame count continued
to advance. After 50 warmups, 1,000 decode-only iterations averaged 0.001 ms per 91-byte
message in this browser run. `performance.now()` precision and the tiny single-event payload
make this a smoke measurement, not a performance regression threshold.
