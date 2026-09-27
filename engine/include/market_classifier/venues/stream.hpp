#pragma once

#include <cstdint>

namespace market_classifier::venues {

// Identifies which upstream source produced a raw frame. Values are wire-stable
// (RawFrameBatch, ADR-0004 addendum); never renumber.
enum class StreamTag : std::uint8_t {
    BinanceExchangeInfo = 1,
    BinanceDepthSnapshot,
    BinanceWs, // combined-stream frame from /public or /market
    BinanceKlinesRest,
    BinanceOpenInterestRest,
    HyperliquidMeta = 32,
    HyperliquidWs,
    HyperliquidCandleSnapshot,
};

[[nodiscard]] constexpr bool is_valid(StreamTag tag) noexcept {
    const auto v = static_cast<std::uint8_t>(tag);
    return (v >= 1 && v <= 5) || (v >= 32 && v <= 34);
}

} // namespace market_classifier::venues
