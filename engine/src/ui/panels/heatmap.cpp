#include <cmath>

#include "../ui_common.hpp"
#include "panels.hpp"

namespace market_classifier::ui::panels {
namespace {

using namespace detail;

constexpr std::int64_t k_window_ms = 15 * 60'000; // visible time span (store keeps 60 min)

ImU32 heat_color(double normalized) {
    const float t = static_cast<float>(std::clamp(normalized, 0.0, 1.0));
    return ImGui::GetColorU32(ImVec4(t, 0.2f + 0.6f * t * t, 1.0f - t, 0.15f + 0.85f * t));
}

class HeatmapPanel final : public Panel {
  public:
    PanelKind kind() const override { return PanelKind::Heatmap; }

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
        const double max_log = std::log2(1.0 + std::max(heatmap.max_quantity(), 1e-9));
        const float cell_w   = std::max(
            1.0f, size.x * static_cast<float>(processors::k_heatmap_column_ms) / k_window_ms);
        const double quantum = num(heatmap.quantum());
        const float cell_h   = std::max(1.0f, static_cast<float>(quantum / (hi - lo)) * size.y);
        for (std::size_t i = 0; i < columns.size(); ++i) {
            const auto &c = columns[i];
            if (c.t_ms < begin_t) {
                continue;
            }
            const float x   = x_of(c.t_ms);
            const auto side = [&](const std::vector<std::int16_t> &offsets,
                                  const std::vector<std::uint16_t> &codes) {
                for (std::size_t k = 0; k < offsets.size(); ++k) {
                    const double p = c.price_of(offsets[k], heatmap.quantum());
                    if (p < lo || p > hi) {
                        continue;
                    }
                    const double q = processors::HeatmapColumn::decode_quantity(codes[k]);
                    const float y  = y_of(p);
                    draw->AddRectFilled(ImVec2(x, y - cell_h), ImVec2(x + cell_w, y),
                                        heat_color(std::log2(1.0 + q) / max_log));
                }
            };
            side(c.bid_offsets, c.bid_quantities);
            side(c.ask_offsets, c.ask_quantities);
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
            const float r = 1.5f + static_cast<float>(std::log2(1.0 + num(trades[i].quantity)));
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
