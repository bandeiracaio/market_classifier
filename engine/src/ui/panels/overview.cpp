#include "market_classifier/processors/metrics.hpp"

#include "../ui_common.hpp"
#include "panels.hpp"

namespace market_classifier::ui::panels {
namespace {

using namespace detail;

using Cell = void (*)(const runtime::Engine &, domain::Venue, const processors::MetricsView &);

void status_cell(const runtime::Engine &e, domain::Venue v,
                 const processors::MetricsView & /*unused*/) {
    draw_quality_badge(e.feed_quality(v), feed_age(e, v));
}

void last_cell(const runtime::Engine &e, domain::Venue v, const processors::MetricsView &m) {
    // Binance publishes a last price in its 24h ticker; Hyperliquid's is the latest trade.
    if (v == domain::Venue::BinanceUsdM) {
        ImGui::TextUnformatted(m.summary ? text(m.summary->last_price).c_str() : "-");
        return;
    }
    const auto &tape = e.view(v).tape.trades();
    ImGui::TextUnformatted(tape.empty() ? "-" : text(tape.back().price).c_str());
}

void mark_cell(const runtime::Engine & /*unused*/, domain::Venue /*unused*/,
               const processors::MetricsView &m) {
    ImGui::TextUnformatted(m.asset ? text(m.asset->mark_price).c_str() : "-");
}

void index_cell(const runtime::Engine & /*unused*/, domain::Venue /*unused*/,
                const processors::MetricsView &m) {
    if (!m.asset) {
        ImGui::TextUnformatted("-");
    } else if (m.asset->index_price) {
        ImGui::Text("%s (index)", text(m.asset->index_price).c_str());
    } else {
        ImGui::Text("%s (oracle)", text(m.asset->oracle_price).c_str());
    }
}

void change_cell(const runtime::Engine & /*unused*/, domain::Venue /*unused*/,
                 const processors::MetricsView &m) {
    if (!m.summary || !m.summary->change_24h) {
        ImGui::TextUnformatted("-");
        return;
    }
    const bool up = num(*m.summary->change_24h) >= 0;
    ImGui::TextColored(up ? k_buy : k_sell, "%s%s", up ? "+" : "",
                       text(m.summary->change_24h).c_str());
}

void volume_cell(const runtime::Engine & /*unused*/, domain::Venue /*unused*/,
                 const processors::MetricsView &m) {
    ImGui::TextUnformatted(
        m.summary && m.summary->volume_24h ? grouped(num(*m.summary->volume_24h), 0).c_str() : "-");
}

void funding_cell(const runtime::Engine &e, domain::Venue v, const processors::MetricsView &m) {
    if (!m.asset) {
        ImGui::TextUnformatted("-");
        return;
    }
    const double annual = num(processors::annualized_funding(m.asset->funding_rate, v));
    const auto next     = m.asset->next_funding_time_ms
                              ? *m.asset->next_funding_time_ms
                              : processors::next_hourly_funding_ms(e.wall_ms());
    ImGui::Text("%.4f%% (%.2f%%/y) in %s", num(m.asset->funding_rate) * 100.0, annual * 100.0,
                format_duration(next - e.wall_ms()).c_str());
}

void oi_cell(const runtime::Engine &e, domain::Venue /*unused*/, const processors::MetricsView &m) {
    if (!m.open_interest_last) {
        ImGui::TextUnformatted("-");
        return;
    }
    const auto quantity = grouped(num(m.open_interest_last->native_quantity), 1);
    // Cadence is always visible: a polled series is never presented as streaming.
    if (m.oi_sample_interval_ms > 0) {
        ImGui::Text("%s BTC (polled %llds, age %s)", quantity.c_str(),
                    static_cast<long long>(m.oi_sample_interval_ms / 1000),
                    format_duration(e.wall_ms() - m.oi_update_ms).c_str());
    } else {
        ImGui::Text("%s BTC (stream)", quantity.c_str());
    }
}

void spread_cell(const runtime::Engine &e, domain::Venue v, const processors::MetricsView &m) {
    if (!m.bbo || !m.mid) {
        ImGui::TextUnformatted("-");
        return;
    }
    const auto spread  = domain::sub(m.bbo->ask_price, m.bbo->bid_price).value;
    const auto &def    = e.view(v).definition;
    const double ticks = def ? num(spread) / num(def->tick_size) : 0;
    ImGui::Text("%s (%.0f ticks, %.2f bps)", text(spread).c_str(), ticks,
                num(spread) / num(*m.mid) * 1e4);
}

struct Row {
    const char *label;
    Cell cell;
};

constexpr std::array<Row, 9> k_rows{{
    {"Status", status_cell},
    {"Last", last_cell},
    {"Mark", mark_cell},
    {"Index / Oracle", index_cell},
    {"24h change", change_cell},
    {"24h volume (USD)", volume_cell},
    {"Funding", funding_cell},
    {"Open interest", oi_cell},
    {"Spread", spread_cell},
}};

// Always both venues side by side, plus the cross-venue basis (packet §5 #1).
class OverviewPanel final : public Panel {
  public:
    [[nodiscard]] PanelKind kind() const override { return PanelKind::Overview; }

    void draw(const runtime::Engine &engine, PanelSettings &settings) override {
        settings.venue = VenueSelection::Both;
        constexpr std::array<domain::Venue, 2> k_venues{domain::Venue::BinanceUsdM,
                                                        domain::Venue::Hyperliquid};
        if (ImGui::BeginTable("overview", 3,
                              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Field");
            ImGui::TableSetupColumn("Binance BTCUSDT");
            ImGui::TableSetupColumn("Hyperliquid BTC");
            ImGui::TableHeadersRow();
            for (const auto &row : k_rows) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(row.label);
                int column = 1;
                for (const auto v : k_venues) {
                    ImGui::TableSetColumnIndex(column++);
                    row.cell(engine, v, engine.view(v).metrics.view());
                }
            }
            ImGui::EndTable();
        }
        ImGui::Separator();
        if (const auto basis = engine.basis()) {
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
