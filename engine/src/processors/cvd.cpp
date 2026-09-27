#include "market_classifier/processors/cvd.hpp"

#include "market_classifier/domain/decimal_math.hpp"

#include <algorithm>

namespace market_classifier::processors {

CvdPoint &Cvd::point_for(std::int64_t minute) {
    if (series_.empty() || series_.back().minute_ms < minute) {
        series_.push({minute, value_, false, false});
    }
    // Out-of-order trades from an earlier minute update the newest point; the series
    // is a per-minute close, and trades are near-monotonic per venue.
    return series_.back();
}

void Cvd::on_trade(const domain::Trade &trade) {
    if (failed_) {
        return;
    }
    const auto t    = trade.meta.source_time().value;
    last_update_ms_ = std::max(last_update_ms_, t);
    const auto day  = floor_to_ms(t, k_day_ms) / k_day_ms;
    bool reset_now  = false;
    if (daily_reset_enabled && last_day_ >= 0 && day > last_day_) {
        value_    = {};
        reset_now = true;
    }
    last_day_ = std::max(last_day_, day);

    domain::DecimalResult next{value_};
    switch (trade.aggressor_side) {
    case domain::AggressorSide::Buy:
        next = domain::add(value_, trade.quantity);
        break;
    case domain::AggressorSide::Sell:
        next = domain::sub(value_, trade.quantity);
        break;
    case domain::AggressorSide::Unknown:
        ++unknown_;
        break;
    }
    if (!next) {
        failed_ = true; // never wrap; the panel shows Failed
        return;
    }
    value_      = next.value;
    auto &point = point_for(floor_to_ms(t, k_minute_ms));
    point.value = value_;
    point.reset = point.reset || reset_now;
}

void Cvd::reset() {
    value_ = {};
    series_.clear();
    last_day_ = -1;
    failed_   = false;
}

void Cvd::mark_gap(std::int64_t t_ms) {
    ++gaps_;
    auto &point = point_for(floor_to_ms(t_ms, k_minute_ms));
    point.gap   = true;
}

} // namespace market_classifier::processors
