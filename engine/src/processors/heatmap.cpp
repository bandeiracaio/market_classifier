#include "market_classifier/processors/heatmap.hpp"

#include "market_classifier/domain/decimal_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace market_classifier::processors {
namespace {

// Quantization: code = round(log2(1 + qty) * 1024), 16 bits covers qty up to ~2^64.
constexpr double k_log_scale = 1024.0;

// Price in quantum units, if exactly representable (it is for tick-aligned prices).
std::optional<std::int64_t> ticks_of(const domain::Decimal &price, const domain::Decimal &quantum) {
    const auto floored = domain::floor_to(price, quantum);
    if (!floored || !(floored.value == price)) {
        return std::nullopt;
    }
    // price / quantum as integer: compare mantissas at a common scale.
    const auto scale = std::max(price.scale(), quantum.scale());
    const auto p     = price.rescale_exact(scale);
    const auto q     = quantum.rescale_exact(scale);
    if (!p || !q || q.value.mantissa() == 0) {
        return std::nullopt;
    }
    return p.value.mantissa() / q.value.mantissa();
}

template <typename Levels>
void encode_side(const Levels &levels, std::int64_t ref, const domain::Decimal &quantum,
                 std::vector<std::int16_t> &offsets, std::vector<std::uint16_t> &quantities,
                 double &max_q, std::uint64_t &clipped) {
    const auto n = std::min(levels.size(), k_heatmap_levels);
    offsets.reserve(n);
    quantities.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto ticks = ticks_of(levels[i].price, quantum);
        if (!ticks) {
            ++clipped;
            continue;
        }
        const auto offset = *ticks - ref;
        if (offset < std::numeric_limits<std::int16_t>::min() ||
            offset > std::numeric_limits<std::int16_t>::max()) {
            ++clipped; // farther than ±32767 quanta from the touch
            continue;
        }
        const double q = domain::to_double(levels[i].quantity);
        max_q          = std::max(max_q, q);
        offsets.push_back(static_cast<std::int16_t>(offset));
        quantities.push_back(HeatmapColumn::encode_quantity(q));
    }
}

} // namespace

double HeatmapColumn::price_of(std::int16_t offset, const domain::Decimal &quantum) const noexcept {
    // (ticks * mantissa) / 10^scale: one correctly rounded division.
    const auto ticks = static_cast<double>(ref_ticks + offset);
    return ticks * static_cast<double>(quantum.mantissa()) / std::pow(10.0, quantum.scale());
}

std::uint16_t HeatmapColumn::encode_quantity(double quantity) noexcept {
    if (!(quantity > 0)) {
        return 0;
    }
    const double code = std::round(std::log2(1.0 + quantity) * k_log_scale);
    return static_cast<std::uint16_t>(std::min(code, 65535.0));
}

double HeatmapColumn::decode_quantity(std::uint16_t code) noexcept {
    return std::exp2(static_cast<double>(code) / k_log_scale) - 1.0;
}

Heatmap::Heatmap(domain::Decimal quantum) : quantum_(quantum) {}

void Heatmap::on_book(const books::OrderBook &book, std::int64_t t_ms) {
    const auto best = book.best_bid();
    if (!best) {
        return;
    }
    const auto ref = ticks_of(best->price, quantum_);
    if (!ref) {
        ++clipped_;
        return;
    }
    const auto column_t = floor_to_ms(t_ms, k_heatmap_column_ms);
    last_update_ms_     = std::max(last_update_ms_, t_ms);
    HeatmapColumn column;
    column.t_ms      = column_t;
    column.ref_ticks = *ref;
    encode_side(book.bids(), *ref, quantum_, column.bid_offsets, column.bid_quantities,
                max_quantity_, clipped_);
    encode_side(book.asks(), *ref, quantum_, column.ask_offsets, column.ask_quantities,
                max_quantity_, clipped_);
    // The newest sample inside a column wins.
    if (!columns_.empty() && columns_.back().t_ms == column_t) {
        columns_.back() = std::move(column);
    } else if (columns_.empty() || columns_.back().t_ms < column_t) {
        columns_.push(std::move(column));
    }
}

void Heatmap::on_trade(const domain::Trade &trade) {
    trades_.push(trade);
}

void Heatmap::mark_gap(std::int64_t /*t_ms*/) {
    // Missing columns already render as blank time; nothing is interpolated.
}

HeatmapRaster rasterize(const Heatmap &heatmap, std::int64_t begin_ms, std::int64_t end_ms,
                        double lo, double hi, std::size_t columns, std::size_t rows) {
    HeatmapRaster raster;
    raster.columns = std::clamp<std::size_t>(columns, 1, k_max_raster_columns);
    raster.rows    = std::clamp<std::size_t>(rows, 1, k_max_raster_rows);
    raster.cells.assign(raster.columns * raster.rows, 0.0F);
    if (end_ms <= begin_ms || !(hi > lo)) {
        return raster;
    }
    const double max_log     = std::log2(1.0 + std::max(heatmap.max_quantity(), 1e-9));
    const auto span_ms       = static_cast<double>(end_ms - begin_ms);
    const auto &columns_ring = heatmap.columns();
    for (std::size_t i = 0; i < columns_ring.size(); ++i) {
        const auto &c = columns_ring[i];
        if (c.t_ms < begin_ms || c.t_ms > end_ms) {
            continue;
        }
        const auto col =
            std::min(raster.columns - 1,
                     static_cast<std::size_t>(static_cast<double>(c.t_ms - begin_ms) / span_ms *
                                              static_cast<double>(raster.columns)));
        const auto side = [&](const std::vector<std::int16_t> &offsets,
                              const std::vector<std::uint16_t> &codes) {
            for (std::size_t k = 0; k < offsets.size(); ++k) {
                const double p = c.price_of(offsets[k], heatmap.quantum());
                if (p < lo || p > hi) {
                    continue;
                }
                const auto row = std::min(
                    raster.rows - 1, static_cast<std::size_t>((hi - p) / (hi - lo) *
                                                              static_cast<double>(raster.rows)));
                const double q = HeatmapColumn::decode_quantity(codes[k]);
                auto &cell     = raster.cells[row * raster.columns + col];
                cell =
                    std::max(cell, static_cast<float>(std::min(1.0, std::log2(1.0 + q) / max_log)));
            }
        };
        side(c.bid_offsets, c.bid_quantities);
        side(c.ask_offsets, c.ask_quantities);
    }
    return raster;
}

} // namespace market_classifier::processors
