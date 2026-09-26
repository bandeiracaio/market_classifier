#include "market_classifier/runtime/clock.hpp"

#include <chrono>
#include <limits>

namespace market_classifier::runtime {

std::int64_t SystemClock::wall_time_ms() const noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::int64_t SystemClock::monotonic_time_ms() const noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool FakeClock::advance_ms(std::int64_t delta_ms) noexcept {
    if (delta_ms < 0 || wall_time_ms_ > std::numeric_limits<std::int64_t>::max() - delta_ms ||
        monotonic_time_ms_ > std::numeric_limits<std::int64_t>::max() - delta_ms) {
        return false;
    }
    wall_time_ms_ += delta_ms;
    monotonic_time_ms_ += delta_ms;
    return true;
}

} // namespace market_classifier::runtime
