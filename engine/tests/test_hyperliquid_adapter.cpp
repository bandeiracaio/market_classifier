#include "market_classifier/domain/decimal_math.hpp"
#include "market_classifier/venues/hyperliquid_adapter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

#include "fixture_util.hpp"

using namespace market_classifier;
using mc_test::load_frame;
using mc_test::load_payload;
using venues::StreamTag;

namespace {
domain::Decimal dec(const char *text) {
    return domain::Decimal::parse(text).value;
}
} // namespace

TEST_CASE("hyperliquid trades map to Trade list") {
    venues::HyperliquidAdapter adapter;
    const auto r =
        adapter.adapt(load_frame("hyperliquid/trades.json", StreamTag::HyperliquidWs, 9));
    REQUIRE(r.error == venues::AdapterError::None);
    REQUIRE(r.events.size() == 30);
    const auto &t = std::get<domain::Trade>(r.events[0]);
    CHECK(t.meta.instrument().venue() == domain::Venue::Hyperliquid);
    CHECK(t.meta.instrument().native_symbol() == "BTC");
    CHECK(t.meta.source_time().value == 1790488093562);
    CHECK(t.meta.receive_time().value == 9);
    CHECK(t.aggressor_side == domain::AggressorSide::Sell); // side "A"
    CHECK(t.price == dec("84502.0"));
    CHECK(t.quantity == dec("0.00303"));
    CHECK(t.usd_notional == domain::mul(dec("84502"), dec("0.00303")).value);
    CHECK(t.source_id == "390029514147266");
    CHECK(std::get<domain::Trade>(r.events[1]).aggressor_side == domain::AggressorSide::Buy);
    CHECK(std::get<domain::Trade>(r.events[29]).meta.local_sequence().value == 30);
    CHECK(r.binance_prev_final_update_ids.size() == 30);
}

TEST_CASE("hyperliquid l2Book maps to BookSnapshot") {
    venues::HyperliquidAdapter adapter;
    const auto r =
        adapter.adapt(load_frame("hyperliquid/l2Book.json", StreamTag::HyperliquidWs, 1));
    REQUIRE(r.error == venues::AdapterError::None);
    const auto &s = std::get<domain::BookSnapshot>(r.events.at(0));
    REQUIRE(s.bids.size() == 20);
    REQUIRE(s.asks.size() == 20);
    CHECK(s.bids[0].price == dec("84504.0"));
    CHECK(s.bids[0].quantity == dec("17.59827"));
    CHECK(s.bids[0].order_count == 75U);
    CHECK(s.asks[0].price == dec("84505.0"));
    CHECK(s.asks[0].order_count == 3U);
    CHECK_FALSE(s.source_sequence);
    CHECK(s.meta.source_time().value == 1790488126880);
}

TEST_CASE("hyperliquid bbo maps to Bbo") {
    venues::HyperliquidAdapter adapter;
    const auto r = adapter.adapt(load_frame("hyperliquid/bbo.json", StreamTag::HyperliquidWs, 1));
    REQUIRE(r.error == venues::AdapterError::None);
    const auto &b = std::get<domain::Bbo>(r.events.at(0));
    CHECK(b.bid_price == dec("84504.0"));
    CHECK(b.bid_quantity == dec("18.29092"));
    CHECK(b.ask_price == dec("84505.0"));
    CHECK(b.ask_quantity == dec("0.00401"));
    const bridge::RawFrame empty_side{
        StreamTag::HyperliquidWs, 1,
        R"({"channel":"bbo","data":{"coin":"BTC","time":1,"bbo":[null,{"px":"1","sz":"1","n":1}]}})"};
    CHECK(adapter.adapt(empty_side).error == venues::AdapterError::Ignored);
}

TEST_CASE("hyperliquid activeAssetCtx maps to metrics, open interest and summary") {
    venues::HyperliquidAdapter adapter;
    const auto r =
        adapter.adapt(load_frame("hyperliquid/activeAssetCtx.json", StreamTag::HyperliquidWs, 77));
    REQUIRE(r.error == venues::AdapterError::None);
    REQUIRE(r.events.size() == 3);
    const auto &m = std::get<domain::AssetMetrics>(r.events[0]);
    CHECK(m.mark_price == dec("84504.0"));
    CHECK(m.oracle_price == dec("84537.0"));
    CHECK_FALSE(m.index_price);
    CHECK(m.funding_rate == dec("0.0000125"));
    CHECK(m.meta.source_time().value == 77); // no venue timestamp: receive time
    const auto &oi = std::get<domain::OpenInterest>(r.events[1]);
    CHECK(oi.native_quantity == dec("37115.06004"));
    CHECK(oi.usd_notional == domain::mul(dec("37115.06004"), dec("84504")).value);
    CHECK(oi.sample_interval_ms == 0); // streamed
    const auto &s = std::get<domain::MarketSummary>(r.events[2]);
    CHECK(s.mid_price == dec("84504.5"));
    CHECK(s.volume_24h == dec("857060217.5145208836"));
    CHECK(s.change_24h == dec("577")); // markPx - prevDayPx
    CHECK(s.funding_rate == dec("0.0000125"));
    CHECK(s.open_interest == dec("37115.06004"));
}

