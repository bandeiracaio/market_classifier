# Cumulative volume delta (CVD)

Code: `engine/src/processors/cvd.cpp`. Spec §11 aggressor-delta semantics.

- Unit: base asset (BTC). `CVD += q` for aggressor Buy, `CVD −= q` for aggressor Sell.
  Unknown-side trades are ignored and counted (`unknown_side_trades`); neither venue
  currently emits Unknown.
- Exact `Decimal` sums; overflow is never wrapped — the series stops and reports `Failed`.
- Live from page load only (no backfill, packet §3). Reload starts at 0.
- Series: one point per UTC minute holding the CVD after the last trade of that minute;
  ring of 1,440 points (24 h).
- Optional daily reset at 00:00 UTC, decided by trade `source_time` (not wall clock); the
  first point of the new day carries `reset = true`.
- Discontinuity (queue drop, reconnect, book gap on the venue) calls `mark_gap(t)`: the
  point for that minute is flagged `gap = true` and the panel draws a break; the value is
  not adjusted or interpolated.
