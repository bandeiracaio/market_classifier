#pragma once

#include "market_classifier/domain/events.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace market_classifier::books {

inline constexpr std::size_t k_max_book_levels_per_side = 5000;

// Price-level book keyed by exact Decimal price. Sorted vectors with binary-search
// insert are sufficient at <= 5000 levels per side.
class OrderBook {
  public:
    // Replaces both sides; keeps the k_max_book_levels_per_side levels nearest the touch.
    void apply_snapshot(const domain::BookSnapshot &snapshot);
    // Quantity 0 removes a level. Returns false if a new level was refused by the bound
    // (all other changes are still applied).
    bool apply_levels(std::span<const domain::BookLevel> bids,
                      std::span<const domain::BookLevel> asks);
    void clear() noexcept;

    [[nodiscard]] std::span<const domain::BookLevel> bids() const noexcept { return bids_; }
    [[nodiscard]] std::span<const domain::BookLevel> asks() const noexcept { return asks_; }
    [[nodiscard]] std::optional<domain::BookLevel> best_bid() const noexcept;
    [[nodiscard]] std::optional<domain::BookLevel> best_ask() const noexcept;
    // True when best bid >= best ask; a locked or crossed book is never valid state.
    [[nodiscard]] bool crossed() const noexcept;

  private:
    std::vector<domain::BookLevel> bids_; // descending price
    std::vector<domain::BookLevel> asks_; // ascending price
};

} // namespace market_classifier::books
