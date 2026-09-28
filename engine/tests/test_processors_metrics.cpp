#include "market_classifier/processors/metrics.hpp"

#include <catch2/catch_test_macros.hpp>

#include "processor_util.hpp"

using namespace market_classifier;
using mc_test::dec;
using mc_test::meta;

namespace {

domain::Bbo bbo(std::int64_t t, const char *bid, const char *ask,
                domain::Venue v = domain::Venue::BinanceUsdM) {
    domain::Bbo b{};
    b.meta         = meta(t, v);
    b.bid_price    = dec(bid);
    b.ask_price    = dec(ask);
    b.bid_quantity = dec("1");
    b.ask_quantity = dec("1");
    return b;
}

} // namespace

TEST_CASE("metrics keeps latest values and per-minute series") {
    processors::Metrics m;
    domain::AssetMetrics a{};
    a.meta         = meta(1'000);
    a.mark_price   = dec("100");
    a.funding_rate = dec("0.0001");
    m.on_asset_metrics(a);
    domain::OpenInterest oi{};
    oi.meta               = meta(2'000);
    oi.native_quantity    = dec("10");
    oi.sample_interval_ms = 10'000;
    m.on_open_interest(oi);
    oi.meta            = meta(62'000);
    oi.native_quantity = dec("12");
    m.on_open_interest(oi);
    const auto &v = m.view();
    REQUIRE(v.asset);
    CHECK(v.asset->mark_price == dec("100"));
    REQUIRE(v.open_interest.size() == 2);
    CHECK(v.open_interest.back().value == dec("12"));
    CHECK(v.oi_sample_interval_ms == 10'000);
    REQUIRE(v.funding.size() == 1);
    CHECK(v.last_update_ms == 62'000);
}

TEST_CASE("bbo spread series samples once per second") {
    processors::Metrics m;
    m.on_bbo(bbo(1'000, "100.0", "100.2"));
    m.on_bbo(bbo(1'500, "100.0", "100.1"));
    m.on_bbo(bbo(2'000, "100.0", "100.3"));
    const auto &v = m.view();
    REQUIRE(v.spread.size() == 2);
    CHECK(v.spread[0].spread == dec("0.1")); // latest in second 1
    CHECK(v.spread[1].spread == dec("0.3"));
    REQUIRE(v.mid);
    CHECK(*v.mid == dec("100.15"));
}

TEST_CASE("cross-venue basis is exact with bps rounded half-even") {
    processors::Metrics binance;
    processors::Metrics hyperliquid;
    CHECK_FALSE(processors::cross_venue_basis(binance, hyperliquid));
    binance.on_bbo(bbo(1'000, "84500.0", "84500.2"));
    hyperliquid.on_bbo(bbo(1'000, "84490", "84492", domain::Venue::Hyperliquid));
    const auto b = processors::cross_venue_basis(binance, hyperliquid);
    REQUIRE(b);
    CHECK(b->binance_mid == dec("84500.1"));
    CHECK(b->hyperliquid_mid == dec("84491"));
    CHECK(b->basis == dec("9.1"));
    // 9.1 / 84491 * 10000 = 1.07703... -> 1.08
    CHECK(b->basis_bps == 1.08);
    CHECK(processors::round_half_even_2dp(0.125) == 0.12);
    CHECK(processors::round_half_even_2dp(0.135) == 0.14);
    CHECK(processors::round_half_even_2dp(-1.005) == -1.0);
}

TEST_CASE("funding annualization per venue cadence") {
    CHECK(processors::annualized_funding(dec("0.0001"), domain::Venue::BinanceUsdM) ==
          dec("0.1095")); // 3 per day * 365
    CHECK(processors::annualized_funding(dec("0.0000125"), domain::Venue::Hyperliquid) ==
          dec("0.1095")); // 24 per day * 365
    CHECK(processors::next_hourly_funding_ms(3'600'000 + 5) == 7'200'000);
}
