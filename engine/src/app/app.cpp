#include "app.hpp"

#include "market_classifier/bridge/browser_api.hpp"
#include "market_classifier/runtime/engine.hpp"
#include "market_classifier/ui/ui_state.hpp"
#include "market_classifier/ui/workspace_controller.hpp"

#include "imgui.h"

#include <cstdint>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace market_classifier::app {
namespace {

// Frame-loop drain budget for venue data (spec §7.2); the rest of the frame renders.
constexpr std::int64_t k_engine_budget_ms = 4;

#ifdef __EMSCRIPTEN__
// clang-format off
// File actions are performed by the Svelte host (download / file picker).
EM_JS(void, js_request_host_action, (const char *name), {
    window.dispatchEvent(new CustomEvent('mc-workspace', { detail: UTF8ToString(name) }));
});
// clang-format on
#else
void js_request_host_action(const char *) {}
#endif

ui::WorkspaceController &controller() {
    static ui::WorkspaceController instance;
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

} // namespace

void init() {
    ImGui::GetIO().IniFilename       = nullptr; // docking state persists per layout via the bridge
    controller().on_export_requested = [] { js_request_host_action("export"); };
    controller().on_import_requested = [] { js_request_host_action("import"); };
}

void prepare() {
    controller().prepare();
}

void frame() {
    auto &engine = bridge::app_engine();
    engine.frame(k_engine_budget_ms);
    controller().frame(engine);
    apply_actions(engine);
}

} // namespace market_classifier::app

// ---------------------------------------------------------------------------
// Workspace bridge exports (plan Task 10). JSON crosses the boundary as UTF-8.
// ---------------------------------------------------------------------------
namespace {
std::string g_export_buffer; // valid until the next mc_workspace_export call
}

extern "C" const char *mc_workspace_export() noexcept {
    try {
        g_export_buffer = market_classifier::app::controller().export_json();
    } catch (...) {
        g_export_buffer.clear();
    }
    return g_export_buffer.c_str();
}

// Returns ui::CodecError (0 = applied). Oversize input is rejected before parsing.
extern "C" int mc_workspace_import(const char *json, std::size_t size) noexcept {
    using market_classifier::ui::CodecError;
    if (json == nullptr || size > market_classifier::ui::k_max_workspace_json_bytes) {
        return static_cast<int>(CodecError::TooLarge);
    }
    try {
        return static_cast<int>(market_classifier::app::controller().import_json({json, size}));
    } catch (...) {
        return static_cast<int>(CodecError::Malformed);
    }
}

extern "C" void mc_workspace_reset() noexcept {
    try {
        market_classifier::app::controller().reset();
    } catch (...) {
    }
}

// Returns 1 once after each persisted change (autosave trigger), then 0.
extern "C" int mc_workspace_dirty() noexcept {
    return market_classifier::app::controller().take_dirty() ? 1 : 0;
}

// Test hooks (Playwright): open a panel / switch layout by index.
extern "C" int mc_debug_open_panel(int kind) noexcept {
    if (kind < 0 || kind >= static_cast<int>(market_classifier::ui::k_panel_kind_count)) {
        return 0;
    }
    try {
        return static_cast<int>(market_classifier::app::controller().host_mut().add(
            static_cast<market_classifier::ui::PanelKind>(kind)));
    } catch (...) {
        return 0;
    }
}

extern "C" int mc_debug_activate_layout(int index) noexcept {
    return index >= 0 &&
                   market_classifier::app::controller().activate(static_cast<std::size_t>(index))
               ? 1
               : 0;
}

extern "C" int mc_debug_save_layout_as(const char *name) noexcept {
    try {
        return name != nullptr && market_classifier::app::controller().save_as(name) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}
