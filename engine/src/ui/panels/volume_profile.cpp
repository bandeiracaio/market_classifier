#include "market_classifier/processors/footprint.hpp"

#include "../ui_common.hpp"
#include "panels.hpp"

namespace market_classifier::ui::panels {
namespace {

using namespace detail;

constexpr std::array<const char *, 4> k_bucket_labels{"$1", "$5", "$10", "$25"};

class VolumeProfilePanel final : public Panel {
  public:
    PanelKind kind() const override { return PanelKind::VolumeProfile; }

    void draw(const runtime::Engine &engine, PanelSettings &settings) override {
        const auto selection = panel_header(engine, kind(), settings, feed_q, feed_age);
        int index =
            settings.bucket_index < processors::k_bucket_sizes.size() ? settings.bucket_index : 1;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        if (ImGui::Combo("##bucket", &index, k_bucket_labels.data(),
                         static_cast<int>(k_bucket_labels.size()))) {
            settings.bucket_index = static_cast<std::uint8_t>(index);
        }
        const auto bucket =
            domain::Decimal::parse(processors::k_bucket_sizes[static_cast<std::size_t>(index)])
                .value;
        const auto profile = processors::aggregate(
            engine.view(to_venue(selection)).footprint.session_profile(), bucket);
        const auto &buckets = profile.buckets();
        if (buckets.empty()) {
            empty_state("No trades since page load");
            return;
        }
        const auto poc   = profile.poc();
        double max_total = 0;
        for (const auto &b : buckets) {
            max_total = std::max(max_total, num(b.bid_volume) + num(b.ask_volume));
        }
        ImGui::TextColored(k_muted, "Session from page load. POC %s",
                           poc ? text(*poc).c_str() : "-");
        ImGui::BeginChild("profile");
        auto *draw        = ImGui::GetWindowDrawList();
        const float width = ImGui::GetContentRegionAvail().x - 180.0f;
        for (std::size_t i = buckets.size(); i-- > 0;) {
            const auto &b     = buckets[i];
            const double sell = num(b.bid_volume);
            const double buy  = num(b.ask_volume);
            const bool is_poc = poc && b.price == *poc;
            ImGui::TextColored(is_poc ? ImVec4(1, 1, 0.3f, 1) : ImVec4(1, 1, 1, 1), "%s%10s",
                               is_poc ? "POC " : "    ", text(b.price).c_str());
            ImGui::SameLine(170);
            const auto origin = ImGui::GetCursorScreenPos();
            const float h     = ImGui::GetTextLineHeight();
            const float ws    = max_total > 0 ? static_cast<float>(sell / max_total) * width : 0;
            const float wb    = max_total > 0 ? static_cast<float>(buy / max_total) * width : 0;
            draw->AddRectFilled(origin, ImVec2(origin.x + ws, origin.y + h),
                                ImGui::GetColorU32(k_sell));
            draw->AddRectFilled(ImVec2(origin.x + ws, origin.y),
                                ImVec2(origin.x + ws + wb, origin.y + h),
                                ImGui::GetColorU32(k_buy));
            ImGui::Dummy(ImVec2(ws + wb, h));
        }
        ImGui::EndChild();
    }
};

} // namespace

std::unique_ptr<Panel> make_volume_profile() {
    return std::make_unique<VolumeProfilePanel>();
}

} // namespace market_classifier::ui::panels
