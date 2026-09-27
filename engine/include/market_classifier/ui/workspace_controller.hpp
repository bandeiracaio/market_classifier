#pragma once

#include "market_classifier/ui/panel_host.hpp"
#include "market_classifier/ui/workspace.hpp"
#include "market_classifier/ui/workspace_codec.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace market_classifier::ui {

// Binds the persisted Workspace to live panels and ImGui docking state
// (docs/runtime/mvp-workspace.md). One per app.
class WorkspaceController {
  public:
    WorkspaceController();

    // Applies a pending layout switch (loads its ini). Call before ImGui::NewFrame: ImGui
    // only accepts LoadIniSettingsFromMemory outside a frame.
    void prepare();
    // Draws dockspace + menu bar + panels. Call between ImGui::NewFrame and Render.
    void frame(const runtime::Engine &engine);

    bool activate(std::size_t index);
    bool save_as(std::string_view name);
    bool rename_active(std::string_view name);
    bool delete_active();
    bool duplicate_active();
    void reset(); // defaults (caller confirms first)

    [[nodiscard]] std::string export_json(); // captures live docking first
    CodecError import_json(std::string_view json);
    // True once after any persisted change (layout set, active layout, prefs, geometry).
    [[nodiscard]] bool take_dirty() noexcept;

    [[nodiscard]] const Workspace &workspace() const noexcept { return ws_; }
    [[nodiscard]] const PanelHost &host() const noexcept { return host_; }
    [[nodiscard]] PanelHost &host_mut() noexcept { return host_; }

    // Host (browser) file actions requested from the File menu.
    void set_file_actions(std::function<void()> on_export, std::function<void()> on_import) {
        on_export_requested_ = std::move(on_export);
        on_import_requested_ = std::move(on_import);
    }

  private:
    void capture_active(); // live panels + ini -> active layout (user layouts only)
    void build_preset_dock(unsigned int dockspace_id);
    void menu_bar();
    void layouts_menu();
    void modals();

    Workspace ws_;
    PanelHost host_;
    bool dirty_                = false;
    bool pending_dock_         = true; // build DockBuilder arrangement on next frame
    bool pending_apply_        = true; // (re)create panels for the active layout
    unsigned int dockspace_id_ = 0;    // ImGuiID of the active layout's dockspace
    std::array<char, k_max_layout_name_bytes + 1> name_buffer_{};
    std::function<void()> on_export_requested_;
    std::function<void()> on_import_requested_;
    bool open_save_as_ = false, open_rename_ = false, open_reset_ = false;
};

} // namespace market_classifier::ui
