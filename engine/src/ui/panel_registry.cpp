#include "market_classifier/ui/panel.hpp"
#include "market_classifier/ui/ui_state.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "panels/panels.hpp"
#include "ui_common.hpp"

namespace market_classifier::ui {
namespace {

// "Both?" column of packet §5; Diagnostics is venue-independent.
constexpr std::array<PanelTraits, k_panel_kind_count> k_traits{{
    {PanelKind::Overview, "Overview", true},
    {PanelKind::TradesTape, "Trades", true},
    {PanelKind::Ladder, "Order Book", false},
    {PanelKind::Depth, "Depth", true},
    {PanelKind::Candles, "Candles", true},
    {PanelKind::Cvd, "CVD", true},
    {PanelKind::Footprint, "Footprint", false},
    {PanelKind::VolumeProfile, "Volume Profile", false},
    {PanelKind::Heatmap, "Liquidity Heatmap", false},
    {PanelKind::Liquidations, "Liquidations", true},
    {PanelKind::BboSpread, "BBO / Spread", true},
    {PanelKind::FundingOi, "Funding / OI", true},
    {PanelKind::Diagnostics, "Diagnostics", false},
}};

} // namespace

std::span<const PanelTraits> panel_traits() {
    return k_traits;
}

const PanelTraits &traits_of(PanelKind kind) {
    return k_traits[static_cast<std::size_t>(kind)];
}

VenueSelection effective_venue(PanelKind kind, VenueSelection requested) {
    if (requested == VenueSelection::Both && !traits_of(kind).supports_both) {
        return VenueSelection::Binance;
    }
    return requested;
}

std::unique_ptr<Panel> make_panel(PanelKind kind) {
    using namespace panels;
    switch (kind) {
    case PanelKind::Overview:
        return make_overview();
    case PanelKind::TradesTape:
        return make_trades_tape();
    case PanelKind::Ladder:
        return make_ladder();
    case PanelKind::Depth:
        return make_depth();
    case PanelKind::Candles:
        return make_candles();
    case PanelKind::Cvd:
        return make_cvd();
    case PanelKind::Footprint:
        return make_footprint();
    case PanelKind::VolumeProfile:
        return make_volume_profile();
    case PanelKind::Heatmap:
        return make_heatmap();
    case PanelKind::Liquidations:
        return make_liquidations();
    case PanelKind::BboSpread:
        return make_bbo_spread();
    case PanelKind::FundingOi:
        return make_funding_oi();
    case PanelKind::Diagnostics:
        return make_diagnostics();
    }
    return nullptr;
}

std::string_view quality_label(domain::DataQuality quality) {
    switch (quality) {
    case domain::DataQuality::Live:
        return "Live";
    case domain::DataQuality::Delayed:
        return "Delayed";
    case domain::DataQuality::Stale:
        return "Stale";
    case domain::DataQuality::Reconnecting:
        return "Reconnecting";
    case domain::DataQuality::GapDetected:
        return "GapDetected";
    case domain::DataQuality::Partial:
        return "Partial";
    case domain::DataQuality::Unsupported:
        return "Unsupported";
    case domain::DataQuality::Failed:
        return "Failed";
    }
    return "Unknown";
}

void draw_quality_badge(domain::DataQuality quality, std::int64_t age_ms) {
    ImVec4 color{0.3f, 0.85f, 0.4f, 1.0f};
    switch (quality) {
    case domain::DataQuality::Live:
        break;
    case domain::DataQuality::Stale:
    case domain::DataQuality::Delayed:
    case domain::DataQuality::Partial:
        color = {0.95f, 0.75f, 0.2f, 1.0f};
        break;
    case domain::DataQuality::Reconnecting:
    case domain::DataQuality::GapDetected:
        color = {0.95f, 0.55f, 0.2f, 1.0f};
        break;
    case domain::DataQuality::Unsupported:
        color = {0.6f, 0.6f, 0.65f, 1.0f};
        break;
    case domain::DataQuality::Failed:
        color = {0.95f, 0.3f, 0.3f, 1.0f};
        break;
    }
    const auto label = quality_label(quality);
    if (quality == domain::DataQuality::Stale && age_ms > 0) {
        ImGui::TextColored(color, "[%.*s %s]", static_cast<int>(label.size()), label.data(),
                           detail::format_duration(age_ms).c_str());
    } else {
        ImGui::TextColored(color, "[%.*s]", static_cast<int>(label.size()), label.data());
    }
}

DisplayPrefs &display_prefs() {
    static DisplayPrefs prefs;
    return prefs;
}

UiActions &ui_actions() {
    static UiActions actions;
    return actions;
}

namespace detail {

VenueSelection venue_selector(PanelKind kind, PanelSettings &settings) {
    settings.venue = effective_venue(kind, settings.venue);
    if (kind == PanelKind::Diagnostics || kind == PanelKind::Overview) {
        return settings.venue;
    }
    const bool both     = traits_of(kind).supports_both;
    const char *items[] = {"Binance", "Hyperliquid", "Both"};
    int current         = static_cast<int>(settings.venue);
    ImGui::SetNextItemWidth(110);
    if (ImGui::Combo("##venue", &current, items, both ? 3 : 2)) {
        settings.venue = static_cast<VenueSelection>(current);
    }
    return settings.venue;
}

std::string format_time(std::int64_t epoch_ms, bool millis) {
    const std::time_t seconds = static_cast<std::time_t>(epoch_ms / 1000);
    const std::tm *tm = display_prefs().utc_time ? std::gmtime(&seconds) : std::localtime(&seconds);
    char buffer[32];
    if (tm == nullptr) {
        return "--:--:--";
    }
    if (millis) {
        std::snprintf(buffer, sizeof buffer, "%02d:%02d:%02d.%03d", tm->tm_hour, tm->tm_min,
                      tm->tm_sec, static_cast<int>(epoch_ms % 1000));
    } else {
        std::snprintf(buffer, sizeof buffer, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    }
    return buffer;
}

std::string format_duration(std::int64_t ms) {
    char buffer[32];
    if (ms < 0) {
        ms = 0;
    }
    if (ms < 60'000) {
        std::snprintf(buffer, sizeof buffer, "%.1fs", static_cast<double>(ms) / 1000.0);
    } else if (ms < 3'600'000) {
        std::snprintf(buffer, sizeof buffer, "%lldm%02llds", static_cast<long long>(ms / 60'000),
                      static_cast<long long>(ms / 1000 % 60));
    } else {
        std::snprintf(buffer, sizeof buffer, "%lldh%02lldm", static_cast<long long>(ms / 3'600'000),
                      static_cast<long long>(ms / 60'000 % 60));
    }
    return buffer;
}

std::string grouped(double value, int decimals) {
    char raw[64];
    std::snprintf(raw, sizeof raw, "%.*f", decimals, std::fabs(value));
    std::string digits(raw);
    const auto dot      = digits.find('.');
    std::string intpart = digits.substr(0, dot);
    std::string out;
    for (std::size_t i = 0; i < intpart.size(); ++i) {
        if (i > 0 && (intpart.size() - i) % 3 == 0) {
            out.push_back(',');
        }
        out.push_back(intpart[i]);
    }
    if (dot != std::string::npos) {
        out += digits.substr(dot);
    }
    return (value < 0 ? "-" : "") + out;
}

VenueSelection panel_header(const runtime::Engine &engine, PanelKind kind, PanelSettings &settings,
                            domain::DataQuality (*quality)(const runtime::Engine &, domain::Venue),
                            std::int64_t (*last_update)(const runtime::Engine &, domain::Venue)) {
    const auto selection = venue_selector(kind, settings);
    for (const auto v : venues_of(kind, selection)) {
        ImGui::SameLine();
        ImGui::TextColored(venue_color(v), "%s", venue_name(v));
        ImGui::SameLine();
        draw_quality_badge(quality(engine, v), last_update(engine, v));
    }
    return selection;
}

void empty_state(const char *message) {
    ImGui::TextColored(k_muted, "%s", message);
}

} // namespace detail
} // namespace market_classifier::ui
