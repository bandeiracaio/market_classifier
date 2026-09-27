#include "market_classifier/ui/workspace_controller.hpp"

#include "market_classifier/ui/ui_state.hpp"

#include "imgui.h"
#include "imgui_internal.h" // DockBuilder API (docking branch)

#include <cstdio>
#include <cstring>
#include <sstream>

namespace market_classifier::ui {
namespace {

std::string dockspace_name(std::uint32_t uid) {
    return "RootDockSpace" + std::to_string(uid);
}

// Keeps only the ini sections owned by layout `uid`: its panel windows and its dockspace
// node tree. ImGui serializes every window/node of the session; without this filter a
// layout's ini would grow with every other layout ever opened.
std::string filter_ini(const std::string &ini, std::uint32_t uid, ImGuiID dockspace) {
    std::array<char, 16> dock_hex{};
    std::snprintf(dock_hex.data(), dock_hex.size(), "ID=0x%08X", dockspace);
    // ImGui saves "Title###id" windows under the "###" identity only: "[Window][L<uid>_<n>]".
    const std::string window_tag = "[Window][L" + std::to_string(uid) + "_";
    std::istringstream in(ini);
    std::string out;
    std::string line;
    enum class Section : std::uint8_t { Skip, Window, Docking } section = Section::Skip;
    bool in_our_dock                                                    = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.starts_with("[Window][")) {
            section = line.starts_with(window_tag) ? Section::Window : Section::Skip;
        } else if (line == "[Docking][Data]") {
            section = Section::Docking;
            out += line + "\n";
            continue;
        } else if (!line.empty() && line.front() == '[') {
            section = Section::Skip;
        }
        if (section == Section::Window) {
            out += line + "\n";
        } else if (section == Section::Docking && !line.empty()) {
            if (line.starts_with("DockSpace")) {
                in_our_dock = line.find(dock_hex.data()) != std::string::npos;
            } else if (line.front() != ' ') {
                in_our_dock = false;
            }
            if (in_our_dock) {
                out += line + "\n";
            }
        } else if (section == Section::Window && line.empty()) {
            out += "\n";
        }
    }
    return out;
}

} // namespace

WorkspaceController::WorkspaceController() : ws_(default_workspace()) {}

bool WorkspaceController::activate(std::size_t index) {
    if (index >= ws_.layouts.size()) {
        return false;
    }
    if (index != ws_.active) {
        capture_active();
        dirty_ = true;
    }
    ws_.active     = index;
    pending_apply_ = true;
    return true;
}

void WorkspaceController::prepare() {
    display_prefs().utc_time = ws_.utc_time;
    if (!pending_apply_) {
        return;
    }
    const auto &layout = ws_.layouts[ws_.active];
    host_.clear();
    host_.set_scope(layout.uid);
    for (const auto &p : layout.panels) {
        host_.add(p.kind, p.settings, p.id);
    }
    if (!layout.imgui_ini.empty()) {
        ImGui::LoadIniSettingsFromMemory(layout.imgui_ini.data(), layout.imgui_ini.size());
        pending_dock_ = false;
    } else {
        pending_dock_ = true;
    }
    pending_apply_ = false;
}

void WorkspaceController::capture_active() {
    if (ws_.active >= ws_.layouts.size() || pending_apply_) {
        return;
    }
    auto &layout = ws_.layouts[ws_.active];
    if (layout.builtin) {
        return; // presets are read-only; use "Save as" to keep changes
    }
    layout.panels.clear();
    for (const auto &p : host_.panels()) {
        layout.panels.push_back({p.id, p.kind, p.settings});
    }
    std::size_t size = 0;
    const char *ini  = ImGui::SaveIniSettingsToMemory(&size);
    layout.imgui_ini = filter_ini(std::string(ini, size), layout.uid, dockspace_id_);
}

// Preset arrangement: first panel large on the left (62%), the rest stacked on the right.
void WorkspaceController::build_preset_dock(ImGuiID dockspace_id) {
    const auto *viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);
    const auto &panels = host_.panels();
    if (panels.size() == 1) {
        ImGui::DockBuilderDockWindow(panels[0].window_name().c_str(), dockspace_id);
    } else if (!panels.empty()) {
        ImGuiID left  = 0;
        ImGuiID right = 0;
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.62F, &left, &right);
        ImGui::DockBuilderDockWindow(panels[0].window_name().c_str(), left);
        const std::size_t rest = panels.size() - 1;
        ImGuiID remaining      = right;
        for (std::size_t i = 1; i < panels.size(); ++i) {
            ImGuiID top    = remaining;
            ImGuiID bottom = 0;
            if (i < panels.size() - 1) {
                const float share = 1.0F / static_cast<float>(rest - (i - 1));
                ImGui::DockBuilderSplitNode(remaining, ImGuiDir_Up, share, &top, &bottom);
            }
            ImGui::DockBuilderDockWindow(panels[i].window_name().c_str(), top);
            remaining = bottom;
        }
    }
    ImGui::DockBuilderFinish(dockspace_id);
}

