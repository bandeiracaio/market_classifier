#include <algorithm>

#include "../ui_common.hpp"
#include "panels.hpp"

namespace market_classifier::ui::panels {
namespace {

using namespace detail;

constexpr std::size_t k_rows = 200; // rows rendered per frame (tape holds 5,000)

class TradesTapePanel final : public Panel {
  public:
    [[nodiscard]] PanelKind kind() const override { return PanelKind::TradesTape; }

    void draw(const runtime::Engine &engine, PanelSettings &settings) override {
        const auto selection = panel_header(engine, kind(), settings, feed_q, feed_age);
        ImGui::SetNextItemWidth(120);
        double min_usd = settings.min_trade_usd;
        if (ImGui::InputDouble("min USD", &min_usd, 0, 0, "%.0f")) {
            settings.min_trade_usd = std::max(0.0, min_usd);
        }
        collect(engine, selection, settings.min_trade_usd);
        if (rows_.empty()) {
            empty_state("No trades yet");
            return;
        }
        const bool both = selection == VenueSelection::Both;
        if (!ImGui::BeginTable("tape", both ? 6 : 5,
                               ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_SizingFixedFit)) {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Time");
        if (both) {
            ImGui::TableSetupColumn("Venue");
        }
        ImGui::TableSetupColumn("Side");
        ImGui::TableSetupColumn("Price");
        ImGui::TableSetupColumn("Size");
        ImGui::TableSetupColumn("Notional");
        ImGui::TableHeadersRow();
        for (const auto &row : rows_) {
            draw_row(row, both);
        }
        ImGui::EndTable();
    }

  private:
    // Reused buffer; bounded by k_rows per shown venue.
    struct Row {
        const domain::Trade *trade;
        domain::Venue venue;
    };

    // Newest trades of the shown venues above the USD filter, merged by source time.
    void collect(const runtime::Engine &engine, VenueSelection selection, double min_usd) {
        rows_.clear();
        for (const auto v : venues_of(kind(), selection)) {
            const auto &tape  = engine.view(v).tape.trades();
            std::size_t taken = 0;
            for (std::size_t i = tape.size(); i-- > 0 && taken < k_rows;) {
                if (num(tape[i].usd_notional) >= min_usd) {
                    rows_.push_back({&tape[i], v});
                    ++taken;
                }
            }
        }
        std::stable_sort(rows_.begin(), rows_.end(), [](const Row &a, const Row &b) {
            return a.trade->meta.source_time().value > b.trade->meta.source_time().value;
        });
        if (rows_.size() > k_rows) {
            rows_.resize(k_rows);
        }
    }

    static void draw_row(const Row &row, bool both) {
        const auto &t = *row.trade;
        // Side is conveyed by text as well as color.
        const char *side_text = "?";
        ImVec4 color          = k_muted;
        if (t.aggressor_side == domain::AggressorSide::Buy) {
            side_text = "B ^";
            color     = k_buy;
        } else if (t.aggressor_side == domain::AggressorSide::Sell) {
            side_text = "S v";
            color     = k_sell;
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(format_time(t.meta.source_time().value, true).c_str());
        if (both) {
            ImGui::TableNextColumn();
            ImGui::TextColored(venue_color(row.venue), "%s",
                               row.venue == domain::Venue::BinanceUsdM ? "BN" : "HL");
        }
        ImGui::TableNextColumn();
        ImGui::TextColored(color, "%s", side_text);
        ImGui::TableNextColumn();
        ImGui::TextColored(color, "%s", text(t.price).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(text(t.quantity).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(grouped(num(t.usd_notional), 0).c_str());
    }
    std::vector<Row> rows_;
};

} // namespace

std::unique_ptr<Panel> make_trades_tape() {
    return std::make_unique<TradesTapePanel>();
}

} // namespace market_classifier::ui::panels
