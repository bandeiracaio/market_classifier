#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace market_classifier::processors {

// Retention bounds (packet MVP_BTC_TERMINAL.md §6). All in-memory, cleared on reload.
inline constexpr std::size_t k_tape_capacity       = 5000;
inline constexpr std::size_t k_heatmap_columns     = 14400; // 60 min at 250 ms
inline constexpr std::int64_t k_heatmap_column_ms  = 250;
inline constexpr std::size_t k_heatmap_levels      = 200; // per side per column, nearest touch
inline constexpr std::size_t k_heatmap_trades      = 5000;
inline constexpr std::size_t k_candle_capacity     = 2000; // per interval
inline constexpr std::size_t k_series_minutes      = 1440; // 24h at 1m
inline constexpr std::size_t k_spread_seconds      = 3600; // BBO/spread: 1h at 1s
inline constexpr std::size_t k_max_footprint_cells = 512;  // per 1m candle at $1
inline constexpr std::size_t k_max_profile_buckets = 50000;
inline constexpr std::size_t k_max_liquidations    = 1000;
inline constexpr std::array<std::int64_t, 6> k_candle_intervals_ms{
    60'000, 300'000, 900'000, 3'600'000, 14'400'000, 86'400'000};
inline constexpr std::array<std::string_view, 4> k_bucket_sizes{"1", "5", "10", "25"}; // USD

inline constexpr std::int64_t k_minute_ms = 60'000;
inline constexpr std::int64_t k_day_ms    = 86'400'000;

// Floor to a multiple of `step` (step > 0), correct for negative inputs.
[[nodiscard]] constexpr std::int64_t floor_to_ms(std::int64_t t, std::int64_t step) noexcept {
    const auto q = t / step;
    return (t % step != 0 && t < 0 ? q - 1 : q) * step;
}

} // namespace market_classifier::processors
