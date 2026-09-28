#pragma once

// Terminal UI loop body, independent of platform setup (main.cpp).

namespace market_classifier::app {

void init();
// Applies pending layout switches; call before ImGui::NewFrame.
void prepare();
// Drains the engine within its budget, draws dockspace, menus and panels, and applies
// panel-requested actions. Call between ImGui::NewFrame() and ImGui::Render().
void frame();

} // namespace market_classifier::app
