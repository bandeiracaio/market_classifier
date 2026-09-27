#include "implot.h"

#include "../ui_common.hpp"
#include "panels.hpp"

namespace market_classifier::ui::panels {
namespace {

using namespace detail;

constexpr std::size_t k_depth_levels = 400; // per side drawn

class DepthPanel final : public Panel {
  public:
    PanelKind kind() const override { return PanelKind::Depth; }

    void draw(const runtime::Engine &engine, PanelSettings &settings) override {
        const auto selection = panel_header(engine, kind(), settings, book_q, feed_age);
        if (!ImPlot::BeginPlot("##depth", ImVec2(-1, -1))) {
            return;
        }
        ImPlot::SetupAxes("Price", "Cumulative size (BTC)", ImPlotAxisFlags_AutoFit,
                          ImPlotAxisFlags_AutoFit);
        for (const auto v : venues_of(kind(), selection)) {
            if (engine.book_quality(v) != domain::DataQuality::Live) {
                continue; // never draw a non-live book as if it were current
            }
            const auto &book = engine.book(v);
            for (const bool bids : {true, false}) {
                const auto side = bids ? book.bids() : book.asks();
                xs_.clear();
                ys_.clear();
                double cum = 0;
                for (std::size_t i = 0; i < side.size() && i < k_depth_levels; ++i) {
                    cum += num(side[i].quantity);
                    xs_.push_back(num(side[i].price));
                    ys_.push_back(cum);
                }
                const auto label = std::string(venue_name(v)) + (bids ? " bids" : " asks");
                ImPlot::SetNextLineStyle(bids ? k_buy : k_sell);
                ImPlot::PlotStairs(label.c_str(), xs_.data(), ys_.data(),
                                   static_cast<int>(xs_.size()));
            }
        }
        ImPlot::EndPlot();
    }

  private:
    std::vector<double> xs_, ys_;
};

} // namespace

std::unique_ptr<Panel> make_depth() {
    return std::make_unique<DepthPanel>();
}

} // namespace market_classifier::ui::panels
