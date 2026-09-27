# Footprint and volume-at-price

Code: `engine/src/processors/footprint.cpp`.

- Bucket = `floor(price / b) · b` (exact `Decimal`, toward −∞). Stored at `b = $1` and 1-minute
  bars; panels choose $1/$5/$10/$25 and intervals ≥ 1m, aggregated on read by re-flooring
  stored bucket floors (exact because every option is a multiple of $1 and every interval a
  multiple of 1 minute).
- Cell volumes (base units): `bid_volume` = sell-aggressor quantity (trades that hit the
  bid); `ask_volume` = buy-aggressor quantity (trades that lifted the ask). Displayed
  `bid × ask`. Unknown-side trades are excluded.
- Bounds: 512 cells per stored 1-minute bar (excess counted in `dropped_cells`), 1,440 bars,
  50,000 profile buckets.
- Volume-at-price: session profile from page load; POC = bucket with maximum
  `bid + ask`; ties resolve to the lower price.
- Gaps: the bar containing a discontinuity is flagged `gap`.
