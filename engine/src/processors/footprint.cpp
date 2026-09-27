#include "market_classifier/processors/footprint.hpp"

#include "market_classifier/domain/decimal_math.hpp"

#include <algorithm>

namespace market_classifier::processors {
namespace {

// Finds or inserts the cell for `price` in an ascending vector. Returns nullptr when the
// insert would exceed `bound`.
FootprintCell *cell_for(std::vector<FootprintCell> &cells, const domain::Decimal &price,
                        std::size_t bound) {
    const auto it = std::lower_bound(
        cells.begin(), cells.end(), price,
        [](const FootprintCell &c, const domain::Decimal &p) { return c.price < p; });
    if (it != cells.end() && it->price == price) {
        return &*it;
    }
    if (cells.size() >= bound) {
        return nullptr;
    }
    return &*cells.insert(it, FootprintCell{price, {}, {}});
}

bool accumulate(FootprintCell &cell, const domain::Decimal &qty, bool sell) {
    auto &target = sell ? cell.bid_volume : cell.ask_volume;
    const auto r = domain::add(target, qty);
    if (!r) {
        return false;
    }
    target = r.value;
    return true;
}

bool merge(FootprintCell &into, const FootprintCell &from) {
    return accumulate(into, from.bid_volume, true) && accumulate(into, from.ask_volume, false);
}

} // namespace

bool VolumeProfile::add(const domain::Decimal &bucket, const domain::Decimal &qty, bool sell) {
    auto *cell = cell_for(buckets_, bucket, k_max_profile_buckets);
    return cell != nullptr && accumulate(*cell, qty, sell);
}

void VolumeProfile::clear() noexcept {
    buckets_.clear();
}

std::optional<domain::Decimal> VolumeProfile::poc() const {
    std::optional<domain::Decimal> best_price;
    domain::Decimal best_total{};
    for (const auto &b : buckets_) {
        const auto total = domain::add(b.bid_volume, b.ask_volume);
        if (!total) {
            continue;
        }
        if (!best_price || total.value > best_total) { // strict: ties keep the lower price
            best_price = b.price;
            best_total = total.value;
        }
    }
    return best_price;
}

Footprint::Footprint(domain::Decimal bucket) : bucket_(bucket) {}

FootprintCandle &Footprint::candle_for(std::int64_t open_time) {
    if (candles_.empty() || candles_.back().open_time_ms < open_time) {
        candles_.push({open_time, {}, pending_gap_});
        pending_gap_ = false;
    }
    return candles_.back();
}

void Footprint::on_trade(const domain::Trade &trade) {
    if (failed_ || trade.aggressor_side == domain::AggressorSide::Unknown) {
        return;
    }
    const auto t      = trade.meta.source_time().value;
    last_update_ms_   = std::max(last_update_ms_, t);
    const auto bucket = domain::floor_to(trade.price, bucket_);
    if (!bucket) {
        failed_ = true;
        return;
    }
    const bool sell = trade.aggressor_side == domain::AggressorSide::Sell;
    auto &candle    = candle_for(floor_to_ms(t, interval_ms_));
    auto *cell      = cell_for(candle.cells, bucket.value, k_max_footprint_cells);
    if (cell == nullptr) {
        ++dropped_;
    } else if (!accumulate(*cell, trade.quantity, sell)) {
        failed_ = true;
        return;
    }
    if (!profile_.add(bucket.value, trade.quantity, sell)) {
        ++dropped_;
    }
}

void Footprint::set_interval(std::int64_t ms) {
    if (ms <= 0 || ms == interval_ms_) {
        return;
    }
    interval_ms_ = ms;
    candles_.clear();
}

void Footprint::mark_gap(std::int64_t t_ms) {
    const auto open = floor_to_ms(t_ms, interval_ms_);
    if (!candles_.empty() && candles_.back().open_time_ms == open) {
        candles_.back().gap = true;
    } else {
        pending_gap_ = true;
    }
}

std::vector<FootprintCandle>
aggregate(const runtime::Ring<FootprintCandle, k_series_minutes> &candles,
          const domain::Decimal &bucket, std::int64_t interval_ms, std::size_t max_candles) {
    std::vector<FootprintCandle> out;
    if (interval_ms <= 0 || max_candles == 0) {
        return out;
    }
    for (std::size_t i = 0; i < candles.size(); ++i) {
        const auto &src = candles[i];
        const auto open = floor_to_ms(src.open_time_ms, interval_ms);
        if (out.empty() || out.back().open_time_ms != open) {
            out.push_back({open, {}, false});
        }
        auto &dst = out.back();
        dst.gap   = dst.gap || src.gap;
        for (const auto &cell : src.cells) {
            const auto b = domain::floor_to(cell.price, bucket);
            if (!b) {
                continue;
            }
            // Aggregated candles span several stored ones, so allow the combined bound.
            if (auto *target = cell_for(dst.cells, b.value, k_max_footprint_cells * 4)) {
                merge(*target, cell);
            }
        }
    }
    if (out.size() > max_candles) {
        out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(max_candles));
    }
    return out;
}

VolumeProfile aggregate(const VolumeProfile &profile, const domain::Decimal &bucket) {
    VolumeProfile out;
    for (const auto &cell : profile.buckets()) {
        const auto b = domain::floor_to(cell.price, bucket);
        if (!b) {
            continue;
        }
        out.add(b.value, cell.bid_volume, true);
        out.add(b.value, cell.ask_volume, false);
    }
    return out;
}

} // namespace market_classifier::processors
