#include "market_classifier/ui/workspace.hpp"
#include "market_classifier/ui/workspace_codec.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "fixture_util.hpp"

using namespace market_classifier;
using ui::PanelKind;

namespace {

std::vector<PanelKind> kinds(const ui::Layout &layout) {
    std::vector<PanelKind> out;
    for (const auto &p : layout.panels) {
        out.push_back(p.kind);
    }
    return out;
}

ui::Workspace with_user_layout() {
    auto ws = ui::default_workspace();
    ui::Layout mine;
    mine.name = "Mine \"quoted\"";
    mine.panels.push_back(
        {7, PanelKind::Cvd, {ui::VenueSelection::Both, 300'000, 2, 25'000.5, true, 3}});
    mine.panels.push_back({9, PanelKind::Ladder, {}});
    mine.imgui_ini = "[Window][Trades###panel7]\nPos=10,20\nSize=300,200\n";
    REQUIRE(ui::add_layout(ws, mine));
    ws.active   = ws.layouts.size() - 1;
    ws.utc_time = true;
    return ws;
}

} // namespace

TEST_CASE("default workspace has the five packet presets, Overview active") {
    const auto ws = ui::default_workspace();
    REQUIRE(ws.layouts.size() == 5);
    CHECK(ws.active == 0);
    CHECK_FALSE(ws.utc_time);
    for (const auto &l : ws.layouts) {
        CHECK(l.builtin);
    }
    CHECK(ws.layouts[0].name == "Overview");
    CHECK(kinds(ws.layouts[0]) == std::vector{PanelKind::Overview, PanelKind::BboSpread,
                                              PanelKind::FundingOi, PanelKind::Candles});
    CHECK(ws.layouts[1].name == "Tape Reader");
    CHECK(kinds(ws.layouts[1]) == std::vector{PanelKind::Candles, PanelKind::TradesTape,
                                              PanelKind::Ladder, PanelKind::Cvd});
    CHECK(ws.layouts[2].name == "Footprint");
    CHECK(kinds(ws.layouts[2]) == std::vector{PanelKind::Footprint, PanelKind::VolumeProfile,
                                              PanelKind::Cvd, PanelKind::Liquidations});
    CHECK(ws.layouts[3].name == "Liquidity");
    CHECK(kinds(ws.layouts[3]) == std::vector{PanelKind::Heatmap, PanelKind::Depth,
                                              PanelKind::Ladder, PanelKind::BboSpread});
    CHECK(ws.layouts[4].name == "Derivatives");
    CHECK(kinds(ws.layouts[4]) == std::vector{PanelKind::Candles, PanelKind::FundingOi,
                                              PanelKind::Liquidations, PanelKind::Overview});
    // Derivatives preset shows candles and funding for Both venues.
    CHECK(ws.layouts[4].panels[0].settings.venue == ui::VenueSelection::Both);
    CHECK(ws.layouts[4].panels[1].settings.venue == ui::VenueSelection::Both);
}

TEST_CASE("encode then decode round-trips user layouts and preferences") {
    const auto ws      = with_user_layout();
    const auto json    = ui::encode_workspace(ws);
    const auto decoded = ui::decode_workspace(json);
    REQUIRE(decoded.error == ui::CodecError::None);
    REQUIRE(decoded.value.layouts.size() == 6);
    const auto &mine = decoded.value.layouts.back();
    CHECK(mine.name == "Mine \"quoted\"");
    CHECK_FALSE(mine.builtin);
    CHECK(mine.imgui_ini == ws.layouts.back().imgui_ini);
    REQUIRE(mine.panels.size() == 2);
    CHECK(mine.panels[0].id == 7);
    CHECK(mine.panels[0].kind == PanelKind::Cvd);
    CHECK(mine.panels[0].settings == ws.layouts.back().panels[0].settings);
    CHECK(decoded.value.active == 5);
    CHECK(decoded.value.utc_time);
    CHECK(ui::encode_workspace(decoded.value) == json); // canonical
}

TEST_CASE("decode rejects oversize, bad version, too many layouts, unknown kinds") {
    CHECK(ui::decode_workspace(std::string(ui::k_max_workspace_json_bytes + 1, ' ')).error ==
          ui::CodecError::TooLarge);
    CHECK(ui::decode_workspace(R"({"schema":99,"layouts":[],"active":0,"utcTime":false})").error ==
          ui::CodecError::UnsupportedVersion);
    CHECK(ui::decode_workspace("{not json").error == ui::CodecError::Malformed);
    CHECK(
        ui::decode_workspace(
            R"({"schema":1,"layouts":[{"name":"x","panels":[{"id":1,"kind":"Nope"}],"imguiIni":""}],"active":0,"utcTime":false})")
            .error == ui::CodecError::Malformed);
    CHECK(ui::decode_workspace(R"({"schema":1,"layouts":[],"active":99,"utcTime":false})").error ==
          ui::CodecError::OutOfBounds);

    std::string many = R"({"schema":1,"active":0,"utcTime":false,"layouts":[)";
    for (std::size_t i = 0; i <= ui::k_max_user_layouts; ++i) {
        many += (i == 0 ? "" : ",");
        many += R"({"name":"L)" + std::to_string(i) + R"(","panels":[],"imguiIni":""})";
    }
    many += "]}";
    CHECK(ui::decode_workspace(many).error == ui::CodecError::OutOfBounds);

    std::string panels =
        R"({"schema":1,"active":0,"utcTime":false,"layouts":[{"name":"p","imguiIni":"","panels":[)";
    for (std::size_t i = 0; i <= ui::k_max_panels_per_layout; ++i) {
        panels += (i == 0 ? "" : ",");
        panels += R"({"id":)" + std::to_string(i + 1) + R"(,"kind":"Cvd"})";
    }
    panels += "]}]}";
    CHECK(ui::decode_workspace(panels).error == ui::CodecError::OutOfBounds);
}

TEST_CASE("builtin layouts cannot be deleted or renamed; user layouts can") {
    auto ws = with_user_layout();
    CHECK_FALSE(ui::remove_layout(ws, 0));
    CHECK_FALSE(ui::rename_layout(ws, 1, "x"));
    CHECK(ws.layouts.size() == 6);
    CHECK(ui::rename_layout(ws, 5, "Renamed"));
    CHECK(ws.layouts[5].name == "Renamed");
    CHECK_FALSE(ui::rename_layout(ws, 5, ""));                   // invalid name
    CHECK_FALSE(ui::rename_layout(ws, 5, std::string(65, 'a'))); // too long
    const auto copy = ui::duplicate_layout(ws, 0);               // builtin -> user copy
    REQUIRE(copy);
    CHECK_FALSE(ws.layouts[*copy].builtin);
    CHECK(ws.layouts[*copy].name == "Overview (copy)");
    CHECK(ui::remove_layout(ws, 5));
    CHECK(ws.active < ws.layouts.size());
}

TEST_CASE("v0 workspace migrates to v1") {
    const auto text    = mc_test::read_file("workspace-v0.json");
    const auto decoded = ui::decode_workspace(text);
    REQUIRE(decoded.error == ui::CodecError::None);
    CHECK_FALSE(decoded.value.utc_time);
    REQUIRE(decoded.value.layouts.size() == 6);
    CHECK(decoded.value.layouts.back().name == "Old layout");
    CHECK(decoded.value.layouts.back().panels.at(0).kind == PanelKind::TradesTape);
    CHECK(decoded.value.active == 5);
}
