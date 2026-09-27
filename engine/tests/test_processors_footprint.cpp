#include "market_classifier/processors/footprint.hpp"

#include <catch2/catch_test_macros.hpp>

#include "processor_util.hpp"

using namespace market_classifier;
using domain::AggressorSide;
using mc_test::dec;
using mc_test::trade;

TEST_CASE("footprint buckets trades by floored price") {
    processors::Footprint fp(dec("5"));
    fp.on_trade(trade(1'000, "63002.4", "1", AggressorSide::Buy));
    fp.on_trade(trade(2'000, "63004.9", "2", AggressorSide::Sell));
    REQUIRE(fp.candles().size() == 1);
    const auto &c = fp.candles()[0];
    REQUIRE(c.cells.size() == 1);
    CHECK(c.cells[0].price == dec("63000"));
    CHECK(c.cells[0].bid_volume == dec("2")); // sells hit the bid
    CHECK(c.cells[0].ask_volume == dec("1")); // buys lift the ask
    CHECK(c.open_time_ms == 0);
}

TEST_CASE("footprint new interval starts new candle") {
    processors::Footprint fp(dec("5"));
    fp.on_trade(trade(1'000, "100", "1", AggressorSide::Buy));
    fp.on_trade(trade(61'000, "100", "1", AggressorSide::Buy));
    REQUIRE(fp.candles().size() == 2);
    CHECK(fp.candles()[1].open_time_ms == 60'000);
    fp.set_interval(300'000);
    CHECK(fp.candles().empty());
    fp.on_trade(trade(301'000, "100", "1", AggressorSide::Buy));
    CHECK(fp.candles()[0].open_time_ms == 300'000);
}

TEST_CASE("session profile tracks POC as max total bucket") {
    processors::Footprint fp(dec("5"));
    fp.on_trade(trade(1'000, "100", "1", AggressorSide::Buy));
    fp.on_trade(trade(2'000, "106", "1", AggressorSide::Buy));
    fp.on_trade(trade(3'000, "107", "1.5", AggressorSide::Sell));
    const auto &p = fp.session_profile();
    REQUIRE(p.buckets().size() == 2);
    REQUIRE(p.poc());
    CHECK(*p.poc() == dec("105"));
    CHECK(p.buckets()[1].bid_volume == dec("1.5"));
}

TEST_CASE("footprint aggregates to coarser buckets and intervals exactly") {
    processors::Footprint fp(dec("1"));
    fp.on_trade(trade(1'000, "63001.5", "1", AggressorSide::Buy));
    fp.on_trade(trade(61'000, "63004.2", "2", AggressorSide::Sell));
    fp.on_trade(trade(121'000, "63010", "3", AggressorSide::Buy));
    const auto agg = processors::aggregate(fp.candles(), dec("5"), 300'000, 10);
    REQUIRE(agg.size() == 1);
    REQUIRE(agg[0].cells.size() == 2);
    CHECK(agg[0].cells[0].price == dec("63000"));
    CHECK(agg[0].cells[0].ask_volume == dec("1"));
    CHECK(agg[0].cells[0].bid_volume == dec("2"));
    CHECK(agg[0].cells[1].price == dec("63010"));
    const auto profile = processors::aggregate(fp.session_profile(), dec("10"));
    REQUIRE(profile.buckets().size() == 2);
    CHECK(*profile.poc() == dec("63000"));
}

TEST_CASE("footprint marks gaps and bounds its storage") {
    processors::Footprint fp(dec("5"));
    fp.on_trade(trade(1'000, "100", "1", AggressorSide::Buy));
    fp.mark_gap(2'000);
    CHECK(fp.candles().back().gap);
    for (std::int64_t m = 0; m < 2000; ++m) {
        fp.on_trade(trade(m * 60'000, "100", "1", AggressorSide::Buy));
    }
    CHECK(fp.candles().size() == processors::k_series_minutes);
    // Cell bound per candle.
    processors::Footprint wide(dec("1"));
    for (std::size_t i = 0; i < processors::k_max_footprint_cells + 5; ++i) {
        wide.on_trade(trade(1'000, std::to_string(1000 + i).c_str(), "1", AggressorSide::Buy));
    }
    CHECK(wide.candles()[0].cells.size() == processors::k_max_footprint_cells);
    CHECK(wide.dropped_cells() == 5);
}
