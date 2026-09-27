# Liquidity heatmap

Code: `engine/src/processors/heatmap.cpp`. Presentation-only data.

- One column per 250 ms (`floor(receive_time / 250) · 250`); the newest book sample inside a
  column replaces older ones. 14,400 columns = 60 minutes.
- Binance columns are sampled only while the synced book is `Live`; missing time is left
  blank, never interpolated.
- Up to 200 levels per side nearest the touch.
- Storage (memory bound): price as `int16` offset in quantum units from the column's best
  bid (`quantum` = 0.1 Binance tick, 1 for Hyperliquid at BTC prices); levels farther than
  ±32,767 quanta or off-quantum are dropped and counted (`clipped_levels`). Quantity
  encoded as `round(log2(1 + q) · 1024)` in 16 bits (relative error ≈ 0.07 %). 4 bytes per
  level; worst case ≈ 23 MB per venue.
- Color scale: `log2(1 + q)` normalized by the maximum seen quantity. Trades overlaid from
  a separate 5,000-trade ring.
