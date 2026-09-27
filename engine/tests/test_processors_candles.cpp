#include "market_classifier/processors/candles.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "processor_util.hpp"

using namespace market_classifier;
using mc_test::candle;
using mc_test::dec;

TEST_CASE("candles preload then live update replaces or appends") {
    processors::CandleSeries series;
    std::vector<domain::Candle> preload;
    for (std::int64_t i = 0; i < 1000; ++i) {
        preload.push_back(candle(i * 60'000, "100"));
    }
    series.preload(preload);
    REQUIRE(series.candles(60'000).size() == 1000);
    series.on_candle(candle(999 * 60'000, "101"));
    REQUIRE(series.candles(60'000).size() == 1000);
    CHECK(series.candles(60'000).back().close == dec("101"));
    series.on_candle(candle(1000 * 60'000, "102"));
    CHECK(series.candles(60'000).size() == 1001);
    CHECK(series.candles(300'000).empty());
    // A stale older bar within range replaces in place; unknown old bars are ignored.
    series.on_candle(candle(10 * 60'000, "55"));
    CHECK(series.candles(60'000)[10].close == dec("55"));
}

TEST_CASE("candle capacity 2000 holds per interval") {
    processors::CandleSeries series;
    for (std::int64_t i = 0; i < 2500; ++i) {
        series.on_candle(candle(i * 60'000, "100"));
    }
    REQUIRE(series.candles(60'000).size() == processors::k_candle_capacity);
    CHECK(series.candles(60'000).front().open_time_ms == 500 * 60'000);
}

TEST_CASE("candles with unsupported interval are rejected") {
    processors::CandleSeries series;
    series.on_candle(candle(0, "1", 120'000));
    CHECK(series.rejected() == 1);
    CHECK(series.candles(120'000).empty());
}
