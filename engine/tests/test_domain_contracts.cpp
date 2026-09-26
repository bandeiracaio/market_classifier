#include "market_classifier/domain/events.hpp"
#include "market_classifier/domain/types.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <variant>

namespace domain = market_classifier::domain;

TEST_CASE("instrument identity validates venue and native symbol", "[domain][identity]") {
    using enum domain::InstrumentIdError;

    const auto binance = domain::InstrumentId::create(domain::Venue::BinanceUsdM, "BTCUSDT");
    REQUIRE(binance);
    CHECK(binance.value.venue() == domain::Venue::BinanceUsdM);
    CHECK(binance.value.native_symbol() == "BTCUSDT");

    CHECK(domain::InstrumentId::create(domain::Venue::Hyperliquid, "BTC").error == None);
    CHECK(domain::InstrumentId::create(static_cast<domain::Venue>(255), "BTC").error ==
          InvalidVenue);
    CHECK(domain::InstrumentId::create(domain::Venue::BinanceUsdM, "").error == EmptySymbol);
    CHECK(domain::InstrumentId::create(domain::Venue::BinanceUsdM, "BTC USDT").error ==
          InvalidSymbol);
    CHECK(domain::InstrumentId::create(domain::Venue::BinanceUsdM, "BTC\nUSDT").error ==
          InvalidSymbol);

    const std::string oversized(domain::InstrumentId::k_max_symbol_length + 1, 'A');
    CHECK(domain::InstrumentId::create(domain::Venue::BinanceUsdM, oversized).error ==
          SymbolTooLong);
}

TEST_CASE("instrument identity ordering always includes venue", "[domain][identity]") {
    const auto binance_btc =
        domain::InstrumentId::create(domain::Venue::BinanceUsdM, "BTCUSDT").value;
    const auto binance_eth =
        domain::InstrumentId::create(domain::Venue::BinanceUsdM, "ETHUSDT").value;
    const auto hyperliquid_btc =
        domain::InstrumentId::create(domain::Venue::Hyperliquid, "BTCUSDT").value;

    CHECK(binance_btc == binance_btc);
    CHECK(binance_btc != hyperliquid_btc);
    CHECK(binance_btc < binance_eth);
    CHECK(binance_eth < hyperliquid_btc);
}

TEST_CASE("event metadata keeps clocks and local sequence distinct", "[domain][metadata]") {
    using enum domain::EventMetaError;

    const auto instrument =
        domain::InstrumentId::create(domain::Venue::BinanceUsdM, "BTCUSDT").value;
    const auto result = domain::EventMeta::create(
        instrument, domain::SourceTimeMs{1000}, domain::ReceiveTimeMs{1015},
        domain::LocalSequence{42}, domain::DataQuality::Live);
    REQUIRE(result);
    CHECK(result.value.source_time().value == 1000);
    CHECK(result.value.receive_time().value == 1015);
    CHECK(result.value.local_sequence().value == 42);

    CHECK(domain::EventMeta::create(instrument, domain::SourceTimeMs{-1}, domain::ReceiveTimeMs{0},
                                    domain::LocalSequence{1}, domain::DataQuality::Live)
              .error == InvalidSourceTime);
    CHECK(domain::EventMeta::create(instrument, domain::SourceTimeMs{0}, domain::ReceiveTimeMs{-1},
                                    domain::LocalSequence{1}, domain::DataQuality::Live)
              .error == InvalidReceiveTime);
    CHECK(domain::EventMeta::create(instrument, domain::SourceTimeMs{0}, domain::ReceiveTimeMs{0},
                                    domain::LocalSequence{0}, domain::DataQuality::Live)
              .error == InvalidLocalSequence);
    CHECK(domain::EventMeta::create(instrument, domain::SourceTimeMs{0}, domain::ReceiveTimeMs{0},
                                    domain::LocalSequence{1}, static_cast<domain::DataQuality>(255))
              .error == InvalidQuality);
}

TEST_CASE("data quality states and transitions remain explicit", "[domain][quality]") {
    constexpr std::array qualities{
        domain::DataQuality::Live,        domain::DataQuality::Delayed,
        domain::DataQuality::Stale,       domain::DataQuality::Reconnecting,
        domain::DataQuality::GapDetected, domain::DataQuality::Partial,
        domain::DataQuality::Unsupported, domain::DataQuality::Failed,
    };
    for (const auto quality : qualities) {
        CHECK(domain::is_valid(quality));
    }
    CHECK_FALSE(domain::is_valid(static_cast<domain::DataQuality>(255)));

    CHECK(domain::can_transition(domain::DataQuality::Live, domain::DataQuality::Stale));
    CHECK(domain::can_transition(domain::DataQuality::Stale, domain::DataQuality::Reconnecting));
    CHECK(domain::can_transition(domain::DataQuality::Reconnecting, domain::DataQuality::Live));
    CHECK(domain::can_transition(domain::DataQuality::GapDetected, domain::DataQuality::Partial));
    CHECK_FALSE(
        domain::can_transition(domain::DataQuality::GapDetected, domain::DataQuality::Live));
    CHECK_FALSE(
        domain::can_transition(domain::DataQuality::Unsupported, domain::DataQuality::Live));
    CHECK(
        domain::can_transition(domain::DataQuality::Unsupported, domain::DataQuality::Unsupported));
    CHECK_FALSE(
        domain::can_transition(static_cast<domain::DataQuality>(255), domain::DataQuality::Failed));
}

TEST_CASE("all normalized event contracts share explicit metadata", "[domain][events]") {
    const auto instrument = domain::InstrumentId::create(domain::Venue::Hyperliquid, "BTC").value;
    const auto meta       = domain::EventMeta::create(instrument, domain::SourceTimeMs{1000},
                                                      domain::ReceiveTimeMs{1001},
                                                      domain::LocalSequence{1}, domain::DataQuality::Live)
                          .value;
    const auto one = domain::Decimal::parse("1").value;

    const std::array<domain::NormalizedEvent, 11> events{
        domain::InstrumentDefinition{meta, one, one, domain::ContractKind::LinearPerpetual, "BTC",
                                     "USD", true, std::nullopt},
        domain::MarketSummary{meta, one, one, one, one, std::nullopt, std::nullopt},
        domain::Trade{meta, "trade-1", domain::AggressorSide::Buy, one, one, one},
        domain::BookSnapshot{
            meta, {{one, one, std::nullopt}}, {{one, one, std::nullopt}}, 10, "cursor"},
        domain::BookDelta{meta, {{one, one, std::nullopt}}, {}, 11, 12},
        domain::Bbo{meta, one, one, one, one},
        domain::AssetMetrics{meta, one, one, one, one, std::nullopt, std::nullopt},
        domain::OpenInterest{meta, one, one, 1000},
        domain::Candle{meta, 60'000, 0, 59'999, one, one, one, one, one, one, 1, true},
        domain::Liquidation{meta, domain::LiquidationSide::Long, one, one, one, "liq-1",
                            domain::LiquidationMethod::VenueReported},
        domain::FeedStatus{meta, domain::FeedLifecycle::Open, 0, 0, domain::GapStatus::None,
                           domain::FeedError::None},
    };

    for (const auto &event : events) {
        std::visit([&meta](const auto &value) { CHECK(value.meta == meta); }, event);
    }
}
