#include "market_classifier/runtime/clock.hpp"
#include "market_classifier/runtime/engine.hpp"
#include "market_classifier/ui/workspace_codec.hpp"
#include "market_classifier/ui/workspace_controller.hpp"

#include "imgui.h"
#include "implot.h"

#include <catch2/catch_test_macros.hpp>

using namespace market_classifier;

namespace {

struct Headless {
    Headless() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        auto &io       = ImGui::GetIO();
        io.DisplaySize = ImVec2(1600, 1000);
        io.DeltaTime   = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        unsigned char *pixels = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    }
    ~Headless() {
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

void run_frames(ui::WorkspaceController &c, const runtime::Engine &e, int n = 3) {
    for (int i = 0; i < n; ++i) {
        c.prepare();
        ImGui::NewFrame();
        c.frame(e);
        ImGui::Render();
    }
}

} // namespace

TEST_CASE("first run opens the Overview preset") {
    Headless h;
    runtime::FakeClock clock;
    runtime::Engine engine(clock);
    ui::WorkspaceController c;
    run_frames(c, engine);
    CHECK(c.workspace().active == 0);
    CHECK(c.host().panels().size() == 4);
    CHECK(c.host().panels()[0].kind == ui::PanelKind::Overview);
}

TEST_CASE("each preset opens and builds a dock layout") {
    Headless h;
    runtime::FakeClock clock;
    runtime::Engine engine(clock);
    ui::WorkspaceController c;
    for (std::size_t i = 0; i < ui::k_builtin_layout_count; ++i) {
        REQUIRE(c.activate(i));
        run_frames(c, engine);
        CHECK(c.host().panels().size() == c.workspace().layouts[i].panels.size());
    }
}

TEST_CASE("save as, export, reset, import restores the user layout") {
    Headless h;
    runtime::FakeClock clock;
    runtime::Engine engine(clock);
    ui::WorkspaceController c;
    run_frames(c, engine);
    c.host_mut().add(ui::PanelKind::Diagnostics);
    run_frames(c, engine);
    REQUIRE(c.save_as("Mine"));
    run_frames(c, engine);
    CHECK(c.take_dirty());
    CHECK_FALSE(c.take_dirty());
    const auto active = c.workspace().active;
    CHECK(c.workspace().layouts[active].name == "Mine");
    CHECK(c.workspace().layouts[active].panels.size() == 5);

    const auto exported = c.export_json(); // captures live docking into the layout
    CHECK_FALSE(c.workspace().layouts[active].imgui_ini.empty());
    CHECK(c.workspace().layouts[active].imgui_ini.find("[Window][L6_") != std::string::npos);
    CHECK(c.workspace().layouts[active].imgui_ini.find("[Window][L1_") == std::string::npos);
    CHECK(c.workspace().layouts[active].imgui_ini.find("DockSpace") != std::string::npos);
    c.reset();
    run_frames(c, engine);
    CHECK(c.workspace().layouts.size() == ui::k_builtin_layout_count);
    CHECK(c.workspace().active == 0);

    CHECK(c.import_json(exported) == ui::CodecError::None);
    run_frames(c, engine);
    CHECK(c.workspace().layouts[c.workspace().active].name == "Mine");
    CHECK(c.host().panels().size() == 5);

    CHECK(c.import_json("{garbage") == ui::CodecError::Malformed);
    CHECK(c.workspace().layouts[c.workspace().active].name == "Mine"); // unchanged on error
}

TEST_CASE("builtin presets stay read-only through the controller") {
    Headless h;
    runtime::FakeClock clock;
    runtime::Engine engine(clock);
    ui::WorkspaceController c;
    run_frames(c, engine);
    CHECK_FALSE(c.rename_active("x"));
    CHECK_FALSE(c.delete_active());
    REQUIRE(c.duplicate_active());
    CHECK_FALSE(c.workspace().layouts[c.workspace().active].builtin);
    CHECK(c.rename_active("Copy renamed"));
    CHECK(c.delete_active());
    CHECK(c.workspace().active == 0);
}
