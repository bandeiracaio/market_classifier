#include "market_classifier/runtime/feed_state.hpp"

#include <algorithm>

namespace market_classifier::runtime {

FeedState::FeedState(FeedConfig cfg, std::uint64_t jitter_seed)
    : cfg_(cfg), rng_(static_cast<std::minstd_rand::result_type>(jitter_seed % 2147483646U + 1U)) {}

void FeedState::on_open(std::int64_t now_ms) {
    if (phase_ == FeedPhase::Failed) {
        return;
    }
    phase_           = FeedPhase::Live;
    last_message_ms_ = now_ms;
}

void FeedState::on_message(std::int64_t now_ms) {
    if (phase_ == FeedPhase::Failed || phase_ == FeedPhase::Reconnecting) {
        return; // late frames from a closed socket do not revive the feed
    }
    phase_           = FeedPhase::Live;
    last_message_ms_ = now_ms;
    attempt_         = 0;
}

void FeedState::on_close(std::int64_t now_ms) {
    if (phase_ == FeedPhase::Failed) {
        return;
    }
    ++attempt_;
    if (attempt_ > cfg_.max_attempts_before_failed) {
        phase_ = FeedPhase::Failed;
        return;
    }
    // delay = min(cap, base * 2^attempt) * (0.5 + 0.5 * u), u in [0, 1)
    std::int64_t raw = cfg_.backoff_base_ms;
    for (std::uint32_t i = 0; i < attempt_ && raw < cfg_.backoff_cap_ms; ++i) {
        raw *= 2;
    }
    raw              = std::min(raw, cfg_.backoff_cap_ms);
    const auto span  = static_cast<double>(std::minstd_rand::max() - std::minstd_rand::min());
    const double u   = static_cast<double>(rng_() - std::minstd_rand::min()) / (span + 1.0);
    const auto delay = static_cast<std::int64_t>(static_cast<double>(raw) * (0.5 + 0.5 * u));
    next_retry_ms_   = now_ms + std::max<std::int64_t>(delay, 1);
    phase_           = FeedPhase::Reconnecting;
}

void FeedState::on_reconnect_started(std::int64_t /*now_ms*/) {
    if (phase_ == FeedPhase::Reconnecting) {
        phase_ = FeedPhase::Connecting;
    }
}

void FeedState::tick(std::int64_t now_ms) {
    if (phase_ == FeedPhase::Live && now_ms - last_message_ms_ > cfg_.stale_after_ms) {
        phase_ = FeedPhase::Stale;
    }
}

void FeedState::retry(std::int64_t now_ms) {
    if (phase_ != FeedPhase::Failed) {
        return;
    }
    attempt_       = 0;
    next_retry_ms_ = now_ms;
    phase_         = FeedPhase::Connecting;
}

bool FeedState::should_reconnect(std::int64_t now_ms) const noexcept {
    return phase_ == FeedPhase::Reconnecting && now_ms >= next_retry_ms_;
}

std::int64_t FeedState::age_ms(std::int64_t now_ms) const noexcept {
    return now_ms - last_message_ms_;
}

domain::DataQuality FeedState::quality() const noexcept {
    switch (phase_) {
    case FeedPhase::Live:
        return domain::DataQuality::Live;
    case FeedPhase::Stale:
        return domain::DataQuality::Stale;
    case FeedPhase::Failed:
        return domain::DataQuality::Failed;
    case FeedPhase::Connecting:
    case FeedPhase::Reconnecting:
        break;
    }
    return domain::DataQuality::Reconnecting;
}

} // namespace market_classifier::runtime
