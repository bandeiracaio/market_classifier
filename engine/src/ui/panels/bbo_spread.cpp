#include "implot.h"

#include "../ui_common.hpp"
#include "panels.hpp"

namespace market_classifier::ui::panels {
namespace {

using namespace detail;

class BboSpreadPanel final : public Panel {
  public:
    PanelKind kind() const override { return PanelKind::BboSpread; }

    void draw(const runtime::Engine &engine, PanelSettings &settings) override {
        const auto selection = panel_header(engine, kind(), settings, feed_q, feed_age);
        const auto venues    = venues_of(kind(), selection);
        for (const auto v : venues) {
            const auto &m = engine.view(v).metrics.view();
            if (!m.bbo || !m.mid) {
                ImGui::TextColored(venue_color(v), "%s: no BBO yet", venue_name(v));
                continue;
            }
            const auto spread  = domain::sub(m.bbo->ask_price, m.bbo->bid_price).value;
            const auto &def    = engine.view(v).definition;
            const double ticks = def ? num(spread) / num(def->tick_size) : 0;
            ImGui::TextColored(venue_color(v), "%s", venue_name(v));
            ImGui::SameLine();
            ImGui::TextColored(k_buy, "bid %s x %s", text(m.bbo->bid_price).c_str(),
                               text(m.bbo->bid_quantity).c_str());
            ImGui::SameLine();
            ImGui::TextColored(k_sell, "ask %s x %s", text(m.bbo->ask_price).c_str(),
                               text(m.bbo->ask_quantity).c_str());
            ImGui::SameLine();
            ImGui::Text("spread %s (%.0f ticks, %.2f bps)", text(spread).c_str(), ticks,
                        num(spread) / num(*m.mid) * 1e4);
        }
        ImPlot::GetStyle().UseLocalTime = !display_prefs().utc_time;
        if (!ImPlot::BeginPlot("##spread", ImVec2(-1, -1))) {
            return;
        }
        ImPlot::SetupAxes(nullptr, "Spread (bps)", ImPlotAxisFlags_AutoFit,
                          ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
        for (const auto v : venues) {
            const auto &series = engine.view(v).metrics.view().spread;
            xs_.clear();
            ys_.clear();
            for (std::size_t i = 0; i < series.size(); ++i) {
                xs_.push_back(static_cast<double>(series[i].t_ms) / 1000.0);
                ys_.push_back(num(series[i].spread) / num(series[i].mid) * 1e4);
            }
            ImPlot::SetNextLineStyle(venue_color(v));
            ImPlot::PlotStairs(venue_name(v), xs_.data(), ys_.data(), static_cast<int>(xs_.size()));
        }
        ImPlot::EndPlot();
    }

  private:
    std::vector<double> xs_, ys_;
};

} // namespace

std::unique_ptr<Panel> make_bbo_spread() {
    return std::make_unique<BboSpreadPanel>();
}

} // namespace market_classifier::ui::panels
