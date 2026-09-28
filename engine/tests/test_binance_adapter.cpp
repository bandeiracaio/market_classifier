#include "market_classifier/domain/decimal_math.hpp"
#include "market_classifier/runtime/ingress.hpp"
#include "market_classifier/venues/binance_adapter.hpp"
#include "market_classifier/venues/instruments.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

#include "fixture_util.hpp"

using namespace market_classifier;
using mc_test::load_frame;
using mc_test::load_malformed;
using mc_test::load_payload;
using venues::StreamTag;

namespace {
domain::Decimal dec(const char *text) {
    return domain::Decimal::parse(text).value;
}
} // namespace

TEST_CASE("binance aggTrade maps to Trade") {
    venues::BinanceAdapter adapter;
    const auto r = adapter.adapt(load_frame("binance/aggTrade.json", StreamTag::BinanceWs, 42));
    REQUIRE(r.error == venues::AdapterError::None);
    REQUIRE(r.events.size() == 1);
    const auto &t = std::get<domain::Trade>(r.events[0]);
    CHECK(t.meta.instrument().native_symbol() == "BTCUSDT");
    CHECK(t.meta.instrument().venue() == domain::Venue::BinanceUsdM);
    CHECK(t.meta.receive_time().value == 42);
    CHECK(t.meta.source_time().value == 1790488171500);
    CHECK(t.meta.local_sequence().value == 1);
    CHECK(t.meta.quality() == domain::DataQuality::Live);
    CHECK(t.source_id == "3466455773");
    CHECK(t.price == dec("84499.50"));
    CHECK(t.quantity == dec("0.001"));
    CHECK(t.usd_notional == dec("84.4995"));
    CHECK(t.aggressor_side == domain::AggressorSide::Sell); // m == true
    CHECK(r.binance_prev_final_update_ids == std::vector<std::uint64_t>{0});

    const auto second =
        adapter.adapt(load_frame("binance/aggTrade.json", StreamTag::BinanceWs, 43, 1));
    REQUIRE(second.error == venues::AdapterError::None);
    CHECK(std::get<domain::Trade>(second.events[0]).meta.local_sequence().value == 2);
}

TEST_CASE("binance depthUpdate maps to BookDelta with pu side channel") {
    venues::BinanceAdapter adapter;
    const auto r = adapter.adapt(load_frame("binance/depth_diff.json", StreamTag::BinanceWs, 1));
    REQUIRE(r.error == venues::AdapterError::None);
    REQUIRE(r.events.size() == 1);
    const auto &d = std::get<domain::BookDelta>(r.events[0]);
    CHECK(d.first_source_sequence == 11668410507050ULL);
    CHECK(d.last_source_sequence == 11668410517613ULL);
    CHECK(r.binance_prev_final_update_ids == std::vector<std::uint64_t>{11668410506992ULL});
    CHECK(d.meta.source_time().value == 1790488147621);
    REQUIRE(d.changed_bids.size() == 14);
    CHECK(d.changed_bids[0].price == dec("1000.00"));
    CHECK(d.changed_bids[0].quantity == dec("139.491"));
    CHECK(d.changed_asks[0].price == dec("84518.10"));
    CHECK_FALSE(d.changed_asks[0].order_count);
}

TEST_CASE("binance bookTicker maps to Bbo") {
    venues::BinanceAdapter adapter;
    const auto r = adapter.adapt(load_frame("binance/bookTicker.json", StreamTag::BinanceWs, 1));
    REQUIRE(r.error == venues::AdapterError::None);
    const auto &b = std::get<domain::Bbo>(r.events.at(0));
    CHECK(b.bid_price == dec("84499.50"));
    CHECK(b.bid_quantity == dec("2.788"));
    CHECK(b.ask_price == dec("84499.60"));
    CHECK(b.ask_quantity == dec("6.375"));
    CHECK(b.meta.source_time().value == 1790488147542);
}

TEST_CASE("binance markPriceUpdate maps to AssetMetrics") {
    venues::BinanceAdapter adapter;
    const auto r = adapter.adapt(load_frame("binance/markPrice.json", StreamTag::BinanceWs, 1));
    REQUIRE(r.error == venues::AdapterError::None);
    const auto &m = std::get<domain::AssetMetrics>(r.events.at(0));
    CHECK(m.mark_price == dec("84499.55705797"));
    CHECK(m.index_price == dec("84535.61304348"));
    CHECK_FALSE(m.oracle_price);
    CHECK(m.funding_rate == dec("0.00003749"));
    CHECK(m.next_funding_time_ms == 1790496000000);
    CHECK(m.meta.source_time().value == 1790488171000);
}

