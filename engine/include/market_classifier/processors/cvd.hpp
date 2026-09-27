#pragma once

#include "market_classifier/domain/events.hpp"
#include "market_classifier/processors/limits.hpp"
#include "market_classifier/runtime/ring.hpp"

#include <cstdint>

namespace market_classifier::processors {

struct CvdPoint {
    std::int64_t minute_ms = 0;
    domain::Decimal value{}; // CVD at the last trade of the minute
    bool gap   = false;      // continuity broken inside/before this minute
    bool reset = false;      // daily reset happened in this minute
};

// Cumulative volume delta in base units (docs/calculations/cvd.md): buy aggressor adds
// quantity, sell subtracts, unknown is ignored and counted. Live from page load only.
class Cvd {
  public:
    void on_trade(const domain::Trade &trade);
    void reset();
    void mark_gap(std::int64_t t_ms);

    [[nodiscard]] domain::Decimal value() const noexcept { return value_; }
    [[nodiscard]] const runtime::Ring<CvdPoint, k_series_minutes> &series() const noexcept {
        return series_;
    }
    [[nodiscard]] std::uint64_t unknown_side_trades() const noexcept { return unknown_; }
    [[nodiscard]] std::uint64_t gap_count() const noexcept { return gaps_; }
    // Decimal overflow is never wrapped: the series stops and reports Failed.
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] std::int64_t last_update_ms() const noexcept { return last_update_ms_; }

    bool daily_reset_enabled = false; // at 00:00 UTC by trade source_time

  private:
    CvdPoint &point_for(std::int64_t minute);

    domain::Decimal value_{};
    runtime::Ring<CvdPoint, k_series_minutes> series_;
    std::int64_t last_day_       = -1;
    std::int64_t last_update_ms_ = 0;
    std::uint64_t unknown_       = 0;
    std::uint64_t gaps_          = 0;
    bool failed_                 = false;
};

} // namespace market_classifier::processors
