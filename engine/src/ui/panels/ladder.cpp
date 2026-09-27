#include <map>

#include "../ui_common.hpp"
#include "panels.hpp"

namespace market_classifier::ui::panels {
namespace {

using namespace detail;

constexpr int k_rows_per_side = 20;
// Price grouping increments in USD; "tick" uses the venue's own levels.
constexpr std::array<const char *, 6> k_groupings{"tick", "1", "5", "10", "50", "100"};

struct Level {
    domain::Decimal price;
    domain::Decimal size;
};

// Groups sorted levels into buckets (bids floor, asks ceil-by-floor+step) with exact sums.
std::vector<Level> group(std::span<const domain::BookLevel> side, const char *step_text,
                         bool bids) {
    std::vector<Level> out;
    const bool raw  = std::string_view(step_text) == "tick";
    const auto step = raw ? domain::Decimal{} : domain::Decimal::parse(step_text).value;
    for (const auto &l : side) {
        auto price = l.price;
        if (!raw) {
            auto floored = domain::floor_to(l.price, step).value;
            if (!bids && !(floored == l.price)) {
                floored = domain::add(floored, step).value; // asks round away from the touch
            }
            price = floored;
        }
        if (!out.empty() && out.back().price == price) {
            out.back().size = domain::add(out.back().size, l.quantity).value;
        } else {
            if (out.size() >= static_cast<std::size_t>(k_rows_per_side)) {
                break;
            }
            out.push_back({price, l.quantity});
        }
    }
    return out;
}

class LadderPanel final : public Panel {
  public:
    [[nodiscard]] PanelKind kind() const override { return PanelKind::Ladder; }

    void draw(const runtime::Engine &engine, PanelSettings &settings) override {
        const auto selection = panel_header(engine, kind(), settings, book_q, feed_age);
        const auto venue     = to_venue(selection);
        ImGui::SameLine();
        int g = settings.grouping_index < k_groupings.size() ? settings.grouping_index : 0;
        ImGui::SetNextItemWidth(80);
        if (ImGui::Combo("group", &g, k_groupings.data(), static_cast<int>(k_groupings.size()))) {
            settings.grouping_index = static_cast<std::uint8_t>(g);
        }
        if (engine.book_quality(venue) != domain::DataQuality::Live) {
            empty_state("Book not live: waiting for a synchronized snapshot");
            return;
        }
        const auto &book = engine.book(venue);
        const auto asks  = group(book.asks(), k_groupings.at(static_cast<std::size_t>(g)), false);
        const auto bids  = group(book.bids(), k_groupings.at(static_cast<std::size_t>(g)), true);
        if (!ImGui::BeginTable("ladder", 3,
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
            return;
        }
        ImGui::TableSetupColumn("Price");
        ImGui::TableSetupColumn("Size");
        ImGui::TableSetupColumn("Cumulative");
        ImGui::TableHeadersRow();
        // Asks top-down from farthest to best; cumulative is measured from the touch.
        std::vector<domain::Decimal> ask_cum(asks.size());
        domain::Decimal acc{};
        for (std::size_t i = 0; i < asks.size(); ++i) {
            acc        = domain::add(acc, asks[i].size).value;
            ask_cum[i] = acc;
        }
        for (std::size_t i = asks.size(); i-- > 0;) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(k_sell, "A %s", text(asks[i].price).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(text(asks[i].size).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(text(ask_cum[i]).c_str());
        }
        acc = {};
        for (const auto &b : bids) {
            acc = domain::add(acc, b.size).value;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(k_buy, "B %s", text(b.price).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(text(b.size).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(text(acc).c_str());
        }
        ImGui::EndTable();
    }
};

} // namespace

std::unique_ptr<Panel> make_ladder() {
    return std::make_unique<LadderPanel>();
}

} // namespace market_classifier::ui::panels
