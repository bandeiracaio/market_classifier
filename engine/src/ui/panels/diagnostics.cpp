#include "../ui_common.hpp"
#include "panels.hpp"

namespace market_classifier::ui::panels {
namespace {

using namespace detail;

constexpr std::array<const char *, 5> k_phase_names{"Connecting", "Live", "Stale", "Reconnecting",
                                                    "Failed"};
constexpr std::array<const char *, 6> k_error_names{
    "none", "malformed", "wrong symbol", "out of bounds", "unknown stream", "ignored"};
constexpr std::array<domain::Venue, 2> k_venues{domain::Venue::BinanceUsdM,
                                                domain::Venue::Hyperliquid};
constexpr std::int64_t k_rate_window_ms = 1000;

// Message rate from counter deltas over >= 1 s windows.
struct RateMeter {
    std::int64_t sample_at     = 0;
    std::uint64_t sample_count = 0;
    double rate                = 0;

    double update(std::int64_t now, std::uint64_t count) {
        if (now - sample_at >= k_rate_window_ms) {
            rate         = sample_at == 0 ? 0
                                          : static_cast<double>(count - sample_count) * 1000.0 /
                                        static_cast<double>(now - sample_at);
            sample_at    = now;
            sample_count = count;
        }
        return rate;
    }
};

struct Context {
    const runtime::Engine *engine_ptr; // borrowed for one draw call
    [[nodiscard]] const runtime::Engine &engine() const { return *engine_ptr; }
    std::int64_t now;
    std::array<double, 2> rates;
};

using Cell = void (*)(const Context &, domain::Venue, std::size_t);

void u64(std::uint64_t value) {
    ImGui::Text("%llu", static_cast<unsigned long long>(value));
}

void state_cell(const Context &c, domain::Venue v, std::size_t /*unused*/) {
    const auto &feed = c.engine().feed(v);
    ImGui::Text("%s", k_phase_names.at(static_cast<std::size_t>(feed.phase())));
    ImGui::SameLine();
    draw_quality_badge(c.engine().feed_quality(v), feed.age_ms(c.now));
    if (feed.phase() == runtime::FeedPhase::Failed) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Retry")) {
            ui_actions().retry_venue = v;
        }
    }
}

void rejected_cell(const Context &c, domain::Venue v, std::size_t /*unused*/) {
    const auto &errors = c.engine().counters(v).adapter_errors;
    bool any           = false;
    for (std::size_t k = 1; k < errors.size(); ++k) {
        if (errors.at(k) > 0) {
            ImGui::Text("%s: %llu", k_error_names.at(k),
                        static_cast<unsigned long long>(errors.at(k)));
            any = true;
        }
    }
    if (!any) {
        ImGui::TextUnformatted("0");
    }
}

void book_cell(const Context &c, domain::Venue v, std::size_t /*unused*/) {
    draw_quality_badge(c.engine().book_quality(v), 0);
    if (v == domain::Venue::BinanceUsdM) {
        ImGui::SameLine();
        ImGui::Text("resyncs %llu",
                    static_cast<unsigned long long>(c.engine().binance_book().resync_count()));
    }
}

struct Row {
    const char *label;
    Cell cell;
};

const std::array<Row, 11> k_rows{{
    {"State", state_cell},
    {"Reconnect attempt", [](const Context &c, domain::Venue v,
                             std::size_t) { ImGui::Text("%u", c.engine().feed(v).attempt()); }},
    {"Last event age",
     [](const Context &c, domain::Venue v, std::size_t) {
         ImGui::TextUnformatted(format_duration(c.engine().feed(v).age_ms(c.now)).c_str());
     }},
    {"Messages / s",
     [](const Context &c, domain::Venue, std::size_t i) { ImGui::Text("%.1f", c.rates.at(i)); }},
    {"Frames received", [](const Context &c, domain::Venue v,
                           std::size_t) { u64(c.engine().counters(v).frames_received); }},
    {"Events",
     [](const Context &c, domain::Venue v, std::size_t) { u64(c.engine().counters(v).events); }},
    {"Queue depth",
     [](const Context &c, domain::Venue v, std::size_t) {
         ImGui::Text("%zu frames / %zu KiB", c.engine().queued_frames(v),
                     c.engine().queued_bytes(v) / 1024);
     }},
    {"Dropped frames", [](const Context &c, domain::Venue v,
                          std::size_t) { u64(c.engine().counters(v).frames_dropped); }},
    {"Reconnects", [](const Context &c, domain::Venue v,
                      std::size_t) { u64(c.engine().counters(v).reconnects); }},
    {"Rejected frames", rejected_cell},
    {"Book", book_cell},
}};

// Per-venue runtime health (packet §9): state, rates, queue depth, drops, reconnects, last
// event age, rejected frames, and frame time.
class DiagnosticsPanel final : public Panel {
  public:
    [[nodiscard]] PanelKind kind() const override { return PanelKind::Diagnostics; }

    void draw(const runtime::Engine &engine, PanelSettings & /*settings*/) override {
        const auto &io = ImGui::GetIO();
        ImGui::Text("Frame %.2f ms (%.0f fps)  rejected batches %llu",
                    1000.0 / std::max(io.Framerate, 1.0F), io.Framerate,
                    static_cast<unsigned long long>(engine.rejected_batches()));
        Context context{&engine, engine.now_ms(), {}};
        for (std::size_t i = 0; i < k_venues.size(); ++i) {
            context.rates.at(i) =
                meters_.at(i).update(context.now, engine.counters(k_venues.at(i)).frames_received);
        }
        if (!ImGui::BeginTable("diag", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
            return;
        }
        ImGui::TableSetupColumn("Metric");
        ImGui::TableSetupColumn("Binance");
        ImGui::TableSetupColumn("Hyperliquid");
        ImGui::TableHeadersRow();
        for (const auto &row : k_rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.label);
            for (std::size_t i = 0; i < k_venues.size(); ++i) {
                ImGui::TableSetColumnIndex(static_cast<int>(i) + 1);
                ImGui::PushID(static_cast<int>(i));
                row.cell(context, k_venues.at(i), i);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

  private:
    std::array<RateMeter, 2> meters_{};
};

} // namespace

std::unique_ptr<Panel> make_diagnostics() {
    return std::make_unique<DiagnosticsPanel>();
}

} // namespace market_classifier::ui::panels
