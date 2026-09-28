#pragma once

#include "market_classifier/domain/types.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace market_classifier::venues {

// The MVP instrument set is fixed (packet MVP_BTC_TERMINAL.md §4). Metadata such as tick
// size is still fetched and validated at startup.
struct InstrumentSpec {
    domain::Venue venue;
    std::string_view native_symbol;
    std::string_view display;
};

inline constexpr std::array<InstrumentSpec, 2> k_instruments{{
    {domain::Venue::BinanceUsdM, "BTCUSDT", "BTC-PERP (Binance)"},
    {domain::Venue::Hyperliquid, "BTC", "BTC-PERP (Hyperliquid)"},
}};

// Binance open interest is REST-polled, never streamed; the cadence travels with every
// sample so panels can show it (packet §4.1).
inline constexpr std::int64_t k_binance_oi_poll_ms = 10'000;

[[nodiscard]] constexpr const InstrumentSpec &instrument_for(domain::Venue venue) noexcept {
    return venue == domain::Venue::BinanceUsdM ? k_instruments[0] : k_instruments[1];
}

// Hyperliquid documents no public liquidation feed; never infer one from trades.
[[nodiscard]] constexpr bool supports_liquidations(domain::Venue venue) noexcept {
    return venue == domain::Venue::BinanceUsdM;
}

// Candle interval text shared by both venues ("1m", "5m", "15m", "1h", "4h", "1d").
[[nodiscard]] std::optional<std::int64_t> interval_ms(std::string_view text) noexcept;

} // namespace market_classifier::venues
