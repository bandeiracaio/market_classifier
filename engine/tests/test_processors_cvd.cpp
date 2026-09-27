#include "market_classifier/processors/cvd.hpp"

#include <catch2/catch_test_macros.hpp>

#include "processor_util.hpp"

using namespace market_classifier;
using domain::AggressorSide;
using mc_test::dec;
using mc_test::trade;

TEST_CASE("cvd adds buys, subtracts sells, ignores unknown") {
    processors::Cvd cvd;
    cvd.on_trade(trade(1'000, "100", "1.5", AggressorSide::Buy));
    CHECK(cvd.value() == dec("1.5"));
    cvd.on_trade(trade(2'000, "100", "0.5", AggressorSide::Sell));
    CHECK(cvd.value() == dec("1.0"));
    cvd.on_trade(trade(3'000, "100", "9", AggressorSide::Unknown));
    CHECK(cvd.value() == dec("1.0"));
    CHECK(cvd.unknown_side_trades() == 1);
    REQUIRE(cvd.series().size() == 1); // all in minute 0
    CHECK(cvd.series().back().value == dec("1"));
    CHECK(cvd.series().back().minute_ms == 0);
}

TEST_CASE("cvd series has one point per minute with the minute close") {
    processors::Cvd cvd;
    cvd.on_trade(trade(59'999, "1", "1", AggressorSide::Buy));
    cvd.on_trade(trade(60'000, "1", "2", AggressorSide::Buy));
    cvd.on_trade(trade(61'000, "1", "1", AggressorSide::Sell));
    REQUIRE(cvd.series().size() == 2);
    CHECK(cvd.series()[0].value == dec("1"));
    CHECK(cvd.series()[1].minute_ms == 60'000);
    CHECK(cvd.series()[1].value == dec("2"));
}

TEST_CASE("cvd daily reset at 00:00 UTC by source time") {
    processors::Cvd cvd;
    cvd.set_daily_reset(true);
    const std::int64_t day = 86'400'000;
    cvd.on_trade(trade(3 * day - 1, "1", "5", AggressorSide::Buy));
    CHECK(cvd.value() == dec("5"));
    cvd.on_trade(trade(3 * day, "1", "2", AggressorSide::Sell));
    CHECK(cvd.value() == dec("-2"));
    CHECK(cvd.series().back().reset);
    processors::Cvd no_reset;
    no_reset.on_trade(trade(3 * day - 1, "1", "5", AggressorSide::Buy));
    no_reset.on_trade(trade(3 * day, "1", "2", AggressorSide::Sell));
    CHECK(no_reset.value() == dec("3"));
}

TEST_CASE("cvd marks gaps instead of silently continuing") {
    processors::Cvd cvd;
    cvd.on_trade(trade(1'000, "1", "1", AggressorSide::Buy));
    cvd.mark_gap(125'000);
    REQUIRE(cvd.series().size() == 2);
    CHECK(cvd.series().back().gap);
    CHECK(cvd.series().back().minute_ms == 120'000);
    cvd.on_trade(trade(126'000, "1", "1", AggressorSide::Buy));
    CHECK(cvd.series().back().gap); // same minute keeps the marker
    CHECK(cvd.gap_count() == 1);
}

TEST_CASE("cvd series is bounded to 24h of minutes") {
    processors::Cvd cvd;
    for (std::int64_t m = 0; m < 2000; ++m) {
        cvd.on_trade(trade(m * 60'000, "1", "1", AggressorSide::Buy));
    }
    CHECK(cvd.series().size() == processors::k_series_minutes);
}
