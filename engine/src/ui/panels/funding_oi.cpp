#include "market_classifier/processors/metrics.hpp"

#include "implot.h"

#include "../ui_common.hpp"
#include "panels.hpp"

namespace market_classifier::ui::panels {
namespace {

using namespace detail;

class FundingOiPanel final : public Panel {
  public:
    PanelKind kind() const override { return PanelKind::FundingOi; }

    void draw(const runtime::Engine &engine, PanelSettings &settings) override {
        const auto selection = panel_header(engine, kind(), settings, feed_q, feed_age);
        const auto venues    = venues_of(kind(), selection);
        for (const auto v : venues) {
            const auto &m = engine.view(v).metrics.view();
            ImGui::TextColored(venue_color(v), "%s", venue_name(v));
            ImGui::SameLine();
            if (m.asset) {
                const auto next = m.asset->next_funding_time_ms
                                      ? *m.asset->next_funding_time_ms
                                      : processors::next_hourly_funding_ms(engine.wall_ms());
                ImGui::Text("funding %.4f%% (annualized %.2f%%), next in %s",
                            num(m.asset->funding_rate) * 100,
                            num(processors::annualized_funding(m.asset->funding_rate, v)) * 100,
                            format_duration(next - engine.wall_ms()).c_str());
            } else {
                ImGui::TextUnformatted("funding -");
            }
            ImGui::SameLine();
            if (m.open_interest_last) {
                // OI cadence is always shown; Binance is polled, never labeled streaming.
                ImGui::Text(
                    "| OI %s BTC (%s)",
                    grouped(num(m.open_interest_last->native_quantity), 1).c_str(),
                    m.oi_sample_interval_ms > 0
                        ? ("REST poll every " + format_duration(m.oi_sample_interval_ms)).c_str()
                        : "streamed");
            } else {
                ImGui::TextUnformatted("| OI -");
            }
        }
        ImPlot::GetStyle().UseLocalTime = !display_prefs().utc_time;
        const float half                = ImGui::GetContentRegionAvail().y * 0.5f;
        plot("##funding", "Funding %", half, engine, venues, true);
        plot("##oi", "OI (BTC)", -1, engine, venues, false);
    }

  private:
    void plot(const char *id, const char *axis, float height, const runtime::Engine &engine,
              const std::vector<domain::Venue> &venues, bool funding) {
        if (!ImPlot::BeginPlot(id, ImVec2(-1, height))) {
            return;
        }
        ImPlot::SetupAxes(nullptr, axis, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
        for (const auto v : venues) {
            const auto &m      = engine.view(v).metrics.view();
            const auto &series = funding ? m.funding : m.open_interest;
            xs_.clear();
            ys_.clear();
            for (std::size_t i = 0; i < series.size(); ++i) {
                xs_.push_back(static_cast<double>(series[i].t_ms) / 1000.0);
                ys_.push_back(num(series[i].value) * (funding ? 100.0 : 1.0));
            }
            ImPlot::SetNextLineStyle(venue_color(v));
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 2);
            ImPlot::PlotLine(venue_name(v), xs_.data(), ys_.data(), static_cast<int>(xs_.size()));
        }
        ImPlot::EndPlot();
    }

    std::vector<double> xs_, ys_;
};

} // namespace

std::unique_ptr<Panel> make_funding_oi() {
    return std::make_unique<FundingOiPanel>();
}

} // namespace market_classifier::ui::panels
