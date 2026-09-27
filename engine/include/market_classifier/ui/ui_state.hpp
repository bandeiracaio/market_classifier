#pragma once

#include "market_classifier/domain/types.hpp"

#include <array>
#include <optional>

namespace market_classifier::ui {

// Display preferences persisted with the workspace (packet §8).
struct DisplayPrefs {
    bool utc_time = false;
};

// Mutations requested by panels (which only see `const Engine &`); the app applies them
// to the engine once per frame, then clears them.
struct UiActions {
    std::optional<domain::Venue> retry_venue;
    std::array<bool, 2> cvd_daily_reset{}; // any CVD panel wants it, per venue
    std::array<bool, 2> cvd_reset_now{};
};

[[nodiscard]] DisplayPrefs &display_prefs();
[[nodiscard]] UiActions &ui_actions();

} // namespace market_classifier::ui
