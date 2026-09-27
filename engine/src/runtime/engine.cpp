#include "market_classifier/runtime/engine.hpp"

#include <type_traits>
#include <variant>

namespace market_classifier::runtime {

Engine::Engine(const Clock &clock)
    // Distinct fixed seeds keep per-venue jitter deterministic and uncorrelated.
    : clock_(clock), venues_{Venue{0x42u}, Venue{0x4Cu}} {}

RawSubmit Engine::submit_raw(std::span<const std::uint8_t> bytes) {
    auto decoded = bridge::decode_raw_frames(bytes);
    if (!decoded) {
        ++rejected_batches_;
        return RawSubmit::RejectedDecode;
    }
    bool dropped   = false;
    const auto now = now_ms();
    for (auto &frame : decoded.frames) {
        auto &venue = venues_[venue_index(venue_of(frame.tag))];
        ++venue.counters.frames_received;
        venue.feed.on_message(now);
        // Bounded queue: evict oldest until the new frame fits (drop-oldest policy,
        // same as M1 ingress). Dropped frames latch a gap for the venue.
        while (!venue.queue.empty() &&
               (venue.queue.size() >= k_max_queued_raw_frames ||
                venue.queued_bytes + frame.payload.size() > k_max_queued_raw_bytes)) {
            venue.queued_bytes -= venue.queue.front().payload.size();
            venue.queue.pop_front();
            ++venue.counters.frames_dropped;
            dropped = true;
            mark_gap(venue_of(frame.tag));
        }
        venue.queued_bytes += frame.payload.size();
        venue.queue.push_back(std::move(frame));
    }
    return dropped ? RawSubmit::AcceptedWithDrop : RawSubmit::Accepted;
}

void Engine::mark_gap(domain::Venue v) {
    auto &venue = venues_[venue_index(v)];
    venue.gap   = true;
    on_gap(v, wall_ms());
}

void Engine::on_socket_event(domain::Venue v, SocketEvent kind) {
    auto &venue    = venues_[venue_index(v)];
    const auto now = now_ms();
    switch (kind) {
    case SocketEvent::Open:
        venue.feed.on_open(now);
        break;
    case SocketEvent::Close:
    case SocketEvent::Error:
        if (venue.feed.phase() == FeedPhase::Reconnecting) {
            break; // error followed by close reports one disconnect
        }
        venue.feed.on_close(now);
        ++venue.counters.reconnects;
        // Frames queued from the dead socket are still valid data; keep them, but the
        // stream restarts, so continuity is broken from here.
        if (v == domain::Venue::BinanceUsdM) {
            binance_book_.reset();
        } else {
            hyperliquid_book_.clear();
        }
        mark_gap(v);
        break;
    }
}

bool Engine::should_reconnect(domain::Venue v) {
    auto &venue    = venues_[venue_index(v)];
    const auto now = now_ms();
    if (!venue.feed.should_reconnect(now)) {
        return false;
    }
    venue.feed.on_reconnect_started(now);
    return true;
}

bool Engine::take_snapshot_request(domain::Venue v) {
    auto &venue              = venues_[venue_index(v)];
    const bool wanted        = venue.snapshot_requested;
    venue.snapshot_requested = false;
    return wanted;
}

void Engine::dispatch(domain::Venue v, const venues::AdapterResult &result) {
    auto &venue = venues_[venue_index(v)];
    for (std::size_t i = 0; i < result.events.size(); ++i) {
        const auto &event = result.events[i];
        std::visit(
            [&](const auto &e) {
                using E = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<E, domain::BookDelta>) {
                    const auto state_before = binance_book_.state();
                    const auto action =
                        binance_book_.on_delta(e, result.binance_prev_final_update_ids[i]);
                    venue.snapshot_requested = venue.snapshot_requested || action.request_snapshot;
                    if (state_before == books::SyncState::Live &&
                        binance_book_.state() == books::SyncState::GapDetected) {
                        mark_gap(v);
                    }
                } else if constexpr (std::is_same_v<E, domain::BookSnapshot>) {
                    if (v == domain::Venue::BinanceUsdM) {
                        const auto action = binance_book_.on_snapshot(e);
                        venue.snapshot_requested =
                            venue.snapshot_requested || action.request_snapshot;
                    } else {
                        hyperliquid_book_.apply_snapshot(e);
                    }
                }
            },
            event);
        ++venue.counters.events;
        on_event(v, event);
    }
}

void Engine::frame(std::int64_t budget_ms) {
    const auto start      = now_ms();
    std::size_t processed = 0;
    bool progressed       = true;
    // Round-robin between venues so one flooded venue cannot starve the other.
    while (progressed && processed < k_max_frames_per_drain && now_ms() - start <= budget_ms) {
        progressed = false;
        for (std::size_t vi = 0; vi < k_venue_count && processed < k_max_frames_per_drain; ++vi) {
            auto &venue = venues_[vi];
            if (venue.queue.empty()) {
                continue;
            }
            const auto frame = std::move(venue.queue.front());
            venue.queue.pop_front();
            venue.queued_bytes -= frame.payload.size();
            ++processed;
            progressed        = true;
            const auto v      = vi == 0 ? domain::Venue::BinanceUsdM : domain::Venue::Hyperliquid;
            const auto result = v == domain::Venue::BinanceUsdM ? binance_adapter_.adapt(frame)
                                                                : hyperliquid_adapter_.adapt(frame);
            if (result.error != venues::AdapterError::None) {
                ++venue.counters.adapter_errors[static_cast<std::size_t>(result.error)];
                continue;
            }
            ++venue.counters.frames_adapted;
            dispatch(v, result);
        }
    }
    const auto now = now_ms();
    for (auto &venue : venues_) {
        venue.feed.tick(now);
    }
}

} // namespace market_classifier::runtime
