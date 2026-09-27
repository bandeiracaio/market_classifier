#include "market_classifier/processors/metrics.hpp"

#include "../ui_common.hpp"
#include "panels.hpp"

namespace market_classifier::ui::panels {
namespace {

using namespace detail;

// Always both venues side by side, plus the cross-venue basis (packet §5 #1).
class OverviewPanel final : public Panel {
  public:
    PanelKind kind() const override { return PanelKind::Overview; }

    void draw(const runtime::Engine &engine, PanelSettings &settings) override {
        settings.venue = VenueSelection::Both;
        const std::array<domain::Venue, 2> venues{domain::Venue::BinanceUsdM,
                                                  domain::Venue::Hyperliquid};
        if (!ImGui::BeginTable("overview", 3,
                               ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
            return;
        }
        ImGui::TableSetupColumn("Field");
        ImGui::TableSetupColumn("Binance BTCUSDT");
        ImGui::TableSetupColumn("Hyperliquid BTC");
        ImGui::TableHeadersRow();
        const auto row = [&](const char *label, auto &&cell) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            for (std::size_t i = 0; i < venues.size(); ++i) {
                ImGui::TableSetColumnIndex(static_cast<int>(i) + 1);
                cell(venues[i], engine.view(venues[i]).metrics.view());
            }
        };
        row("Status", [&](domain::Venue v, const processors::MetricsView &) {
            draw_quality_badge(engine.feed_quality(v), feed_age(engine, v));
        });
        row("Last", [&](domain::Venue v, const processors::MetricsView &m) {
            if (v == domain::Venue::BinanceUsdM) {
                ImGui::TextUnformatted(m.summary ? text(m.summary->last_price).c_str() : "-");
            } else {
                const auto &tape = engine.view(v).tape.trades();
                ImGui::TextUnformatted(tape.empty() ? "-" : text(tape.back().price).c_str());
            }
        });
        row("Mark", [](domain::Venue, const processors::MetricsView &m) {
            ImGui::TextUnformatted(m.asset ? text(m.asset->mark_price).c_str() : "-");
        });
        row("Index / Oracle", [](domain::Venue, const processors::MetricsView &m) {
            if (!m.asset) {
                ImGui::TextUnformatted("-");
            } else if (m.asset->index_price) {
                ImGui::Text("%s (index)", text(m.asset->index_price).c_str());
            } else {
                ImGui::Text("%s (oracle)", text(m.asset->oracle_price).c_str());
            }
        });
        row("24h change", [](domain::Venue, const processors::MetricsView &m) {
            if (!m.summary || !m.summary->change_24h) {
                ImGui::TextUnformatted("-");
                return;
            }
            const double c = num(*m.summary->change_24h);
            ImGui::TextColored(c >= 0 ? k_buy : k_sell, "%s%s", c >= 0 ? "+" : "",
                               text(m.summary->change_24h).c_str());
        });
        row("24h volume (USD)", [](domain::Venue, const processors::MetricsView &m) {
            ImGui::TextUnformatted(m.summary && m.summary->volume_24h
                                       ? grouped(num(*m.summary->volume_24h), 0).c_str()
                                       : "-");
        });
        row("Funding", [&](domain::Venue v, const processors::MetricsView &m) {
            if (!m.asset) {
                ImGui::TextUnformatted("-");
                return;
            }
            const double annual = num(processors::annualized_funding(m.asset->funding_rate, v));
            const auto next     = m.asset->next_funding_time_ms
                                      ? *m.asset->next_funding_time_ms
                                      : processors::next_hourly_funding_ms(engine.wall_ms());
            ImGui::Text("%.4f%% (%.2f%%/y) in %s", num(m.asset->funding_rate) * 100.0,
                        annual * 100.0, format_duration(next - engine.wall_ms()).c_str());
        });
        row("Open interest", [&](domain::Venue, const processors::MetricsView &m) {
            if (!m.open_interest_last) {
                ImGui::TextUnformatted("-");
                return;
            }
            const auto age = format_duration(engine.wall_ms() - m.oi_update_ms);
            if (m.oi_sample_interval_ms > 0) {
                ImGui::Text("%s BTC (polled %llds, age %s)",
                            grouped(num(m.open_interest_last->native_quantity), 1).c_str(),
                            static_cast<long long>(m.oi_sample_interval_ms / 1000), age.c_str());
            } else {
                ImGui::Text("%s BTC (stream)",
                            grouped(num(m.open_interest_last->native_quantity), 1).c_str());
            }
        });
        row("Spread", [&](domain::Venue v, const processors::MetricsView &m) {
            if (!m.bbo || !m.mid) {
                ImGui::TextUnformatted("-");
                return;
            }
            const auto spread  = domain::sub(m.bbo->ask_price, m.bbo->bid_price).value;
            const auto &def    = engine.view(v).definition;
            const double ticks = def ? num(spread) / num(def->tick_size) : 0;
            ImGui::Text("%s (%.0f ticks, %.2f bps)", text(spread).c_str(), ticks,
                        num(spread) / num(*m.mid) * 1e4);
        });
        ImGui::EndTable();
        ImGui::Separator();
        const auto basis = engine.basis();
        if (basis) {
            ImGui::Text("Basis (Binance mid - Hyperliquid mid): %s USD, %.2f bps",
                        text(basis->basis).c_str(), basis->basis_bps);
        } else {
            empty_state("Basis: waiting for both venues' BBO");
        }
    }
};

} // namespace

std::unique_ptr<Panel> make_overview() {
    return std::make_unique<OverviewPanel>();
}

} // namespace market_classifier::ui::panels
