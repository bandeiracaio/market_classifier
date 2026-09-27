#include "market_classifier/ui/workspace.hpp"

#include <algorithm>

namespace market_classifier::ui {
namespace {

PanelInstance panel(std::uint32_t id, PanelKind kind,
                    VenueSelection venue = VenueSelection::Binance) {
    PanelInstance p;
    p.id             = id;
    p.kind           = kind;
    p.settings.venue = effective_venue(kind, venue);
    return p;
}

Layout preset(std::uint32_t uid, std::string name, std::vector<PanelInstance> panels) {
    Layout l;
    l.uid     = uid;
    l.name    = std::move(name);
    l.builtin = true;
    l.panels  = std::move(panels);
    return l;
}

} // namespace

// Packet §7. The first panel of each preset is the "large" one in the dock arrangement
// (app/presets.cpp).
Workspace default_workspace() {
    using K         = PanelKind;
    const auto both = VenueSelection::Both;
    Workspace ws;
    ws.layouts.push_back(preset(1, "Overview",
                                {panel(1, K::Overview, both), panel(2, K::BboSpread, both),
                                 panel(3, K::FundingOi, both), panel(4, K::Candles)}));
    ws.layouts.push_back(preset(
        2, "Tape Reader",
        {panel(1, K::Candles), panel(2, K::TradesTape), panel(3, K::Ladder), panel(4, K::Cvd)}));
    ws.layouts.push_back(preset(3, "Footprint",
                                {panel(1, K::Footprint), panel(2, K::VolumeProfile),
                                 panel(3, K::Cvd), panel(4, K::Liquidations)}));
    ws.layouts.push_back(preset(
        4, "Liquidity",
        {panel(1, K::Heatmap), panel(2, K::Depth), panel(3, K::Ladder), panel(4, K::BboSpread)}));
    ws.layouts.push_back(preset(5, "Derivatives",
                                {panel(1, K::Candles, both), panel(2, K::FundingOi, both),
                                 panel(3, K::Liquidations, both), panel(4, K::Overview, both)}));
    return ws;
}

bool valid_layout_name(std::string_view name) {
    if (name.empty() || name.size() > k_max_layout_name_bytes) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u >= 0x20 && u != 0x7F; // printable; UTF-8 continuation bytes allowed
    });
}

namespace {
std::uint32_t next_uid(const Workspace &ws) {
    auto uid = static_cast<std::uint32_t>(k_builtin_layout_count);
    for (const auto &l : ws.layouts) {
        uid = std::max(uid, l.uid);
    }
    return uid + 1;
}

std::size_t user_count(const Workspace &ws) {
    return static_cast<std::size_t>(std::count_if(ws.layouts.begin(), ws.layouts.end(),
                                                  [](const Layout &l) { return !l.builtin; }));
}
} // namespace

bool add_layout(Workspace &ws, Layout layout) {
    if (!valid_layout_name(layout.name) || layout.panels.size() > k_max_panels_per_layout ||
        user_count(ws) >= k_max_user_layouts) {
        return false;
    }
    layout.builtin   = false;
    const bool taken = std::any_of(ws.layouts.begin(), ws.layouts.end(),
                                   [&](const Layout &l) { return l.uid == layout.uid; });
    if (layout.uid == 0 || taken) {
        layout.uid = next_uid(ws);
    }
    if (layout.uid > k_max_layout_uid) {
        return false;
    }
    ws.layouts.push_back(std::move(layout));
    return true;
}

bool rename_layout(Workspace &ws, std::size_t index, std::string_view name) {
    if (index >= ws.layouts.size() || ws.layouts[index].builtin || !valid_layout_name(name)) {
        return false;
    }
    ws.layouts[index].name = std::string(name);
    return true;
}

bool remove_layout(Workspace &ws, std::size_t index) {
    if (index >= ws.layouts.size() || ws.layouts[index].builtin) {
        return false;
    }
    ws.layouts.erase(ws.layouts.begin() + static_cast<std::ptrdiff_t>(index));
    if (ws.active == index) {
        ws.active = 0;
    } else if (ws.active > index) {
        --ws.active;
    }
    return true;
}

std::optional<std::size_t> duplicate_layout(Workspace &ws, std::size_t index) {
    if (index >= ws.layouts.size()) {
        return std::nullopt;
    }
    auto copy = ws.layouts[index];
    copy.uid  = 0; // fresh window/dock scope; geometry is rebuilt, not shared
    copy.imgui_ini.clear();
    copy.name = copy.name.substr(0, k_max_layout_name_bytes - 7) + " (copy)";
    if (!add_layout(ws, std::move(copy))) {
        return std::nullopt;
    }
    return ws.layouts.size() - 1;
}

} // namespace market_classifier::ui
