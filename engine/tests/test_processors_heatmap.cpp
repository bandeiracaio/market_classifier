#include "market_classifier/processors/heatmap.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

#include "processor_util.hpp"

using namespace market_classifier;
using mc_test::dec;

namespace {

books::OrderBook book_with(std::size_t levels_per_side) {
    domain::BookSnapshot s;
    for (std::size_t i = 0; i < levels_per_side; ++i) {
        const auto bid =
            domain::Decimal::from_parts(1'000'000 - static_cast<std::int64_t>(i), 1).value;
        const auto ask =
            domain::Decimal::from_parts(1'000'001 + static_cast<std::int64_t>(i), 1).value;
        s.bids.push_back({bid, dec("1.5"), std::nullopt});
        s.asks.push_back({ask, dec("2"), std::nullopt});
    }
    books::OrderBook book;
    book.apply_snapshot(s);
    return book;
}

} // namespace

TEST_CASE("two book samples within one 250 ms column keep one column") {
    processors::Heatmap h(dec("0.1"));
    const auto book = book_with(10);
    h.on_book(book, 1'000);
    h.on_book(book, 1'249);
    CHECK(h.columns().size() == 1);
    h.on_book(book, 1'250);
    CHECK(h.columns().size() == 2);
    CHECK(h.columns().back().t_ms == 1'250);
}

TEST_CASE("heatmap keeps 14400 columns and 200 levels per side") {
    processors::Heatmap h(dec("0.1"));
    const auto book = book_with(300);
    for (std::int64_t i = 0; i < 14401; ++i) {
        h.on_book(book, i * processors::k_heatmap_column_ms);
    }
    CHECK(h.columns().size() == processors::k_heatmap_columns);
    CHECK(h.columns()[0].t_ms == processors::k_heatmap_column_ms);
    const auto &col = h.columns().back();
    CHECK(col.bid_offsets.size() == processors::k_heatmap_levels);
    CHECK(col.ask_offsets.size() == processors::k_heatmap_levels);
}

TEST_CASE("heatmap cells decode back to price and quantity") {
    processors::Heatmap h(dec("0.1"));
    h.on_book(book_with(3), 0);
    const auto &col = h.columns().back();
    CHECK(col.price_of(col.bid_offsets[0], h.quantum()) == 100000.0);
    CHECK(col.price_of(col.ask_offsets[0], h.quantum()) == 100000.1);
    const double q = processors::HeatmapColumn::decode_quantity(col.bid_quantities[0]);
    CHECK(q > 1.49);
    CHECK(q < 1.51);
    CHECK(h.max_quantity() > 1.99);
}

TEST_CASE("heatmap trade overlay is bounded") {
    processors::Heatmap h(dec("0.1"));
    for (std::size_t i = 0; i < processors::k_heatmap_trades + 3; ++i) {
        h.on_trade(
            mc_test::trade(static_cast<std::int64_t>(i), "100", "1", domain::AggressorSide::Buy));
    }
    CHECK(h.trades().size() == processors::k_heatmap_trades);
}
