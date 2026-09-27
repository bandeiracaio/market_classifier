#include "market_classifier/venues/instruments.hpp"

namespace market_classifier::venues {

std::optional<std::int64_t> interval_ms(std::string_view text) noexcept {
    constexpr std::int64_t minute = 60'000;
    if (text == "1m")
        return minute;
    if (text == "5m")
        return 5 * minute;
    if (text == "15m")
        return 15 * minute;
    if (text == "1h")
        return 60 * minute;
    if (text == "4h")
        return 240 * minute;
    if (text == "1d")
        return 1440 * minute;
    return std::nullopt;
}

} // namespace market_classifier::venues
