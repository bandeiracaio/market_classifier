#include "market_classifier/runtime/clock.hpp"
#include "market_classifier/runtime/engine.hpp"
#include "market_classifier/ui/panel.hpp"

#include "imgui.h"
#include "implot.h"

#include <catch2/catch_test_macros.hpp>
#include <set>
#include <vector>

#include "fixture_util.hpp"

using namespace market_classifier;

TEST_CASE("panel traits cover every kind once with packet venue support") {
    const auto traits = ui::panel_traits();
    REQUIRE(traits.size() == 13);
    std::set<ui::PanelKind> kinds;
    for (const auto &t : traits) {
        kinds.insert(t.kind);
        CHECK_FALSE(t.title.empty());
    }
    CHECK(kinds.size() == 13);
    const auto both = [&](ui::PanelKind k) { return ui::traits_of(k).supports_both; };
    CHECK(both(ui::PanelKind::Overview));
    CHECK(both(ui::PanelKind::TradesTape));
    CHECK(both(ui::PanelKind::Depth));
    CHECK(both(ui::PanelKind::Candles));
    CHECK(both(ui::PanelKind::Cvd));
    CHECK(both(ui::PanelKind::Liquidations));
    CHECK(both(ui::PanelKind::BboSpread));
    CHECK(both(ui::PanelKind::FundingOi));
    CHECK_FALSE(both(ui::PanelKind::Ladder));
    CHECK_FALSE(both(ui::PanelKind::Footprint));
    CHECK_FALSE(both(ui::PanelKind::VolumeProfile));
    CHECK_FALSE(both(ui::PanelKind::Heatmap));
    CHECK_FALSE(both(ui::PanelKind::Diagnostics));
}

TEST_CASE("venue selection clamps Both for single-venue panels") {
    CHECK(ui::effective_venue(ui::PanelKind::Ladder, ui::VenueSelection::Both) ==
          ui::VenueSelection::Binance);
    CHECK(ui::effective_venue(ui::PanelKind::Ladder, ui::VenueSelection::Hyperliquid) ==
          ui::VenueSelection::Hyperliquid);
    CHECK(ui::effective_venue(ui::PanelKind::Cvd, ui::VenueSelection::Both) ==
          ui::VenueSelection::Both);
}

TEST_CASE("quality badges carry text, never color alone") {
    for (int q = 0; q <= static_cast<int>(domain::DataQuality::Failed); ++q) {
        const auto label = ui::quality_label(static_cast<domain::DataQuality>(q));
        CHECK_FALSE(label.empty());
    }
    CHECK(ui::quality_label(domain::DataQuality::Unsupported) == "Unsupported");
}

TEST_CASE("every panel draws headless over empty and replayed engines") {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    auto &io              = ImGui::GetIO();
    io.DisplaySize        = ImVec2(1600, 1000);
    io.DeltaTime          = 1.0f / 60.0f;
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    runtime::FakeClock clock(1790488600000, 0);
    runtime::Engine engine(clock);
    std::vector<std::unique_ptr<ui::Panel>> panels;
    for (const auto &t : ui::panel_traits()) {
        auto p = ui::make_panel(t.kind);
        REQUIRE(p != nullptr);
        CHECK(p->kind() == t.kind);
        panels.push_back(std::move(p));
    }
    const auto draw_all = [&](ui::VenueSelection venue) {
        ImGui::NewFrame();
        for (auto &p : panels) {
            ui::PanelSettings settings;
            settings.venue = venue;
            ImGui::Begin(std::string(ui::traits_of(p->kind()).title).c_str());
            p->draw(engine, settings);
            ImGui::End();
        }
        ImGui::Render();
    };
    for (const auto v :
         {ui::VenueSelection::Binance, ui::VenueSelection::Hyperliquid, ui::VenueSelection::Both}) {
        draw_all(v);
    }
    // Populate with captured data and draw again.
    engine.on_socket_event(domain::Venue::BinanceUsdM, runtime::SocketEvent::Open);
    engine.on_socket_event(domain::Venue::Hyperliquid, runtime::SocketEvent::Open);
    const auto submit = [&](venues::StreamTag tag, const std::string &payload) {
        const auto bytes =
            bridge::encode_raw_frames(std::vector<bridge::RawFrame>{{tag, 1790488600000, payload}});
        REQUIRE(engine.submit_raw(bytes) == runtime::RawSubmit::Accepted);
        engine.frame(16);
    };
    for (const auto *f : {"aggTrade", "bookTicker", "markPrice", "ticker", "kline", "forceOrder"}) {
        for (const auto &frame :
             mc_test::split_array(mc_test::read_file(std::string("binance/") + f + ".json"))) {
            submit(venues::StreamTag::BinanceWs, frame);
        }
    }
    for (const auto *f : {"l2Book", "trades", "bbo", "activeAssetCtx", "candle"}) {
        for (const auto &frame :
             mc_test::split_array(mc_test::read_file(std::string("hyperliquid/") + f + ".json"))) {
            submit(venues::StreamTag::HyperliquidWs, frame);
        }
    }
    for (const auto v :
         {ui::VenueSelection::Binance, ui::VenueSelection::Hyperliquid, ui::VenueSelection::Both}) {
        draw_all(v);
    }
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}