TEST_CASE("binance 24hrTicker maps to MarketSummary") {
    venues::BinanceAdapter adapter;
    const auto r = adapter.adapt(load_frame("binance/ticker.json", StreamTag::BinanceWs, 1));
    REQUIRE(r.error == venues::AdapterError::None);
    const auto &s = std::get<domain::MarketSummary>(r.events.at(0));
    CHECK(s.last_price == dec("84499.60"));
    CHECK(s.change_24h == dec("555.00"));
    CHECK(s.volume_24h == dec("3001345307.23")); // quote (USD) volume
    CHECK_FALSE(s.open_interest);
}

TEST_CASE("binance kline maps to Candle") {
    venues::BinanceAdapter adapter;
    const auto r = adapter.adapt(load_frame("binance/kline.json", StreamTag::BinanceWs, 1));
    REQUIRE(r.error == venues::AdapterError::None);
    const auto &c = std::get<domain::Candle>(r.events.at(0));
    CHECK(c.interval_ms == 60'000);
    CHECK(c.open_time_ms == 1790488140000);
    CHECK(c.close_time_ms == 1790488199999);
    CHECK(c.open == dec("84499.50"));
    CHECK(c.high == dec("84499.60"));
    CHECK(c.low == dec("84499.50"));
    CHECK(c.close == dec("84499.50"));
    CHECK(c.base_volume == dec("4.373"));
    CHECK(c.quote_volume == dec("369516.42220"));
    CHECK(c.trade_count == 169U);
    CHECK_FALSE(c.closed);
}

TEST_CASE("binance forceOrder maps to Liquidation") {
    venues::BinanceAdapter adapter;
    const auto r = adapter.adapt(load_frame("binance/forceOrder.json", StreamTag::BinanceWs, 1));
    REQUIRE(r.error == venues::AdapterError::None);
    const auto &l = std::get<domain::Liquidation>(r.events.at(0));
    CHECK(l.side == domain::LiquidationSide::Short); // S == BUY closes a short
    CHECK(l.price == dec("84439.90"));               // average fill price
    CHECK(l.quantity == dec("0.057"));
    CHECK(l.notional == domain::mul(dec("84439.90"), dec("0.057")).value);
    CHECK(l.method == domain::LiquidationMethod::ForceOrder);
    CHECK(l.meta.source_time().value == 1790488548221);
}

TEST_CASE("binance REST depth maps to BookSnapshot") {
    venues::BinanceAdapter adapter;
    const auto r = adapter.adapt(
        load_payload("binance/depth_snapshot.json", StreamTag::BinanceDepthSnapshot, 7));
    REQUIRE(r.error == venues::AdapterError::None);
    const auto &s = std::get<domain::BookSnapshot>(r.events.at(0));
    CHECK(s.source_sequence == 11668410750572ULL);
    REQUIRE(s.bids.size() == 50);
    REQUIRE(s.asks.size() == 50);
    CHECK(s.bids[0].price == dec("84499.50"));
    CHECK(s.bids[0].quantity == dec("5.738"));
}

TEST_CASE("binance REST klines map to candles, last one open") {
    venues::BinanceAdapter adapter;
    const auto r =
        adapter.adapt(load_payload("binance/klines_rest.json", StreamTag::BinanceKlinesRest, 7));
    REQUIRE(r.error == venues::AdapterError::None);
    REQUIRE(r.events.size() == 5);
    const auto &first = std::get<domain::Candle>(r.events[0]);
    CHECK(first.interval_ms == 60'000);
    CHECK(first.open_time_ms == 1790487840000);
    CHECK(first.open == dec("84494.20"));
    CHECK(first.close == dec("84511.40"));
    CHECK(first.base_volume == dec("25.659"));
    CHECK(first.trade_count == 632U);
    CHECK(first.closed);
    const auto &last = std::get<domain::Candle>(r.events[4]);
    CHECK(last.open_time_ms == 1790488080000);
    CHECK(last.close == dec("84499.50"));
    CHECK_FALSE(last.closed);
}

TEST_CASE("binance REST openInterest maps with poll cadence") {
    venues::BinanceAdapter adapter;
    const auto r = adapter.adapt(
        load_payload("binance/openInterest.json", StreamTag::BinanceOpenInterestRest, 7));
    REQUIRE(r.error == venues::AdapterError::None);
    const auto &oi = std::get<domain::OpenInterest>(r.events.at(0));
    CHECK(oi.native_quantity == dec("94034.253"));
    CHECK_FALSE(oi.usd_notional);
    CHECK(oi.sample_interval_ms == venues::k_binance_oi_poll_ms);
    CHECK(oi.meta.source_time().value == 1790488140644);
}

TEST_CASE("binance exchangeInfo maps to InstrumentDefinition") {
    venues::BinanceAdapter adapter;
    const auto r =
        adapter.adapt(load_payload("binance/exchangeInfo.json", StreamTag::BinanceExchangeInfo, 7));
    REQUIRE(r.error == venues::AdapterError::None);
    const auto &def = std::get<domain::InstrumentDefinition>(r.events.at(0));
    CHECK(def.tick_size == dec("0.1"));
    CHECK(def.quantity_step == dec("0.001"));
    CHECK(def.contract_kind == domain::ContractKind::LinearPerpetual);
    CHECK(def.base_asset == "BTC");
    CHECK(def.quote_asset == "USDT");
    CHECK(def.active);
}

TEST_CASE("adapters reject malformed input without throwing") {
    CHECK(venues::BinanceAdapter{}.adapt(load_malformed("wrong_symbol")).error ==
          venues::AdapterError::WrongSymbol);
    for (auto name : {"bad_price", "long_decimal", "missing_field", "truncated"}) {
        INFO(name);
        const auto r = venues::BinanceAdapter{}.adapt(load_malformed(name));
        CHECK(r.error == venues::AdapterError::Malformed);
        CHECK(r.events.empty());
    }
    const bridge::RawFrame huge{StreamTag::BinanceWs, 1,
                                std::string(bridge::k_max_raw_frame_bytes + 1, ' ')};
    CHECK(venues::BinanceAdapter{}.adapt(huge).error == venues::AdapterError::Malformed);
    const bridge::RawFrame unknown{StreamTag::BinanceWs, 1,
                                   R"({"stream":"btcusdt@depth5","data":{"s":"BTCUSDT"}})"};
    CHECK(venues::BinanceAdapter{}.adapt(unknown).error == venues::AdapterError::UnknownStream);
    const bridge::RawFrame wrong_venue{StreamTag::HyperliquidWs, 1, "{}"};
    CHECK(venues::BinanceAdapter{}.adapt(wrong_venue).error == venues::AdapterError::UnknownStream);
}

TEST_CASE("binance adapter rejects out-of-bounds values") {
    const auto frame = [](const std::string &price, const std::string &qty) {
        return bridge::RawFrame{
            StreamTag::BinanceWs, 1,
            R"({"stream":"btcusdt@aggTrade","data":{"e":"aggTrade","s":"BTCUSDT","a":1,"p":")" +
                price + R"(","q":")" + qty + R"(","T":1,"m":false}})"};
    };
    CHECK(venues::BinanceAdapter{}.adapt(frame("0", "1")).error ==
          venues::AdapterError::OutOfBounds);
    CHECK(venues::BinanceAdapter{}.adapt(frame("1", "-1")).error ==
          venues::AdapterError::OutOfBounds);
    CHECK(venues::BinanceAdapter{}.adapt(frame("1", "1")).error == venues::AdapterError::None);

    std::string levels;
    for (std::size_t i = 0; i <= runtime::k_max_levels_per_event; ++i) {
        levels += (i == 0 ? "" : ",");
        levels += R"(["1","1"])";
    }
    const bridge::RawFrame many{
        StreamTag::BinanceWs, 1,
        R"({"stream":"btcusdt@depth@100ms","data":{"e":"depthUpdate","s":"BTCUSDT","T":1,"U":1,"u":2,"pu":0,"b":[)" +
            levels + R"(],"a":[]}})"};
    CHECK(venues::BinanceAdapter{}.adapt(many).error == venues::AdapterError::OutOfBounds);
}

TEST_CASE("hyperliquid liquidation capability is unsupported") {
    CHECK_FALSE(venues::supports_liquidations(domain::Venue::Hyperliquid));
    CHECK(venues::supports_liquidations(domain::Venue::BinanceUsdM));
}
