# Spread, cross-venue basis and funding

Code: `engine/src/processors/metrics.cpp`. Spec §11 spread/basis.

- Mid = `(bid + ask) / 2` from each venue's latest BBO, exact `Decimal`.
- Spread = `ask − bid`; ticks = spread / tick size; bps = spread / mid · 10⁴ (display).
  Spread series: latest value per UTC second, 3,600 points.
- Cross-venue basis (Overview) = Binance mid − Hyperliquid mid (exact, USD; USDT and USDC
  treated as USD — a stablecoin depeg shows up as basis). bps = basis / Hyperliquid mid ·
  10⁴, computed in `double` and rounded half-even to 2 dp — presentation only.
- Funding annualized = rate × periods/year: Binance BTCUSDT 8-hour funding (3 × 365);
  Hyperliquid hourly funding (24 × 365). Countdown: Binance `nextFundingTime` from
  `markPrice`; Hyperliquid next UTC hour.
- Open interest series: last sample per UTC minute (1,440 points), with its cadence
  (`sample_interval_ms`: Binance REST poll 10 s; Hyperliquid 0 = streamed).