void WorkspaceController::layouts_menu() {
    if (!ImGui::BeginMenu("Layouts")) {
        return;
    }
    for (std::size_t i = 0; i < ws_.layouts.size(); ++i) {
        const auto &l = ws_.layouts[i];
        const auto label =
            l.name + (l.builtin ? "  (preset)" : "") + "##layout" + std::to_string(l.uid);
        if (ImGui::MenuItem(label.c_str(), nullptr, i == ws_.active)) {
            activate(i);
        }
    }
    ImGui::Separator();
    const bool user = !ws_.layouts[ws_.active].builtin;
    open_save_as_   = ImGui::MenuItem("Save as...") || open_save_as_;
    open_rename_    = ImGui::MenuItem("Rename...", nullptr, false, user) || open_rename_;
    if (ImGui::MenuItem("Duplicate")) {
        duplicate_active();
    }
    if (ImGui::MenuItem("Delete", nullptr, false, user)) {
        delete_active();
    }
    ImGui::Separator();
    open_reset_ = ImGui::MenuItem("Reset workspace...") || open_reset_;
    ImGui::EndMenu();
}

void WorkspaceController::menu_bar() {
    if (!ImGui::BeginMenuBar()) {
        return;
    }
    layouts_menu();
    if (ImGui::BeginMenu("Panels")) {
        for (const auto &t : panel_traits()) {
            if (ImGui::MenuItem(std::string(t.title).c_str()) && host_.add(t.kind) != 0) {
                dirty_ = true;
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        dirty_ = ImGui::MenuItem("UTC time", nullptr, &ws_.utc_time) || dirty_;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Export workspace...") && on_export_requested_) {
            on_export_requested_();
        }
        if (ImGui::MenuItem("Import workspace...") && on_import_requested_) {
            on_import_requested_();
        }
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void WorkspaceController::modals() {
    const auto name_modal = [&](const char *id, bool &open, auto &&apply) {
        if (open) {
            ImGui::OpenPopup(id);
            open = false;
        }
        if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Name", name_buffer_.data(), name_buffer_.size());
            const bool valid = valid_layout_name(name_buffer_.data());
            ImGui::BeginDisabled(!valid);
            if (ImGui::Button("OK")) {
                apply(std::string_view(name_buffer_.data()));
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    };
    name_modal("Save layout as", open_save_as_, [&](std::string_view n) { save_as(n); });
    name_modal("Rename layout", open_rename_, [&](std::string_view n) { rename_active(n); });
    if (open_reset_) {
        ImGui::OpenPopup("Reset workspace");
        open_reset_ = false;
    }
    if (ImGui::BeginPopupModal("Reset workspace", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Delete all saved layouts and restore the presets?");
        if (ImGui::Button("Reset")) {
            reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void WorkspaceController::frame(const runtime::Engine &engine) {
    const auto &layout      = ws_.layouts[ws_.active];
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    constexpr ImGuiWindowFlags k_root_flags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::Begin("##dockspace_root", nullptr, k_root_flags);
    ImGui::PopStyleVar(3);
    menu_bar();
    const ImGuiID dockspace_id = ImGui::GetID(dockspace_name(layout.uid).c_str());
    dockspace_id_              = dockspace_id;
    if (pending_dock_) {
        build_preset_dock(dockspace_id);
        pending_dock_ = false;
    }
    ImGui::DockSpace(dockspace_id, ImVec2(0.0F, 0.0F), ImGuiDockNodeFlags_PassthruCentralNode);
    modals();
    ImGui::End();

    if (host_.draw(engine)) {
        dirty_ = true;
        capture_active();
    }
    auto &io = ImGui::GetIO();
    if (io.WantSaveIniSettings) {
        io.WantSaveIniSettings = false;
        if (!ws_.layouts[ws_.active].builtin) {
            capture_active();
            dirty_ = true;
        }
    }
}

bool WorkspaceController::save_as(std::string_view name) {
    Layout layout;
    layout.name = std::string(name);
    for (const auto &p : host_.panels()) {
        layout.panels.push_back({p.id, p.kind, p.settings});
    }
    if (!add_layout(ws_, std::move(layout))) {
        return false;
    }
    // The new layout has its own window/dock scope, so its geometry starts from the
    // default arrangement.
    ws_.active     = ws_.layouts.size() - 1;
    pending_apply_ = true;
    dirty_         = true;
    return true;
}

bool WorkspaceController::rename_active(std::string_view name) {
    if (!rename_layout(ws_, ws_.active, name)) {
        return false;
    }
    dirty_ = true;
    return true;
}

bool WorkspaceController::delete_active() {
    if (!remove_layout(ws_, ws_.active)) {
        return false;
    }
    ws_.active     = 0;
    pending_apply_ = true;
    dirty_         = true;
    return true;
}

bool WorkspaceController::duplicate_active() {
    capture_active();
    const auto index = duplicate_layout(ws_, ws_.active);
    if (!index) {
        return false;
    }
    ws_.active     = *index;
    pending_apply_ = true;
    dirty_         = true;
    return true;
}

void WorkspaceController::reset() {
    ws_            = default_workspace();
    pending_apply_ = true;
    dirty_         = true;
}

std::string WorkspaceController::export_json() {
    capture_active();
    return encode_workspace(ws_);
}

CodecError WorkspaceController::import_json(std::string_view json) {
    auto decoded = decode_workspace(json);
    if (decoded.error != CodecError::None) {
        return decoded.error;
    }
    ws_            = std::move(decoded.value);
    pending_apply_ = true;
    dirty_         = true;
    return CodecError::None;
}

bool WorkspaceController::take_dirty() noexcept {
    const bool was = dirty_;
    dirty_         = false;
    return was;
}

} // namespace market_classifier::ui
