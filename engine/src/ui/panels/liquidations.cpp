#include "market_classifier/venues/instruments.hpp"

#include "../ui_common.hpp"
#include "panels.hpp"

namespace market_classifier::ui::panels {
namespace {

using namespace detail;

constexpr std::size_t k_rows = 100;

class LiquidationsPanel final : public Panel {
  public:
    [[nodiscard]] PanelKind kind() const override { return PanelKind::Liquidations; }

    void draw(const runtime::Engine &engine, PanelSettings &settings) override {
        const auto selection = panel_header(engine, kind(), settings, liq_q, feed_age);
        for (const auto v : venues_of(kind(), selection)) {
            ImGui::SeparatorText(venue_name(v));
            if (!venues::supports_liquidations(v)) {
                // Never inferred from trades (packet §4.2).
                ImGui::TextColored(k_muted, "Unsupported: %s publishes no public liquidation feed.",
                                   venue_name(v));
                continue;
            }
            ImGui::TextColored(k_muted, "Binance forceOrder: at most one per second (sampled).");
            const auto &items = engine.view(v).liquidations.items();
            if (items.empty()) {
                empty_state("No liquidations since page load");
                continue;
            }
            if (!ImGui::BeginTable(venue_name(v), 5,
                                   ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                continue;
            }
            ImGui::TableSetupColumn("Time");
            ImGui::TableSetupColumn("Liquidated");
            ImGui::TableSetupColumn("Avg price");
            ImGui::TableSetupColumn("Size");
            ImGui::TableSetupColumn("Notional");
            ImGui::TableHeadersRow();
            std::size_t shown = 0;
            for (std::size_t i = items.size(); i-- > 0 && shown < k_rows; ++shown) {
                const auto &l    = items[i];
                const bool longs = l.side == domain::LiquidationSide::Long;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(format_time(l.meta.source_time().value).c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(longs ? k_sell : k_buy, "%s", longs ? "LONG v" : "SHORT ^");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(text(l.price).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(text(l.quantity).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(grouped(num(l.notional), 0).c_str());
            }
            ImGui::EndTable();
        }
    }
};

} // namespace

std::unique_ptr<Panel> make_liquidations() {
    return std::make_unique<LiquidationsPanel>();
}

} // namespace market_classifier::ui::panels
