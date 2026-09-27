#include <algorithm>
#include <cmath>

#include "../ui_common.hpp"
#include "panels.hpp"

namespace market_classifier::ui::panels {
namespace {

using namespace detail;

constexpr std::int64_t k_window_ms = 15 * 60'000; // visible time span (store keeps 60 min)

ImU32 heat_color(double normalized) {
    const float t = static_cast<float>(std::clamp(normalized, 0.0, 1.0));
    return ImGui::GetColorU32(ImVec4(t, 0.2F + 0.6F * t * t, 1.0F - t, 0.15F + 0.85F * t));
}

class HeatmapPanel final : public Panel {
  public:
    [[nodiscard]] PanelKind kind() const override { return PanelKind::Heatmap; }

    void draw(const runtime::Engine &engine, PanelSettings &settings) override {
        const auto selection = panel_header(engine, kind(), settings, book_q, feed_age);
        const auto &heatmap  = engine.view(to_venue(selection)).heatmap;
        const auto &columns  = heatmap.columns();
        if (columns.empty()) {
            empty_state("No book samples yet");
            return;
        }
        const auto end_t   = columns.back().t_ms;
        const auto begin_t = end_t - k_window_ms;
        // Price window: +-0.5% around the newest best bid.
        const double mid = columns.back().price_of(0, heatmap.quantum());
        const double lo  = mid * 0.995;
        const double hi  = mid * 1.005;
        ImGui::TextColored(k_muted, "%s window, price %.0f-%.0f, log-scaled size; dots = trades",
                           format_duration(k_window_ms).c_str(), lo, hi);
        const auto origin = ImGui::GetCursorScreenPos();
        const auto size   = ImGui::GetContentRegionAvail();
        if (size.x < 10 || size.y < 10) {
            return;
        }
        auto *draw      = ImGui::GetWindowDrawList();
        const auto x_of = [&](std::int64_t t) {
            return origin.x +
                   static_cast<float>(static_cast<double>(t - begin_t) / k_window_ms) * size.x;
        };
        const auto y_of = [&](double p) {
            return origin.y + static_cast<float>((hi - p) / (hi - lo)) * size.y;
        };
        // One rect per raster cell (<= 480 x 256) regardless of stored/visible history.
        constexpr float k_cell_px = 3.0F;
        const auto raster         = processors::rasterize(heatmap, begin_t, end_t, lo, hi,
                                                          static_cast<std::size_t>(size.x / k_cell_px),
                                                          static_cast<std::size_t>(size.y / k_cell_px));
        const float cell_w        = size.x / static_cast<float>(raster.columns);
        const float cell_h        = size.y / static_cast<float>(raster.rows);
        for (std::size_t row = 0; row < raster.rows; ++row) {
            for (std::size_t col = 0; col < raster.columns; ++col) {
                const float v = raster.at(col, row);
                if (v <= 0.0F) {
                    continue;
                }
                const ImVec2 top_left(origin.x + static_cast<float>(col) * cell_w,
                                      origin.y + static_cast<float>(row) * cell_h);
                draw->AddRectFilled(top_left, ImVec2(top_left.x + cell_w, top_left.y + cell_h),
                                    heat_color(v));
            }
        }
        const auto &trades = heatmap.trades();
        for (std::size_t i = trades.size(); i-- > 0;) {
            const auto t = trades[i].meta.receive_time().value;
            if (t < begin_t) {
                break;
            }
            const double p = num(trades[i].price);
            if (p < lo || p > hi) {
                continue;
            }
            const float r = 1.5F + static_cast<float>(std::log2(1.0 + num(trades[i].quantity)));
            draw->AddCircleFilled(
                ImVec2(x_of(t), y_of(p)), r,
                ImGui::GetColorU32(
                    trades[i].aggressor_side == domain::AggressorSide::Buy ? k_buy : k_sell));
        }
        ImGui::Dummy(size);
    }
};

} // namespace

std::unique_ptr<Panel> make_heatmap() {
    return std::make_unique<HeatmapPanel>();
}

} // namespace market_classifier::ui::panels
