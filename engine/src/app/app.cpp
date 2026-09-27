#include "app.hpp"

#include "market_classifier/bridge/browser_api.hpp"
#include "market_classifier/runtime/engine.hpp"
#include "market_classifier/ui/panel_host.hpp"
#include "market_classifier/ui/ui_state.hpp"

#include "imgui.h"

#include <cstdint>

namespace market_classifier::app {
namespace {

// Frame-loop drain budget for venue data (spec §7.2); the rest of the frame renders.
constexpr std::int64_t k_engine_budget_ms = 4;

ui::PanelHost &host() {
    static ui::PanelHost instance;
    return instance;
}

void apply_actions(runtime::Engine &engine) {
    auto &actions = ui::ui_actions();
    if (actions.retry_venue) {
        engine.retry(*actions.retry_venue);
    }
    for (const auto v : {domain::Venue::BinanceUsdM, domain::Venue::Hyperliquid}) {
        const auto i = runtime::venue_index(v);
        engine.set_cvd_daily_reset(v, actions.cvd_daily_reset[i]);
        if (actions.cvd_reset_now[i]) {
            engine.reset_cvd(v);
        }
    }
    actions = {};
}

void dockspace() {
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_MenuBar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##dockspace_root", nullptr, flags);
    ImGui::PopStyleVar(3);
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Panels")) {
            for (const auto &t : ui::panel_traits()) {
                if (ImGui::MenuItem(std::string(t.title).c_str())) {
                    host().add(t.kind);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("UTC time", nullptr, &ui::display_prefs().utc_time);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    ImGui::DockSpace(ImGui::GetID("RootDockSpace"), ImVec2(0.0f, 0.0f),
                     ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

} // namespace

void init() {
    host().add(ui::PanelKind::Overview);
    host().add(ui::PanelKind::Diagnostics);
}

void frame() {
    auto &engine = bridge::app_engine();
    engine.frame(k_engine_budget_ms);
    dockspace();
    host().draw(engine);
    apply_actions(engine);
}

} // namespace market_classifier::app

// Test hook (Playwright panel smoke): opens a panel of the given kind; returns its id or 0.
extern "C" int mc_debug_open_panel(int kind) noexcept {
    if (kind < 0 || kind >= static_cast<int>(market_classifier::ui::k_panel_kind_count)) {
        return 0;
    }
    try {
        return static_cast<int>(market_classifier::app::host().add(
            static_cast<market_classifier::ui::PanelKind>(kind)));
    } catch (...) {
        return 0;
    }
}
