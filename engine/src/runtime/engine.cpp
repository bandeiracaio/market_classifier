#include "market_classifier/runtime/engine.hpp"

#include "market_classifier/venues/instruments.hpp"

#include <type_traits>
#include <variant>

namespace market_classifier::runtime {

namespace {
// Heatmap price quantum: Binance BTCUSDT tick 0.1; Hyperliquid prices are integers at
// BTC magnitudes (5 significant figures). docs/calculations/heatmap.md.
const domain::Decimal k_binance_quantum     = domain::Decimal::from_parts(1, 1).value;
const domain::Decimal k_hyperliquid_quantum = domain::Decimal::from_parts(1, 0).value;
const domain::Decimal k_footprint_base      = domain::Decimal::from_parts(1, 0).value;
} // namespace

VenueProcessors::VenueProcessors(domain::Decimal heatmap_quantum)
    : footprint(k_footprint_base), heatmap(heatmap_quantum) {}

Engine::Engine(const Clock &clock)
    // Distinct fixed seeds keep per-venue jitter deterministic and uncorrelated.
    : clock_(clock), venues_{Venue{0x42u}, Venue{0x4Cu}},
      processors_{VenueProcessors{k_binance_quantum}, VenueProcessors{k_hyperliquid_quantum}} {}

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
    venues_[venue_index(v)].gap = true;
    auto &p                     = processors_[venue_index(v)];
    const auto t                = wall_ms();
    p.cvd.mark_gap(t);
    p.footprint.mark_gap(t);
    p.candles.mark_gap(t);
    p.heatmap.mark_gap(t);
}

void Engine::sample_book(domain::Venue v, std::int64_t t_ms) {
    if (v == domain::Venue::BinanceUsdM) {
        if (binance_book_.state() == books::SyncState::Live) {
            processors_[0].heatmap.on_book(binance_book_.book(), t_ms);
        }
    } else {
        processors_[1].heatmap.on_book(hyperliquid_book_, t_ms);
    }
}

void Engine::process(domain::Venue v, const domain::NormalizedEvent &event) {
    auto &p = processors_[venue_index(v)];
    std::visit(
        [&](const auto &e) {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, domain::Trade>) {
                p.tape.on_trade(e);
                p.cvd.on_trade(e);
                p.footprint.on_trade(e);
                p.heatmap.on_trade(e);
            } else if constexpr (std::is_same_v<E, domain::Liquidation>) {
                p.liquidations.on_liquidation(e);
            } else if constexpr (std::is_same_v<E, domain::Candle>) {
                p.candles.on_candle(e);
            } else if constexpr (std::is_same_v<E, domain::AssetMetrics>) {
                p.metrics.on_asset_metrics(e);
            } else if constexpr (std::is_same_v<E, domain::OpenInterest>) {
                p.metrics.on_open_interest(e);
            } else if constexpr (std::is_same_v<E, domain::MarketSummary>) {
                p.metrics.on_summary(e);
            } else if constexpr (std::is_same_v<E, domain::Bbo>) {
                p.metrics.on_bbo(e);
            } else if constexpr (std::is_same_v<E, domain::InstrumentDefinition>) {
                p.definition = e;
            } else if constexpr (std::is_same_v<E, domain::BookSnapshot> ||
                                 std::is_same_v<E, domain::BookDelta>) {
                sample_book(v, e.meta.receive_time().value);
            }
        },
        event);
}

domain::DataQuality Engine::feed_quality(domain::Venue v) const {
    return venues_[venue_index(v)].feed.quality();
}

domain::DataQuality Engine::book_quality(domain::Venue v) const {
    const auto feed_q = feed_quality(v);
    if (feed_q != domain::DataQuality::Live) {
        return feed_q;
    }
    if (v == domain::Venue::BinanceUsdM) {
        switch (binance_book_.state()) {
        case books::SyncState::Live:
            return domain::DataQuality::Live;
        case books::SyncState::GapDetected:
            return domain::DataQuality::GapDetected;
        case books::SyncState::AwaitingSnapshot:
            return domain::DataQuality::Partial;
        }
    }
    return hyperliquid_book_.bids().empty() && hyperliquid_book_.asks().empty()
               ? domain::DataQuality::Partial
               : domain::DataQuality::Live;
}

domain::DataQuality Engine::liquidation_quality(domain::Venue v) const {
    return venues::supports_liquidations(v) ? feed_quality(v) : domain::DataQuality::Unsupported;
}

const books::OrderBook &Engine::book(domain::Venue v) const {
    return v == domain::Venue::BinanceUsdM ? binance_book_.book() : hyperliquid_book_;
}

std::optional<processors::BasisView> Engine::basis() const {
    return processors::cross_venue_basis(processors_[0].metrics, processors_[1].metrics);
}

void Engine::set_cvd_daily_reset(domain::Venue v, bool enabled) {
    processors_[venue_index(v)].cvd.daily_reset_enabled = enabled;
}

void Engine::reset_cvd(domain::Venue v) {
    processors_[venue_index(v)].cvd.reset();
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

void Engine::on_metadata_failed(domain::Venue v) {
    venues_[venue_index(v)].feed.fail();
}

void Engine::retry(domain::Venue v) {
    venues_[venue_index(v)].feed.retry(now_ms());
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
        process(v, event);
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
