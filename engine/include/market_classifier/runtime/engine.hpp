#pragma once

#include "market_classifier/books/binance_book_sync.hpp"
#include "market_classifier/books/order_book.hpp"
#include "market_classifier/bridge/raw_frame.hpp"
#include "market_classifier/processors/candles.hpp"
#include "market_classifier/processors/cvd.hpp"
#include "market_classifier/processors/footprint.hpp"
#include "market_classifier/processors/heatmap.hpp"
#include "market_classifier/processors/metrics.hpp"
#include "market_classifier/processors/trades.hpp"
#include "market_classifier/runtime/clock.hpp"
#include "market_classifier/runtime/feed_state.hpp"
#include "market_classifier/venues/binance_adapter.hpp"
#include "market_classifier/venues/hyperliquid_adapter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>

namespace market_classifier::runtime {

// Per-venue raw frame queue (docs/runtime/mvp-feeds.md). Frames are adapted during
// frame(), not in the network callback.
inline constexpr std::size_t k_max_queued_raw_frames = 512;
inline constexpr std::size_t k_max_queued_raw_bytes  = 4 * 1024 * 1024;
inline constexpr std::size_t k_max_frames_per_drain  = 256;
inline constexpr std::size_t k_venue_count           = 2;

enum class SocketEvent : std::uint8_t { Open = 0, Close = 1, Error = 2 };

enum class RawSubmit : std::uint8_t { Accepted, AcceptedWithDrop, RejectedDecode };

inline constexpr std::size_t k_adapter_error_kinds = 6; // venues::AdapterError values

struct VenueCounters {
    std::uint64_t frames_received = 0;
    std::uint64_t frames_adapted  = 0; // produced at least one event
    std::uint64_t frames_dropped  = 0; // evicted by queue bounds
    std::uint64_t events          = 0;
    std::uint64_t reconnects      = 0;
    std::array<std::uint64_t, k_adapter_error_kinds> adapter_errors{};
};

// Read models per venue. Panels read these through `const Engine &` only.
struct VenueProcessors {
    explicit VenueProcessors(domain::Decimal heatmap_quantum);
    processors::TradeTape tape;
    processors::LiquidationLog liquidations;
    processors::Cvd cvd;
    processors::Footprint footprint; // $1 base bucket, 1m bars; aggregated on read
    processors::CandleSeries candles;
    processors::Heatmap heatmap;
    processors::Metrics metrics;
    std::optional<domain::InstrumentDefinition> definition;
};

[[nodiscard]] constexpr std::size_t venue_index(domain::Venue v) noexcept {
    return v == domain::Venue::BinanceUsdM ? 0 : 1;
}

// Single owner of all runtime state; one per app. Not thread-safe (WASM is
// single-threaded; ADR/packet forbid pthreads).
class Engine {
  public:
    explicit Engine(const Clock &clock);
    ~Engine()                         = default;
    Engine(const Engine &)            = delete;
    Engine &operator=(const Engine &) = delete;

    // Network side: decode + enqueue only.
    RawSubmit submit_raw(std::span<const std::uint8_t> bytes);
    void on_socket_event(domain::Venue v, SocketEvent kind);
    // Bridge asks once per animation frame; true starts one reconnect attempt.
    [[nodiscard]] bool should_reconnect(domain::Venue v);
    // Latched request for a Binance REST depth snapshot; cleared when read.
    [[nodiscard]] bool take_snapshot_request(domain::Venue v);

    // Render side: drain within budget (monotonic ms and k_max_frames_per_drain).
    void frame(std::int64_t budget_ms);

    [[nodiscard]] const FeedState &feed(domain::Venue v) const {
        return venues_[venue_index(v)].feed;
    }
    [[nodiscard]] const VenueCounters &counters(domain::Venue v) const {
        return venues_[venue_index(v)].counters;
    }
    [[nodiscard]] std::size_t queued_frames(domain::Venue v) const {
        return venues_[venue_index(v)].queue.size();
    }
    [[nodiscard]] std::size_t queued_bytes(domain::Venue v) const {
        return venues_[venue_index(v)].queued_bytes;
    }
    // Latched when frames were dropped or a reconnect happened; processors mark gaps.
    [[nodiscard]] bool data_gap(domain::Venue v) const { return venues_[venue_index(v)].gap; }
    [[nodiscard]] std::uint64_t rejected_batches() const noexcept { return rejected_batches_; }
    [[nodiscard]] const books::BinanceBookSync &binance_book() const noexcept {
        return binance_book_;
    }
    [[nodiscard]] const books::OrderBook &hyperliquid_book() const noexcept {
        return hyperliquid_book_;
    }
    [[nodiscard]] std::int64_t now_ms() const noexcept { return clock_.monotonic_time_ms(); }
    [[nodiscard]] std::int64_t wall_ms() const noexcept { return clock_.wall_time_ms(); }

    [[nodiscard]] const VenueProcessors &view(domain::Venue v) const {
        return processors_[venue_index(v)];
    }
    // Quality shown by panels (spec: stale/partial/gap/unsupported are distinct).
    [[nodiscard]] domain::DataQuality feed_quality(domain::Venue v) const;
    [[nodiscard]] domain::DataQuality book_quality(domain::Venue v) const;
    [[nodiscard]] domain::DataQuality liquidation_quality(domain::Venue v) const;
    [[nodiscard]] const books::OrderBook &book(domain::Venue v) const;
    [[nodiscard]] std::optional<processors::BasisView> basis() const;
    void set_cvd_daily_reset(domain::Venue v, bool enabled);
    void reset_cvd(domain::Venue v);

  private:
    struct Venue {
        explicit Venue(std::uint64_t seed) : feed(FeedConfig{}, seed) {}
        FeedState feed;
        std::deque<bridge::RawFrame> queue;
        std::size_t queued_bytes = 0;
        VenueCounters counters;
        bool gap                = false;
        bool snapshot_requested = false;
    };

    void dispatch(domain::Venue v, const venues::AdapterResult &result);
    void process(domain::Venue v, const domain::NormalizedEvent &event);
    void sample_book(domain::Venue v, std::int64_t t_ms);
    void mark_gap(domain::Venue v);

    const Clock &clock_;
    std::array<Venue, k_venue_count> venues_;
    venues::BinanceAdapter binance_adapter_;
    venues::HyperliquidAdapter hyperliquid_adapter_;
    books::BinanceBookSync binance_book_;
    books::OrderBook hyperliquid_book_;
    std::array<VenueProcessors, k_venue_count> processors_;
    std::uint64_t rejected_batches_ = 0;
};

[[nodiscard]] constexpr domain::Venue venue_of(venues::StreamTag tag) noexcept {
    return static_cast<std::uint8_t>(tag) < 32 ? domain::Venue::BinanceUsdM
                                               : domain::Venue::Hyperliquid;
}

} // namespace market_classifier::runtime
