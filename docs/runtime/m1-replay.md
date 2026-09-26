# M1 Deterministic Replay and Read-Model Seam

Date: 2026-09-25

`ReplayHarness` accepts at most 4,096 normalized entries with nondecreasing millisecond
offsets. It validates the full timeline before changing runtime state, advances only an
injected `FakeClock`, submits each event through `BoundedIngress`, and invokes the separate
read-model drain operation. It never sleeps or reads a system clock.

The dummy read model is the M1 observable rendering seam. It contains:

- total and per-event-kind counts;
- last local sequence, source time, and receive time;
- current quality using legal domain transitions;
- accepted, rejected, and dropped ingress counters.

The synthetic fixture `fixtures/m1/reference-replay.csv` contains four feed-status events,
including equal replay offsets and a delayed-to-live recovery. Tests load this semantic
fixture twice and require identical final clocks, replay result, model state, and diagnostics.
Update the fixture only with an explained replay-contract change and update its assertions in
the same commit.

This seam intentionally does not render a production panel, parse exchange data, or perform
real-time pacing. Slice 6 may expose the same read model through the browser diagnostics
surface without changing replay semantics.
