#include "implot.h"

#include <limits>

#include "../ui_common.hpp"
#include "panels.hpp"

namespace market_classifier::ui::panels {
namespace {

using namespace detail;

class CvdPanel final : public Panel {
  public:
    PanelKind kind() const override { return PanelKind::Cvd; }

    void draw(const runtime::Engine &engine, PanelSettings &settings) override {
        const auto selection = panel_header(engine, kind(), settings, feed_q, feed_age);
        ImGui::SameLine();
        ImGui::Checkbox("reset 00:00 UTC", &settings.cvd_daily_reset);
        const auto venues = venues_of(kind(), selection);
        for (const auto v : venues) {
            ui_actions().cvd_daily_reset[runtime::venue_index(v)] |= settings.cvd_daily_reset;
            const auto &cvd = engine.view(v).cvd;
            ImGui::TextColored(venue_color(v), "%s CVD %s BTC%s%s", venue_name(v),
                               text(cvd.value()).c_str(),
                               cvd.gap_count() > 0 ? "  (gaps marked)" : "",
                               cvd.failed() ? "  [Failed: overflow]" : "");
        }
        ImGui::TextColored(k_muted, "Live since page load; no history backfill.");
        ImPlot::GetStyle().UseLocalTime = !display_prefs().utc_time;
        if (!ImPlot::BeginPlot("##cvd", ImVec2(-1, -1))) {
            return;
        }
        ImPlot::SetupAxes(nullptr, "BTC", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
        for (const auto v : venues) {
            const auto &series = engine.view(v).cvd.series();
            xs_.clear();
            ys_.clear();
            for (std::size_t i = 0; i < series.size(); ++i) {
                // A gap point breaks the line: NaN before it (never bridge a discontinuity).
                if (series[i].gap && !xs_.empty()) {
                    xs_.push_back(static_cast<double>(series[i].minute_ms) / 1000.0);
                    ys_.push_back(std::numeric_limits<double>::quiet_NaN());
                }
                xs_.push_back(static_cast<double>(series[i].minute_ms) / 1000.0);
                ys_.push_back(num(series[i].value));
            }
            ImPlot::SetNextLineStyle(venue_color(v));
            ImPlot::PlotLine(venue_name(v), xs_.data(), ys_.data(), static_cast<int>(xs_.size()));
        }
        ImPlot::EndPlot();
    }

  private:
    std::vector<double> xs_, ys_;
};

} // namespace

std::unique_ptr<Panel> make_cvd() {
    return std::make_unique<CvdPanel>();
}

} // namespace market_classifier::ui::panels
