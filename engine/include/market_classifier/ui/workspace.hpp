#pragma once

#include "market_classifier/ui/panel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace market_classifier::ui {

inline constexpr std::uint32_t k_workspace_schema_version = 1;
inline constexpr std::size_t k_max_user_layouts           = 32;
inline constexpr std::size_t k_max_panels_per_layout      = 32;
inline constexpr std::size_t k_max_workspace_json_bytes   = 256 * 1024;
inline constexpr std::size_t k_max_layout_name_bytes      = 64;
inline constexpr std::size_t k_builtin_layout_count       = 5;
inline constexpr std::uint32_t k_max_layout_uid           = 1'000'000;

struct PanelInstance {
    std::uint32_t id = 0;
    PanelKind kind   = PanelKind::Overview;
    PanelSettings settings;
};

struct Layout {
    std::uint32_t uid = 0; // stable per layout; scopes ImGui window and dockspace ids
    std::string name;
    bool builtin = false;
    std::vector<PanelInstance> panels;
    std::string imgui_ini; // empty => preset dock arrangement is built in code
};

// Builtin presets first (read-only), then user layouts.
struct Workspace {
    std::vector<Layout> layouts;
    std::size_t active = 0;
    bool utc_time      = false;
};

// The five presets from packet §7; active = Overview.
[[nodiscard]] Workspace default_workspace();

[[nodiscard]] bool valid_layout_name(std::string_view name);
// Adds a user layout; false when names/bounds are invalid.
bool add_layout(Workspace &ws, Layout layout);
bool rename_layout(Workspace &ws, std::size_t index, std::string_view name);
bool remove_layout(Workspace &ws, std::size_t index);
std::optional<std::size_t> duplicate_layout(Workspace &ws, std::size_t index);

} // namespace market_classifier::ui
