#include "market_classifier/processors/trades.hpp"

#include <catch2/catch_test_macros.hpp>

#include "processor_util.hpp"

using namespace market_classifier;
using mc_test::trade;

TEST_CASE("tape retains the newest 5000 trades") {
    processors::TradeTape tape;
    for (std::int64_t i = 0; i < 5001; ++i) {
        tape.on_trade(trade(i, "100", "1", domain::AggressorSide::Buy));
    }
    REQUIRE(tape.trades().size() == processors::k_tape_capacity);
    CHECK(tape.trades()[0].meta.source_time().value == 1); // oldest dropped
    CHECK(tape.trades().back().meta.source_time().value == 5000);
    CHECK(tape.last_update_ms() == 5000);
}
