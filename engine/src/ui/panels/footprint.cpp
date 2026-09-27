#include "market_classifier/processors/footprint.hpp"

#include "../ui_common.hpp"
#include "panels.hpp"

namespace market_classifier::ui::panels {
namespace {

using namespace detail;

constexpr std::size_t k_visible_candles = 12;
constexpr std::array<const char *, 4> k_bucket_labels{"$1", "$5", "$10", "$25"};

// Bucket selector shared with the volume profile panel.
domain::Decimal bucket_selector(PanelSettings &settings) {
    int index =
        settings.bucket_index < processors::k_bucket_sizes.size() ? settings.bucket_index : 1;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    if (ImGui::Combo("##bucket", &index, k_bucket_labels.data(),
                     static_cast<int>(k_bucket_labels.size()))) {
        settings.bucket_index = static_cast<std::uint8_t>(index);
    }
    const auto text_value = processors::k_bucket_sizes[static_cast<std::size_t>(index)];
    return domain::Decimal::parse(text_value).value;
}

class FootprintPanel final : public Panel {
  public:
    PanelKind kind() const override { return PanelKind::Footprint; }

    void draw(const runtime::Engine &engine, PanelSettings &settings) override {
        const auto selection = panel_header(engine, kind(), settings, feed_q, feed_age);
        const auto bucket    = bucket_selector(settings);
        const auto venue     = to_venue(selection);
        const auto &fp       = engine.view(venue).footprint;
        const auto candles =
            processors::aggregate(fp.candles(), bucket, settings.interval_ms, k_visible_candles);
        if (candles.empty()) {
            empty_state("No trades since page load");
            return;
        }
        ImGui::TextColored(k_muted, "cells: bid x ask (base units); interval %s",
                           format_duration(settings.interval_ms).c_str());
        const int columns = static_cast<int>(candles.size());
        if (!ImGui::BeginTable("footprint", columns,
                               ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                                   ImGuiTableFlags_SizingFixedFit)) {
            return;
        }
        std::size_t max_rows = 0;
        for (const auto &c : candles) {
            ImGui::TableSetupColumn(format_time(c.open_time_ms).c_str());
            max_rows = std::max(max_rows, c.cells.size());
        }
        ImGui::TableHeadersRow();
        // Rows from high to low price inside each candle column.
        for (std::size_t row = 0; row < max_rows; ++row) {
            ImGui::TableNextRow();
            for (const auto &c : candles) {
                ImGui::TableNextColumn();
                if (row == 0 && c.gap) {
                    ImGui::TextColored(k_sell, "[gap]");
                }
                if (row >= c.cells.size()) {
                    continue;
                }
                const auto &cell     = c.cells[c.cells.size() - 1 - row];
                const bool buy_heavy = cell.bid_volume < cell.ask_volume;
                ImGui::TextColored(buy_heavy ? k_buy : k_sell, "%s %s x %s",
                                   text(cell.price).c_str(), text(cell.bid_volume).c_str(),
                                   text(cell.ask_volume).c_str());
            }
        }
        ImGui::EndTable();
    }
};

} // namespace

std::unique_ptr<Panel> make_footprint() {
    return std::make_unique<FootprintPanel>();
}

} // namespace market_classifier::ui::panels
