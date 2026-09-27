#include "market_classifier/books/order_book.hpp"

#include <algorithm>

namespace market_classifier::books {
namespace {

// `before(a, b)` is true when price a sorts ahead of b on this side.
template <typename Before>
bool upsert(std::vector<domain::BookLevel> &side, const domain::BookLevel &level, Before before) {
    const auto it = std::lower_bound(
        side.begin(), side.end(), level.price,
        [&](const domain::BookLevel &l, const domain::Decimal &p) { return before(l.price, p); });
    const bool exists = it != side.end() && it->price == level.price;
    if (level.quantity.mantissa() == 0) {
        if (exists) {
            side.erase(it);
        }
        return true;
    }
    if (exists) {
        *it = level;
        return true;
    }
    if (side.size() >= k_max_book_levels_per_side) {
        if (it == side.end()) {
            return false; // farther than every kept level: nothing a panel would show
        }
        side.pop_back(); // drop the farthest level; the book stays exact near the touch
        const auto pos =
            std::lower_bound(side.begin(), side.end(), level.price,
                             [&](const domain::BookLevel &l, const domain::Decimal &p) {
                                 return before(l.price, p);
                             });
        side.insert(pos, level);
        return true;
    }
    side.insert(it, level);
    return true;
}

bool higher(const domain::Decimal &a, const domain::Decimal &b) {
    return a > b;
}
bool lower(const domain::Decimal &a, const domain::Decimal &b) {
    return a < b;
}

template <typename Before>
void load(std::vector<domain::BookLevel> &side, const std::vector<domain::BookLevel> &in,
          Before before) {
    side.clear();
    for (const auto &level : in) {
        if (level.quantity.mantissa() > 0) {
            side.push_back(level);
        }
    }
    std::stable_sort(side.begin(), side.end(),
                     [&](const auto &a, const auto &b) { return before(a.price, b.price); });
    // Duplicate prices in a snapshot: the first occurrence wins (stable sort).
    side.erase(std::unique(side.begin(), side.end(),
                           [](const auto &a, const auto &b) { return a.price == b.price; }),
               side.end());
    if (side.size() > k_max_book_levels_per_side) {
        side.resize(k_max_book_levels_per_side);
    }
}

} // namespace

void OrderBook::apply_snapshot(const domain::BookSnapshot &snapshot) {
    load(bids_, snapshot.bids, higher);
    load(asks_, snapshot.asks, lower);
}

bool OrderBook::apply_levels(std::span<const domain::BookLevel> bids,
                             std::span<const domain::BookLevel> asks) {
    bool ok = true;
    for (const auto &level : bids) {
        ok = upsert(bids_, level, higher) && ok;
    }
    for (const auto &level : asks) {
        ok = upsert(asks_, level, lower) && ok;
    }
    return ok;
}

void OrderBook::clear() noexcept {
    bids_.clear();
    asks_.clear();
}

std::optional<domain::BookLevel> OrderBook::best_bid() const noexcept {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.front();
}

std::optional<domain::BookLevel> OrderBook::best_ask() const noexcept {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.front();
}

bool OrderBook::crossed() const noexcept {
    return !bids_.empty() && !asks_.empty() && bids_.front().price >= asks_.front().price;
}

} // namespace market_classifier::books
