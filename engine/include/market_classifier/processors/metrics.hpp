#pragma once

#include "market_classifier/domain/events.hpp"
#include "market_classifier/processors/limits.hpp"
#include "market_classifier/runtime/ring.hpp"

#include <cstdint>
#include <optional>

namespace market_classifier::processors {

struct SeriesPoint {
    std::int64_t t_ms = 0; // minute (OI, funding) or second (spread) bucket start
    domain::Decimal value;
};

struct SpreadPoint {
    std::int64_t t_ms = 0; // second bucket start
    domain::Decimal spread;
    domain::Decimal mid;
};

struct MetricsView {
    std::optional<domain::AssetMetrics> asset;
    std::optional<domain::MarketSummary> summary;
    std::optional<domain::OpenInterest> open_interest_last;
    std::optional<domain::Bbo> bbo;
    std::optional<domain::Decimal> mid; // (bid + ask) / 2, exact
    runtime::Ring<SeriesPoint, k_series_minutes> open_interest;
    runtime::Ring<SeriesPoint, k_series_minutes> funding;
    runtime::Ring<SpreadPoint, k_spread_seconds> spread;
    std::int64_t oi_sample_interval_ms = 0; // 0 = streamed
    std::int64_t last_update_ms        = 0;
    std::int64_t oi_update_ms          = 0;
};

class Metrics {
  public:
    void on_asset_metrics(const domain::AssetMetrics &metrics);
    void on_open_interest(const domain::OpenInterest &oi);
    void on_summary(const domain::MarketSummary &summary);
    void on_bbo(const domain::Bbo &bbo);

    [[nodiscard]] const MetricsView &view() const noexcept { return view_; }

  private:
    void touch(std::int64_t t_ms) noexcept;
    MetricsView view_;
};

struct BasisView {
    domain::Decimal binance_mid;
    domain::Decimal hyperliquid_mid;
    domain::Decimal basis;           // Binance mid - Hyperliquid mid, exact
    double basis_bps            = 0; // basis / Hyperliquid mid * 1e4, half-even 2 dp (display)
    std::int64_t last_update_ms = 0;
};

// Overview cross-venue basis (docs/calculations/basis.md). nullopt until both mids exist.
[[nodiscard]] std::optional<BasisView> cross_venue_basis(const Metrics &binance,
                                                         const Metrics &hyperliquid);
[[nodiscard]] double round_half_even_2dp(double value) noexcept;
// Funding periods per year: Binance BTCUSDT 8h (3/day), Hyperliquid hourly (24/day).
[[nodiscard]] domain::Decimal annualized_funding(const domain::Decimal &rate, domain::Venue venue);
// Hyperliquid funds on every UTC hour.
[[nodiscard]] std::int64_t next_hourly_funding_ms(std::int64_t now_ms) noexcept;

} // namespace market_classifier::processors
