#include "market_classifier/processors/candles.hpp"

#include <algorithm>

namespace market_classifier::processors {
namespace {

std::optional<std::size_t> slot(std::int64_t interval_ms) {
    for (std::size_t i = 0; i < k_candle_intervals_ms.size(); ++i) {
        if (k_candle_intervals_ms.at(i) == interval_ms) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace

void CandleSeries::preload(std::span<const domain::Candle> candles) {
    for (const auto &c : candles) {
        on_candle(c);
    }
}

void CandleSeries::on_candle(const domain::Candle &candle) {
    const auto index = slot(candle.interval_ms);
    if (!index) {
        ++rejected_;
        return;
    }
    auto &series    = series_.at(*index);
    last_update_ms_ = std::max(last_update_ms_, candle.meta.source_time().value);
    if (series.empty() || series.back().open_time_ms < candle.open_time_ms) {
        series.push_back(candle);
        if (series.size() > k_candle_capacity) {
            series.erase(series.begin()); // O(capacity) only when a new bar opens
        }
        return;
    }
    // Upsert an existing bar (the live bar, or a late correction within retention).
    const auto it = std::lower_bound(
        series.begin(), series.end(), candle.open_time_ms,
        [](const domain::Candle &c, std::int64_t t) { return c.open_time_ms < t; });
    if (it != series.end() && it->open_time_ms == candle.open_time_ms) {
        *it = candle;
    }
    // Bars older than retention, or missing in the middle, are ignored: inserting would
    // fabricate continuity the stream did not provide.
}

void CandleSeries::mark_gap(std::int64_t t_ms) {
    gap_after_ms_ = t_ms;
}

std::span<const domain::Candle> CandleSeries::candles(std::int64_t interval_ms) const {
    const auto index = slot(interval_ms);
    if (!index) {
        return {};
    }
    return series_.at(*index);
}

} // namespace market_classifier::processors
