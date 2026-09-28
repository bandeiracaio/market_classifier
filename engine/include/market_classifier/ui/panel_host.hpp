#pragma once

#include "market_classifier/ui/panel.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace market_classifier::ui {

inline constexpr std::size_t k_max_open_panels = 32;

struct OpenPanel {
    std::uint32_t id = 0;
    PanelKind kind   = PanelKind::Overview;
    PanelSettings settings;
    std::unique_ptr<Panel> panel;
    bool open           = true;
    std::uint32_t scope = 0; // owning layout uid

    // Stable ImGui window name: visible title + hidden "###L<uid>_<id>" identity, so each
    // layout owns distinct windows and docking state.
    [[nodiscard]] std::string window_name() const;
};

// Owns the live panel instances and draws them as dockable windows.
class PanelHost {
  public:
    // Returns the new id, or 0 when k_max_open_panels is reached.
    std::uint32_t add(PanelKind kind, const PanelSettings &settings = {}, std::uint32_t id = 0);
    bool remove(std::uint32_t id);
    std::uint32_t duplicate(std::uint32_t id);
    void clear();
    // Layout uid used for new panels' window identities.
    void set_scope(std::uint32_t uid) noexcept { scope_ = uid; }
    [[nodiscard]] std::uint32_t scope() const noexcept { return scope_; }
    // Draws every open panel; closed windows are removed afterwards. Returns true when the
    // set of panels or any setting changed (workspace dirty).
    bool draw(const runtime::Engine &engine);

    [[nodiscard]] const std::vector<OpenPanel> &panels() const noexcept { return panels_; }

  private:
    std::vector<OpenPanel> panels_;
    std::uint32_t next_id_ = 1;
    std::uint32_t scope_   = 0;
};

} // namespace market_classifier::ui