TEST_CASE("hyperliquid candle maps to Candle") {
    venues::HyperliquidAdapter adapter;
    const auto r =
        adapter.adapt(load_frame("hyperliquid/candle.json", StreamTag::HyperliquidWs, 1));
    REQUIRE(r.error == venues::AdapterError::None);
    const auto &c = std::get<domain::Candle>(r.events.at(0));
    CHECK(c.interval_ms == 60'000);
    CHECK(c.open_time_ms == 1790488080000);
    CHECK(c.close_time_ms == 1790488139999);
    CHECK(c.open == dec("84503.0"));
    CHECK(c.close == dec("84505.0"));
    CHECK(c.base_volume == dec("0.16717"));
    CHECK(c.trade_count == 41U);
    CHECK_FALSE(c.quote_volume);
    // Live candle frames carry no closed flag; closed-ness is derived from time.
    CHECK_FALSE(c.closed);
}

TEST_CASE("hyperliquid candleSnapshot maps to candles, last one open") {
    venues::HyperliquidAdapter adapter;
    const auto r = adapter.adapt(
        load_payload("hyperliquid/candleSnapshot.json", StreamTag::HyperliquidCandleSnapshot, 1));
    REQUIRE(r.error == venues::AdapterError::None);
    REQUIRE(r.events.size() == 6);
    CHECK(std::get<domain::Candle>(r.events[0]).open_time_ms == 1790487780000);
    CHECK(std::get<domain::Candle>(r.events[0]).closed);
    CHECK_FALSE(std::get<domain::Candle>(r.events[5]).closed);
}

TEST_CASE("hyperliquid metaAndAssetCtxs maps to InstrumentDefinition") {
    venues::HyperliquidAdapter adapter;
    const auto r = adapter.adapt(
        load_payload("hyperliquid/metaAndAssetCtxs.json", StreamTag::HyperliquidMeta, 1));
    REQUIRE(r.error == venues::AdapterError::None);
    const auto &def = std::get<domain::InstrumentDefinition>(r.events.at(0));
    CHECK(def.quantity_step == dec("0.00001")); // szDecimals 5
    CHECK(def.tick_size == dec("1"));           // 5 significant figures at 84504
    CHECK(def.base_asset == "BTC");
    CHECK(def.quote_asset == "USDC");
    CHECK(def.active);
}

TEST_CASE("hyperliquid acks and pongs are ignored, bad frames rejected") {
    venues::HyperliquidAdapter adapter;
    for (std::size_t i = 0; i < 6; ++i) {
        const auto r = adapter.adapt(
            load_frame("hyperliquid/subscription_ack.json", StreamTag::HyperliquidWs, 1, i));
        CHECK(r.error == venues::AdapterError::Ignored);
        CHECK(r.events.empty());
    }
    const bridge::RawFrame eth{
        StreamTag::HyperliquidWs, 1,
        R"({"channel":"trades","data":[{"coin":"ETH","side":"B","px":"1","sz":"1","time":1,"tid":1}]})"};
    CHECK(adapter.adapt(eth).error == venues::AdapterError::WrongSymbol);
    const bridge::RawFrame bad_side{
        StreamTag::HyperliquidWs, 1,
        R"({"channel":"trades","data":[{"coin":"BTC","side":"X","px":"1","sz":"1","time":1,"tid":1}]})"};
    CHECK(adapter.adapt(bad_side).error == venues::AdapterError::Malformed);
    const bridge::RawFrame unknown{StreamTag::HyperliquidWs, 1,
                                   R"({"channel":"orderUpdates","data":[]})"};
    CHECK(adapter.adapt(unknown).error == venues::AdapterError::UnknownStream);
    const bridge::RawFrame truncated{StreamTag::HyperliquidWs, 1, R"({"channel":"trades","da)"};
    CHECK(adapter.adapt(truncated).error == venues::AdapterError::Malformed);
    const bridge::RawFrame zero_price{
        StreamTag::HyperliquidWs, 1,
        R"({"channel":"trades","data":[{"coin":"BTC","side":"B","px":"0","sz":"1","time":1,"tid":1}]})"};
    CHECK(adapter.adapt(zero_price).error == venues::AdapterError::OutOfBounds);
}
