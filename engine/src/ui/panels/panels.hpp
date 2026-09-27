#pragma once

#include "market_classifier/ui/panel.hpp"

#include <memory>

// One factory per panel; each lives in its own translation unit in this directory.
namespace market_classifier::ui::panels {

std::unique_ptr<Panel> make_overview();
std::unique_ptr<Panel> make_trades_tape();
std::unique_ptr<Panel> make_ladder();
std::unique_ptr<Panel> make_depth();
std::unique_ptr<Panel> make_candles();
std::unique_ptr<Panel> make_cvd();
std::unique_ptr<Panel> make_footprint();
std::unique_ptr<Panel> make_volume_profile();
std::unique_ptr<Panel> make_heatmap();
std::unique_ptr<Panel> make_liquidations();
std::unique_ptr<Panel> make_bbo_spread();
std::unique_ptr<Panel> make_funding_oi();
std::unique_ptr<Panel> make_diagnostics();

} // namespace market_classifier::ui::panels
