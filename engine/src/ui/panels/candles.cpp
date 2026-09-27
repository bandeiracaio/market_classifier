#include "market_classifier/processors/limits.hpp"

#include "implot.h"

#include "../ui_common.hpp"
#include "panels.hpp"

namespace market_classifier::ui::panels {
namespace {

using namespace detail;

constexpr std::array<const char *, 6> k_interval_names{"1m", "5m", "15m", "1h", "4h", "1d"};

// Interval selector shared by candle-like panels. Returns the chosen interval in ms.
std::int64_t interval_selector(PanelSettings &settings) {
    int index = 0;
    for (std::size_t i = 0; i < processors::k_candle_intervals_ms.size(); ++i) {
        if (processors::k_candle_intervals_ms[i] == settings.interval_ms) {
            index = static_cast<int>(i);
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    if (ImGui::Combo("##interval", &index, k_interval_names.data(),
                     static_cast<int>(k_interval_names.size()))) {
        settings.interval_ms = processors::k_candle_intervals_ms[static_cast<std::size_t>(index)];
    }
    return processors::k_candle_intervals_ms[static_cast<std::size_t>(index)];
}

class CandlesPanel final : public Panel {
  public:
    PanelKind kind() const override { return PanelKind::Candles; }

    void draw(const runtime::Engine &engine, PanelSettings &settings) override {
        const auto selection = panel_header(engine, kind(), settings, feed_q, feed_age);
        const auto interval  = interval_selector(settings);
        const auto venues    = venues_of(kind(), selection);
        const auto primary   = engine.view(venues.front()).candles.candles(interval);
        if (primary.empty()) {
            empty_state("No candles yet (preloading from REST)");
            return;
        }
        ImPlot::GetStyle().UseLocalTime = !display_prefs().utc_time;
        const float price_height        = ImGui::GetContentRegionAvail().y * 0.72f;
        if (ImPlot::BeginPlot("##price", ImVec2(-1, price_height), ImPlotFlags_None)) {
            ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_AutoFit,
                              ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_Opposite);
            ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
            draw_bodies(primary, static_cast<double>(interval) / 1000.0);
            // Both: overlay the other venue's close as a line (packet §5 #5).
            if (venues.size() == 2) {
                for (const auto v : venues) {
                    const auto series = engine.view(v).candles.candles(interval);
                    xs_.clear();
                    ys_.clear();
                    for (const auto &c : series) {
                        xs_.push_back(static_cast<double>(c.open_time_ms) / 1000.0);
                        ys_.push_back(num(c.close));
                    }
                    ImPlot::SetNextLineStyle(venue_color(v));
                    ImPlot::PlotLine((std::string(venue_name(v)) + " close").c_str(), xs_.data(),
                                     ys_.data(), static_cast<int>(xs_.size()));
                }
            }
            ImPlot::EndPlot();
        }
        if (ImPlot::BeginPlot("##volume", ImVec2(-1, -1), ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes(nullptr, "Vol", ImPlotAxisFlags_AutoFit,
                              ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_Opposite);
            ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
            xs_.clear();
            ys_.clear();
            for (const auto &c : primary) {
                xs_.push_back(static_cast<double>(c.open_time_ms) / 1000.0);
                ys_.push_back(num(c.base_volume));
            }
            ImPlot::PlotBars("volume", xs_.data(), ys_.data(), static_cast<int>(xs_.size()),
                             static_cast<double>(interval) / 1000.0 * 0.7);
            ImPlot::EndPlot();
        }
    }

  private:
    // Candle bodies and wicks via the plot draw list (ImPlot has no built-in OHLC).
    static void draw_bodies(std::span<const domain::Candle> candles, double width_s) {
        auto *draw = ImPlot::GetPlotDrawList();
        ImPlot::PushPlotClipRect();
        const double half = width_s * 0.35;
        for (const auto &c : candles) {
            const double t   = static_cast<double>(c.open_time_ms) / 1000.0 + width_s / 2;
            const bool up    = !(c.close < c.open);
            const auto color = ImGui::GetColorU32(up ? k_buy : k_sell);
            draw->AddLine(ImPlot::PlotToPixels(t, num(c.low)), ImPlot::PlotToPixels(t, num(c.high)),
                          color);
            draw->AddRectFilled(ImPlot::PlotToPixels(t - half, num(up ? c.close : c.open)),
                                ImPlot::PlotToPixels(t + half, num(up ? c.open : c.close)), color);
        }
        ImPlot::PopPlotClipRect();
        // Invisible series so AutoFit covers the candles.
        static thread_local std::vector<double> x, y;
        x.clear();
        y.clear();
        for (const auto &c : candles) {
            x.push_back(static_cast<double>(c.open_time_ms) / 1000.0);
            y.push_back(num(c.low));
            x.push_back(static_cast<double>(c.open_time_ms) / 1000.0 + width_s);
            y.push_back(num(c.high));
        }
        ImPlot::SetNextMarkerStyle(ImPlotMarker_None);
        ImPlot::SetNextLineStyle(ImVec4(0, 0, 0, 0));
        ImPlot::PlotScatter("##fit", x.data(), y.data(), static_cast<int>(x.size()));
    }

    std::vector<double> xs_, ys_;
};

} // namespace

std::unique_ptr<Panel> make_candles() {
    return std::make_unique<CandlesPanel>();
}

} // namespace market_classifier::ui::panels
