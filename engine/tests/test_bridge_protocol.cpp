#include "market_classifier/bridge/protocol.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace bridge = market_classifier::bridge;
namespace domain = market_classifier::domain;

TEST_CASE("bridge protocol constants are bounded and versioned", "[bridge]") {
    CHECK(bridge::k_protocol_version == 1);
    CHECK(bridge::k_header_bytes == 16);
    CHECK(bridge::k_max_message_bytes == 64 * 1024);
    CHECK(bridge::k_max_events_per_batch == 256);
}

TEST_CASE("bridge decoder rejects malformed envelopes before payload decoding", "[bridge]") {
    CHECK(bridge::decode({}).error == bridge::DecodeError::Truncated);

    std::vector<std::uint8_t> header(bridge::k_header_bytes);
    CHECK(bridge::decode(header).error == bridge::DecodeError::InvalidMagic);
}

namespace {

domain::Trade sample_trade() {
    const auto instrument =
        domain::InstrumentId::create(domain::Venue::BinanceUsdM, "BTCUSDT").value;
    const auto meta =
        domain::EventMeta::create(instrument, domain::SourceTimeMs{1'700'000'000'000},
                                  domain::ReceiveTimeMs{1'700'000'000'007},
                                  domain::LocalSequence{42}, domain::DataQuality::Live)
            .value;
    return {meta,
            "trade-123",
            domain::AggressorSide::Buy,
            domain::Decimal::parse("42000.25").value,
            domain::Decimal::parse("0.125").value,
            domain::Decimal::parse("5250.03125").value};
}

void set_u16(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset]     = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

std::string as_hex(std::span<const std::uint8_t> bytes) {
    constexpr char k_digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        result.push_back(k_digits[byte >> 4U]);
        result.push_back(k_digits[byte & 0x0FU]);
    }
    return result;
}

} // namespace

TEST_CASE("trade batch round trips exact normalized values", "[bridge]") {
    const auto original = sample_trade();
    const auto bytes    = bridge::encode_trade_batch(std::span{&original, 1U});
    REQUIRE_FALSE(bytes.empty());
    const auto decoded = bridge::decode(bytes);
    REQUIRE(decoded);
    REQUIRE(decoded.trades.size() == 1);
    const auto &trade = decoded.trades.front();
    CHECK(trade.meta == original.meta);
    CHECK(trade.source_id == original.source_id);
    CHECK(trade.aggressor_side == original.aggressor_side);
    CHECK(trade.price == original.price);
    CHECK(trade.quantity == original.quantity);
    CHECK(trade.usd_notional == original.usd_notional);
}

TEST_CASE("C++ codec matches the shared V1 golden fixture", "[bridge]") {
    const auto trade = sample_trade();
    const auto bytes = bridge::encode_trade_batch(std::span{&trade, 1U});
    std::ifstream fixture(std::string{MC_SOURCE_DIR} + "/fixtures/m1/trade-batch-v1.hex");
    REQUIRE(fixture);
    const std::string expected{std::istreambuf_iterator<char>{fixture},
                               std::istreambuf_iterator<char>{}};
    REQUIRE(expected.size() >= 2);
    CHECK(as_hex(bytes) == expected.substr(0, expected.find_first_of("\r\n")));
}

TEST_CASE("bridge decoder rejects unsupported and inconsistent headers", "[bridge]") {
    const auto trade = sample_trade();
    const auto valid = bridge::encode_trade_batch(std::span{&trade, 1U});

    auto changed = valid;
    set_u16(changed, 4, 2);
    CHECK(bridge::decode(changed).error == bridge::DecodeError::UnsupportedVersion);
    changed    = valid;
    changed[6] = 255;
    CHECK(bridge::decode(changed).error == bridge::DecodeError::UnknownKind);
    changed    = valid;
    changed[7] = 1;
    CHECK(bridge::decode(changed).error == bridge::DecodeError::InvalidFlags);
    changed = valid;
    changed.pop_back();
    CHECK(bridge::decode(changed).error == bridge::DecodeError::InvalidLength);
    changed = valid;
    set_u16(changed, 12, bridge::k_max_events_per_batch + 1);
    CHECK(bridge::decode(changed).error == bridge::DecodeError::InvalidCount);
    CHECK(bridge::decode(std::vector<std::uint8_t>(bridge::k_max_message_bytes + 1)).error ==
          bridge::DecodeError::Oversized);
}

TEST_CASE("bridge decoder rejects invalid record enums decimals and metadata", "[bridge]") {
    const auto trade = sample_trade();
    const auto valid = bridge::encode_trade_batch(std::span{&trade, 1U});

    auto changed = valid;
    changed[18]  = 255; // venue
    CHECK(bridge::decode(changed).error == bridge::DecodeError::InvalidEnum);
    changed     = valid;
    changed[56] = domain::Decimal::k_max_scale + 1; // price scale
    CHECK(bridge::decode(changed).error == bridge::DecodeError::InvalidDecimal);
    changed = valid;
    std::fill_n(changed.begin() + 24, 8, std::uint8_t{0xFF}); // negative source time
    CHECK(bridge::decode(changed).error == bridge::DecodeError::InvalidMetadata);
}

TEST_CASE("bridge encoder enforces event count and string limits", "[bridge]") {
    const auto trade = sample_trade();
    const std::vector<domain::Trade> too_many(bridge::k_max_events_per_batch + 1, trade);
    CHECK(bridge::encode_trade_batch(too_many).empty());

    auto invalid = trade;
    invalid.source_id.assign(bridge::k_max_source_id_bytes + 1, 'x');
    CHECK(bridge::encode_trade_batch(std::span{&invalid, 1U}).empty());
}
