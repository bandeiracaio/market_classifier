#include "market_classifier/ui/panel_host.hpp"

#include "imgui.h"

#include <algorithm>

namespace market_classifier::ui {

std::string OpenPanel::window_name() const {
    return std::string(traits_of(kind).title) + "###L" + std::to_string(scope) + "_" +
           std::to_string(id);
}

std::uint32_t PanelHost::add(PanelKind kind, const PanelSettings &settings, std::uint32_t id) {
    if (panels_.size() >= k_max_open_panels) {
        return 0;
    }
    if (id == 0 ||
        std::any_of(panels_.begin(), panels_.end(), [&](const auto &p) { return p.id == id; })) {
        id = next_id_;
    }
    next_id_ = std::max(next_id_, id + 1);
    OpenPanel p;
    p.id             = id;
    p.kind           = kind;
    p.settings       = settings;
    p.settings.venue = effective_venue(kind, settings.venue);
    p.panel          = make_panel(kind);
    p.scope          = scope_;
    panels_.push_back(std::move(p));
    return id;
}

bool PanelHost::remove(std::uint32_t id) {
    const auto before = panels_.size();
    std::erase_if(panels_, [&](const auto &p) { return p.id == id; });
    return panels_.size() != before;
}

std::uint32_t PanelHost::duplicate(std::uint32_t id) {
    const auto it =
        std::find_if(panels_.begin(), panels_.end(), [&](const auto &p) { return p.id == id; });
    if (it == panels_.end()) {
        return 0;
    }
    const auto kind     = it->kind;
    const auto settings = it->settings;
    return add(kind, settings);
}

void PanelHost::clear() {
    panels_.clear();
    next_id_ = 1;
}

bool PanelHost::draw(const runtime::Engine &engine) {
    bool changed               = false;
    std::uint32_t duplicate_of = 0;
    for (auto &p : panels_) {
        const auto before = p.settings;
        if (ImGui::Begin(p.window_name().c_str(), &p.open)) {
            if (ImGui::BeginPopupContextItem("panel_menu")) {
                if (ImGui::MenuItem("Duplicate")) {
                    duplicate_of = p.id;
                }
                if (ImGui::MenuItem("Close")) {
                    p.open = false;
                }
                ImGui::EndPopup();
            }
            p.panel->draw(engine, p.settings);
        }
        ImGui::End();
        changed = changed || !(before == p.settings) || !p.open;
    }
    const auto before = panels_.size();
    std::erase_if(panels_, [](const auto &p) { return !p.open; });
    changed = changed || panels_.size() != before;
    if (duplicate_of != 0 && duplicate(duplicate_of) != 0) {
        changed = true;
    }
    return changed;
}

} // namespace market_classifier::ui
