#include "../ui_common.hpp"
#include "panels.hpp"

namespace market_classifier::ui::panels {
namespace {

using namespace detail;

constexpr std::array<const char *, 5> k_phase_names{"Connecting", "Live", "Stale", "Reconnecting",
                                                    "Failed"};
constexpr std::array<const char *, 6> k_error_names{
    "none", "malformed", "wrong symbol", "out of bounds", "unknown stream", "ignored"};

// Per-venue runtime health (packet §9). Rates are computed from counter deltas over ~1 s.
class DiagnosticsPanel final : public Panel {
  public:
    PanelKind kind() const override { return PanelKind::Diagnostics; }

    void draw(const runtime::Engine &engine, PanelSettings &) override {
        const auto now = engine.now_ms();
        const auto &io = ImGui::GetIO();
        ImGui::Text("Frame %.2f ms (%.0f fps)  rejected batches %llu",
                    1000.0 / std::max(io.Framerate, 1.0f), io.Framerate,
                    static_cast<unsigned long long>(engine.rejected_batches()));
        if (!ImGui::BeginTable("diag", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
            return;
        }
        ImGui::TableSetupColumn("Metric");
        ImGui::TableSetupColumn("Binance");
        ImGui::TableSetupColumn("Hyperliquid");
        ImGui::TableHeadersRow();
        std::array<double, 2> rate{};
        for (std::size_t i = 0; i < 2; ++i) {
            const auto v        = i == 0 ? domain::Venue::BinanceUsdM : domain::Venue::Hyperliquid;
            const auto received = engine.counters(v).frames_received;
            if (now - sample_at_[i] >= 1000) {
                rate_[i]         = sample_at_[i] == 0
                                       ? 0
                                       : static_cast<double>(received - sample_count_[i]) * 1000.0 /
                                     static_cast<double>(now - sample_at_[i]);
                sample_at_[i]    = now;
                sample_count_[i] = received;
            }
            rate[i] = rate_[i];
        }
        const auto row = [&](const char *label, auto &&cell) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            for (int i = 0; i < 2; ++i) {
                ImGui::TableSetColumnIndex(i + 1);
                ImGui::PushID(i);
                cell(i == 0 ? domain::Venue::BinanceUsdM : domain::Venue::Hyperliquid,
                     static_cast<std::size_t>(i));
                ImGui::PopID();
            }
        };
        row("State", [&](domain::Venue v, std::size_t) {
            const auto &feed = engine.feed(v);
            ImGui::Text("%s", k_phase_names[static_cast<std::size_t>(feed.phase())]);
            ImGui::SameLine();
            draw_quality_badge(engine.feed_quality(v), feed.age_ms(now));
            if (feed.phase() == runtime::FeedPhase::Failed) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Retry")) {
                    ui_actions().retry_venue = v;
                }
            }
        });
        row("Reconnect attempt",
            [&](domain::Venue v, std::size_t) { ImGui::Text("%u", engine.feed(v).attempt()); });
        row("Last event age", [&](domain::Venue v, std::size_t) {
            ImGui::TextUnformatted(format_duration(engine.feed(v).age_ms(now)).c_str());
        });
        row("Messages / s", [&](domain::Venue, std::size_t i) { ImGui::Text("%.1f", rate[i]); });
        row("Frames received", [&](domain::Venue v, std::size_t) {
            ImGui::Text("%llu",
                        static_cast<unsigned long long>(engine.counters(v).frames_received));
        });
        row("Events", [&](domain::Venue v, std::size_t) {
            ImGui::Text("%llu", static_cast<unsigned long long>(engine.counters(v).events));
        });
        row("Queue depth", [&](domain::Venue v, std::size_t) {
            ImGui::Text("%zu frames / %zu KiB", engine.queued_frames(v),
                        engine.queued_bytes(v) / 1024);
        });
        row("Dropped frames", [&](domain::Venue v, std::size_t) {
            ImGui::Text("%llu", static_cast<unsigned long long>(engine.counters(v).frames_dropped));
        });
        row("Reconnects", [&](domain::Venue v, std::size_t) {
            ImGui::Text("%llu", static_cast<unsigned long long>(engine.counters(v).reconnects));
        });
        row("Rejected frames", [&](domain::Venue v, std::size_t) {
            const auto &errors = engine.counters(v).adapter_errors;
            bool any           = false;
            for (std::size_t k = 1; k < errors.size(); ++k) {
                if (errors[k] > 0) {
                    ImGui::Text("%s: %llu", k_error_names[k],
                                static_cast<unsigned long long>(errors[k]));
                    any = true;
                }
            }
            if (!any) {
                ImGui::TextUnformatted("0");
            }
        });
        row("Book", [&](domain::Venue v, std::size_t) {
            draw_quality_badge(engine.book_quality(v), 0);
            if (v == domain::Venue::BinanceUsdM) {
                ImGui::SameLine();
                ImGui::Text("resyncs %llu",
                            static_cast<unsigned long long>(engine.binance_book().resync_count()));
            }
        });
        ImGui::EndTable();
    }

  private:
    std::array<std::int64_t, 2> sample_at_{};
    std::array<std::uint64_t, 2> sample_count_{};
    std::array<double, 2> rate_{};
};

} // namespace

std::unique_ptr<Panel> make_diagnostics() {
    return std::make_unique<DiagnosticsPanel>();
}

} // namespace market_classifier::ui::panels
