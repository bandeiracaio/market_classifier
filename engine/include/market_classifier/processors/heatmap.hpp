#pragma once

#include "market_classifier/books/order_book.hpp"
#include "market_classifier/domain/events.hpp"
#include "market_classifier/processors/limits.hpp"
#include "market_classifier/runtime/ring.hpp"

#include <cstdint>
#include <vector>

namespace market_classifier::processors {

// One 250 ms book sample stored compactly for rendering only (docs/calculations/heatmap.md):
// price = (ref_ticks + offset) * quantum, quantity log2-quantized to 16 bits.
struct HeatmapColumn {
    std::int64_t t_ms      = 0;
    std::int64_t ref_ticks = 0; // best bid in quantum units
    std::vector<std::int16_t> bid_offsets;
    std::vector<std::uint16_t> bid_quantities;
    std::vector<std::int16_t> ask_offsets;
    std::vector<std::uint16_t> ask_quantities;

    [[nodiscard]] double price_of(std::int16_t offset,
                                  const domain::Decimal &quantum) const noexcept;
    [[nodiscard]] static std::uint16_t encode_quantity(double quantity) noexcept;
    [[nodiscard]] static double decode_quantity(std::uint16_t code) noexcept;
};

class Heatmap {
  public:
    explicit Heatmap(domain::Decimal quantum);

    void on_book(const books::OrderBook &book, std::int64_t t_ms); // one column per 250 ms
    void on_trade(const domain::Trade &trade);
    void mark_gap(std::int64_t t_ms);

    [[nodiscard]] const runtime::Ring<HeatmapColumn, k_heatmap_columns> &columns() const noexcept {
        return columns_;
    }
    [[nodiscard]] const runtime::Ring<domain::Trade, k_heatmap_trades> &trades() const noexcept {
        return trades_;
    }
    [[nodiscard]] const domain::Decimal &quantum() const noexcept { return quantum_; }
    // Largest quantity seen (for color scaling); presentation only.
    [[nodiscard]] double max_quantity() const noexcept { return max_quantity_; }
    [[nodiscard]] std::uint64_t clipped_levels() const noexcept { return clipped_; }
    [[nodiscard]] std::int64_t last_update_ms() const noexcept { return last_update_ms_; }

  private:
    domain::Decimal quantum_;
    runtime::Ring<HeatmapColumn, k_heatmap_columns> columns_;
    runtime::Ring<domain::Trade, k_heatmap_trades> trades_;
    double max_quantity_         = 0;
    std::uint64_t clipped_       = 0;
    std::int64_t last_update_ms_ = 0;
};

} // namespace market_classifier::processors
