#pragma once

#include "market_classifier/domain/types.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace market_classifier::runtime {
class Engine;
}

namespace market_classifier::ui {

enum class VenueSelection : std::uint8_t { Binance, Hyperliquid, Both };

enum class PanelKind : std::uint8_t {
    Overview,
    TradesTape,
    Ladder,
    Depth,
    Candles,
    Cvd,
    Footprint,
    VolumeProfile,
    Heatmap,
    Liquidations,
    BboSpread,
    FundingOi,
    Diagnostics,
};
inline constexpr std::size_t k_panel_kind_count = 13;

// Per-panel persisted settings (workspace schema v1). Unused fields are ignored by
// panels that do not need them.
struct PanelSettings {
    VenueSelection venue        = VenueSelection::Binance;
    std::int64_t interval_ms    = 60'000;
    std::uint8_t bucket_index   = 1; // processors::k_bucket_sizes index ($5)
    double min_trade_usd        = 0;
    bool cvd_daily_reset        = false;
    std::uint8_t grouping_index = 0; // ladder price grouping

    [[nodiscard]] bool operator==(const PanelSettings &) const = default;
};

class Panel {
  public:
    virtual ~Panel()                             = default;
    [[nodiscard]] virtual PanelKind kind() const = 0;
    // Reads only engine read models. Settings may be edited by the panel's controls.
    virtual void draw(const runtime::Engine &engine, PanelSettings &settings) = 0;
};

struct PanelTraits {
    PanelKind kind;
    std::string_view title;
    bool supports_both;
};

[[nodiscard]] std::span<const PanelTraits> panel_traits(); // one entry per PanelKind
[[nodiscard]] const PanelTraits &traits_of(PanelKind kind);
[[nodiscard]] std::unique_ptr<Panel> make_panel(PanelKind kind);
// Single-venue panels treat Both as Binance.
[[nodiscard]] VenueSelection effective_venue(PanelKind kind, VenueSelection requested);

[[nodiscard]] std::string_view quality_label(domain::DataQuality quality);
// Draws a text badge (label + optional age) with a color; text always present.
void draw_quality_badge(domain::DataQuality quality, std::int64_t age_ms);

} // namespace market_classifier::ui
