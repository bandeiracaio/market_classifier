#pragma once

// Shared drawing helpers for panels. Private to engine/src/ui.

#include "market_classifier/domain/decimal_math.hpp"
#include "market_classifier/runtime/engine.hpp"
#include "market_classifier/ui/panel.hpp"
#include "market_classifier/ui/ui_state.hpp"

#include "imgui.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace market_classifier::ui::detail {

inline const ImVec4 k_buy{0.25f, 0.80f, 0.45f, 1.0f};
inline const ImVec4 k_sell{0.95f, 0.35f, 0.35f, 1.0f};
inline const ImVec4 k_muted{0.60f, 0.60f, 0.65f, 1.0f};
inline const ImVec4 k_binance_color{0.95f, 0.75f, 0.20f, 1.0f};
inline const ImVec4 k_hyperliquid_color{0.40f, 0.85f, 0.85f, 1.0f};

[[nodiscard]] inline domain::Venue to_venue(VenueSelection s) {
    return s == VenueSelection::Hyperliquid ? domain::Venue::Hyperliquid
                                            : domain::Venue::BinanceUsdM;
}

[[nodiscard]] inline const char *venue_name(domain::Venue v) {
    return v == domain::Venue::BinanceUsdM ? "Binance" : "Hyperliquid";
}

[[nodiscard]] inline ImVec4 venue_color(domain::Venue v) {
    return v == domain::Venue::BinanceUsdM ? k_binance_color : k_hyperliquid_color;
}

// Venues to render for a selection (Both => both, in fixed order).
[[nodiscard]] inline std::vector<domain::Venue> venues_of(PanelKind kind, VenueSelection s) {
    const auto e = effective_venue(kind, s);
    if (e == VenueSelection::Both) {
        return {domain::Venue::BinanceUsdM, domain::Venue::Hyperliquid};
    }
    return {to_venue(e)};
}

// Draws the venue selector (Both only where supported). Returns the effective selection.
VenueSelection venue_selector(PanelKind kind, PanelSettings &settings);

// Formats epoch ms as HH:MM:SS(.mmm) in UTC or local time per display prefs.
[[nodiscard]] std::string format_time(std::int64_t epoch_ms, bool millis = false);
[[nodiscard]] std::string format_duration(std::int64_t ms);
[[nodiscard]] inline std::string text(const domain::Decimal &d) {
    return d.to_string();
}
[[nodiscard]] inline std::string text(const std::optional<domain::Decimal> &d) {
    return d ? d->to_string() : "-";
}
[[nodiscard]] inline double num(const domain::Decimal &d) {
    return domain::to_double(d);
}
// Thousands-separated fixed-point number for display.
[[nodiscard]] std::string grouped(double value, int decimals);

// Header row: selector + one badge per shown venue (quality + age since last update).
VenueSelection panel_header(const runtime::Engine &engine, PanelKind kind, PanelSettings &settings,
                            domain::DataQuality (*quality)(const runtime::Engine &, domain::Venue),
                            std::int64_t (*last_update)(const runtime::Engine &, domain::Venue));

[[nodiscard]] inline domain::DataQuality feed_q(const runtime::Engine &e, domain::Venue v) {
    return e.feed_quality(v);
}
[[nodiscard]] inline domain::DataQuality book_q(const runtime::Engine &e, domain::Venue v) {
    return e.book_quality(v);
}
[[nodiscard]] inline domain::DataQuality liq_q(const runtime::Engine &e, domain::Venue v) {
    return e.liquidation_quality(v);
}
[[nodiscard]] inline std::int64_t feed_age(const runtime::Engine &e, domain::Venue v) {
    return e.feed(v).age_ms(e.now_ms());
}

// Message shown in place of content when there is nothing valid to draw.
void empty_state(const char *message);

} // namespace market_classifier::ui::detail
