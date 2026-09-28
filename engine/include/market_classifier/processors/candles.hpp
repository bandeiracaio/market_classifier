#pragma once

#include "market_classifier/domain/events.hpp"
#include "market_classifier/processors/limits.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace market_classifier::processors {

// Candle series per interval: REST preload + live upserts keyed by open time.
class CandleSeries {
  public:
    void preload(std::span<const domain::Candle> candles);
    void on_candle(const domain::Candle &candle);
    void mark_gap(std::int64_t t_ms);

    // Contiguous, ascending open time; empty for unsupported intervals.
    [[nodiscard]] std::span<const domain::Candle> candles(std::int64_t interval_ms) const;
    [[nodiscard]] std::uint64_t rejected() const noexcept { return rejected_; }
    [[nodiscard]] std::int64_t last_update_ms() const noexcept { return last_update_ms_; }
    // Open time of the first bar after a continuity break, per interval (0 = none).
    [[nodiscard]] std::int64_t gap_after_ms() const noexcept { return gap_after_ms_; }

  private:
    std::array<std::vector<domain::Candle>, k_candle_intervals_ms.size()> series_;
    std::uint64_t rejected_      = 0;
    std::int64_t last_update_ms_ = 0;
    std::int64_t gap_after_ms_   = 0;
};

} // namespace market_classifier::processors
