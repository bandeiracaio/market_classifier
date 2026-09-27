#pragma once

#include "market_classifier/domain/events.hpp"
#include "market_classifier/processors/limits.hpp"
#include "market_classifier/runtime/ring.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace market_classifier::processors {

// One price bucket: `bid_volume` = sell-aggressor volume (hit the bid), `ask_volume` =
// buy-aggressor volume (lifted the ask). Base units. docs/calculations/footprint.md.
struct FootprintCell {
    domain::Decimal price{}; // bucket floor
    domain::Decimal bid_volume{};
    domain::Decimal ask_volume{};
};

struct FootprintCandle {
    std::int64_t open_time_ms = 0;
    std::vector<FootprintCell> cells; // ascending price
    bool gap = false;
};

class VolumeProfile {
  public:
    // Returns false when a new bucket would exceed k_max_profile_buckets.
    bool add(const domain::Decimal &bucket, const domain::Decimal &qty, bool sell);
    void clear() noexcept;

    [[nodiscard]] const std::vector<FootprintCell> &buckets() const noexcept { return buckets_; }
    // Point of control: bucket with the largest bid+ask volume; ties go to the lower price.
    [[nodiscard]] std::optional<domain::Decimal> poc() const;

  private:
    std::vector<FootprintCell> buckets_; // ascending price
};

// Footprint candles plus the session volume-at-price profile from page load.
class Footprint {
  public:
    explicit Footprint(domain::Decimal bucket);

    void on_trade(const domain::Trade &trade);
    void set_interval(std::int64_t ms); // clears candles
    void mark_gap(std::int64_t t_ms);

    [[nodiscard]] const runtime::Ring<FootprintCandle, k_series_minutes> &candles() const noexcept {
        return candles_;
    }
    [[nodiscard]] const VolumeProfile &session_profile() const noexcept { return profile_; }
    [[nodiscard]] std::uint64_t dropped_cells() const noexcept { return dropped_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] domain::Decimal bucket() const noexcept { return bucket_; }
    [[nodiscard]] std::int64_t last_update_ms() const noexcept { return last_update_ms_; }

  private:
    FootprintCandle &candle_for(std::int64_t open_time);

    domain::Decimal bucket_;
    std::int64_t interval_ms_ = k_minute_ms;
    runtime::Ring<FootprintCandle, k_series_minutes> candles_;
    VolumeProfile profile_;
    std::uint64_t dropped_       = 0;
    std::int64_t last_update_ms_ = 0;
    bool pending_gap_            = false;
    bool failed_                 = false;
};

// Exact re-bucketing of stored candles to a coarser bucket (multiple of the stored one)
// and interval (multiple of the stored one). Returns at most `max_candles`, newest last.
[[nodiscard]] std::vector<FootprintCandle>
aggregate(const runtime::Ring<FootprintCandle, k_series_minutes> &candles,
          const domain::Decimal &bucket, std::int64_t interval_ms, std::size_t max_candles);
[[nodiscard]] VolumeProfile aggregate(const VolumeProfile &profile, const domain::Decimal &bucket);

} // namespace market_classifier::processors
