#include "market_classifier/books/order_book.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace market_classifier;

namespace {
domain::Decimal dec(const char *text) {
    return domain::Decimal::parse(text).value;
}
domain::BookLevel lvl(const char *px, const char *qty) {
    return {dec(px), dec(qty), std::nullopt};
}
} // namespace

TEST_CASE("order book snapshot sorts sides and exposes best levels") {
    books::OrderBook book;
    domain::BookSnapshot s;
    s.bids = {lvl("99", "1"), lvl("100", "2"), lvl("98", "3")};
    s.asks = {lvl("102", "1"), lvl("101", "2")};
    book.apply_snapshot(s);
    REQUIRE(book.bids().size() == 3);
    CHECK(book.bids()[0].price == dec("100"));
    CHECK(book.bids()[2].price == dec("98"));
    CHECK(book.asks()[0].price == dec("101"));
    CHECK(book.best_bid()->quantity == dec("2"));
    CHECK(book.best_ask()->price == dec("101"));
    CHECK_FALSE(book.crossed());
}

TEST_CASE("order book levels insert, update and remove on zero") {
    books::OrderBook book;
    const std::vector<domain::BookLevel> bids{lvl("100", "1"), lvl("99", "1")};
    REQUIRE(book.apply_levels(bids, {}));
    const std::vector<domain::BookLevel> update{lvl("100", "5"), lvl("99", "0"), lvl("97", "0")};
    REQUIRE(book.apply_levels(update, {}));
    REQUIRE(book.bids().size() == 1);
    CHECK(book.bids()[0].quantity == dec("5"));
    const std::vector<domain::BookLevel> asks{lvl("100.5", "1")};
    REQUIRE(book.apply_levels({}, asks));
    CHECK_FALSE(book.crossed());
    const std::vector<domain::BookLevel> crossing{lvl("100", "1")};
    REQUIRE(book.apply_levels({}, crossing));
    CHECK(book.crossed());
    CHECK(book.asks()[0].price == dec("100")); // locked counts as crossed
}

TEST_CASE("order book level bound") {
    books::OrderBook book;
    std::vector<domain::BookLevel> many;
    for (std::size_t i = 1; i <= books::k_max_book_levels_per_side + 1; ++i) {
        many.push_back({domain::Decimal::from_parts(static_cast<std::int64_t>(i), 0).value,
                        dec("1"), std::nullopt});
    }
    CHECK_FALSE(book.apply_levels(many, {}));
    CHECK(book.bids().size() <= books::k_max_book_levels_per_side);
    domain::BookSnapshot s;
    s.asks = many;
    book.apply_snapshot(s);
    CHECK(book.asks().size() == books::k_max_book_levels_per_side);
    CHECK(book.asks()[0].price == dec("1")); // nearest levels kept
}
