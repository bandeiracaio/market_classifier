#pragma once

#include "market_classifier/domain/types.hpp"

#include <cstdint>
#include <random>

namespace market_classifier::runtime {

enum class FeedPhase : std::uint8_t { Connecting, Live, Stale, Reconnecting, Failed };

// Thresholds per venue are documented in docs/runtime/mvp-feeds.md.
struct FeedConfig {
    std::int64_t stale_after_ms              = 5000;
    std::int64_t backoff_base_ms             = 500;
    std::int64_t backoff_cap_ms              = 30000;
    std::uint32_t max_attempts_before_failed = 20;
};

// Per-venue connection state machine. Times are monotonic milliseconds supplied by the
// caller (never read from a clock here). Backoff jitter comes from a seeded generator so
// schedules are deterministic in tests.
class FeedState {
  public:
    FeedState(FeedConfig cfg, std::uint64_t jitter_seed);

    void on_open(std::int64_t now_ms);
    void on_message(std::int64_t now_ms);
    void on_close(std::int64_t now_ms); // schedules a reconnect (or fails)
    void on_reconnect_started(std::int64_t now_ms);
    void tick(std::int64_t now_ms);  // Live -> Stale when silent
    void retry(std::int64_t now_ms); // user action from Failed: reconnect due now
    void fail() noexcept;            // unrecoverable without user retry (e.g. metadata)

    [[nodiscard]] bool should_reconnect(std::int64_t now_ms) const noexcept;
    [[nodiscard]] FeedPhase phase() const noexcept { return phase_; }
    [[nodiscard]] std::int64_t age_ms(std::int64_t now_ms) const noexcept;
    [[nodiscard]] std::uint32_t attempt() const noexcept { return attempt_; }
    [[nodiscard]] std::int64_t next_retry_ms() const noexcept { return next_retry_ms_; }
    [[nodiscard]] domain::DataQuality quality() const noexcept;

  private:
    FeedConfig cfg_;
    std::minstd_rand rng_;
    FeedPhase phase_              = FeedPhase::Connecting;
    std::int64_t last_message_ms_ = 0;
    std::int64_t next_retry_ms_   = 0;
    std::uint32_t attempt_        = 0;
};

} // namespace market_classifier::runtime
