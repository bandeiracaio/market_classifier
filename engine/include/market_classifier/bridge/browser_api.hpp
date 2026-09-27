#pragma once

#include <cstddef>
#include <cstdint>

namespace market_classifier::runtime {
class Engine;
}

namespace market_classifier::bridge {
// The single app-wide engine behind the C exports; the render loop drains it.
[[nodiscard]] runtime::Engine &app_engine();
} // namespace market_classifier::bridge

extern "C" {

[[nodiscard]] int mc_bridge_decode(const std::uint8_t *bytes, std::size_t size) noexcept;
[[nodiscard]] const char *mc_bridge_result_name(int code) noexcept;

// MVP runtime exports (plan Task 6). `venue`: 0 Binance USD-M, 1 Hyperliquid.
// Returns runtime::RawSubmit as int, or 255 on internal error.
[[nodiscard]] int mc_submit_raw(const std::uint8_t *bytes, std::size_t size) noexcept;
// kind: 0 open, 1 close, 2 error.
void mc_socket_event(int venue, int kind) noexcept;
[[nodiscard]] int mc_should_reconnect(int venue) noexcept;
[[nodiscard]] int mc_request_snapshot(int venue) noexcept;
void mc_metadata_failed(int venue) noexcept;
void mc_retry_venue(int venue) noexcept;
// Drains queued frames within `budget_ms`; called once per animation frame.
void mc_engine_frame(int budget_ms) noexcept;
// Diagnostics: 0 received, 1 adapted, 2 dropped, 3 events, 4 reconnects, 5 queued frames,
// 6 feed phase (runtime::FeedPhase), 7 rejected batches (venue ignored), 8 last event age ms.
[[nodiscard]] double mc_venue_stat(int venue, int which) noexcept;
}
