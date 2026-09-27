#pragma once

#include "market_classifier/books/order_book.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace market_classifier::books {

enum class SyncState : std::uint8_t { AwaitingSnapshot, Live, GapDetected };

struct SyncAction {
    bool request_snapshot = false; // caller should fetch REST depth now
};

// Binance USD-M local order book maintenance (docs/calculations/book-sync.md):
// buffer diffs, apply a REST snapshot, discard diffs with u < lastUpdateId, require the
// first applied diff to straddle lastUpdateId, then require pu == previous u.
class BinanceBookSync {
  public:
    static constexpr std::size_t k_max_buffered_diffs = 2048;

    SyncAction on_delta(const domain::BookDelta &delta, std::uint64_t prev_final_update_id);
    SyncAction on_snapshot(const domain::BookSnapshot &snapshot);
    // Socket reconnect: the diff stream restarts, so discard the book and wait for a new
    // snapshot. Counters survive.
    void reset() noexcept;

    [[nodiscard]] SyncState state() const noexcept { return state_; }
    [[nodiscard]] const OrderBook &book() const noexcept { return book_; }
    [[nodiscard]] std::uint64_t resync_count() const noexcept { return resyncs_; }
    [[nodiscard]] std::uint64_t buffer_overflows() const noexcept { return overflows_; }
    [[nodiscard]] std::uint64_t discarded_stale() const noexcept { return stale_; }
    [[nodiscard]] std::size_t buffered() const noexcept { return buffer_.size(); }
    [[nodiscard]] std::uint64_t level_bound_hits() const noexcept { return bound_hits_; }

  private:
    struct Pending {
        domain::BookDelta delta;
        std::uint64_t prev;
    };

    SyncAction buffer(const domain::BookDelta &delta, std::uint64_t prev);
    // Consumes buffered diffs against the held snapshot. Returns true when a gap occurred.
    bool drain_buffer();
    bool apply(const domain::BookDelta &delta);
    SyncAction gap();

    SyncState state_ = SyncState::AwaitingSnapshot;
    OrderBook book_;
    std::deque<Pending> buffer_;
    std::optional<std::uint64_t> snapshot_id_; // held snapshot not yet bridged by a diff
    std::uint64_t last_final_id_ = 0;
    bool snapshot_requested_     = false;
    std::uint64_t resyncs_       = 0;
    std::uint64_t overflows_     = 0;
    std::uint64_t stale_         = 0;
    std::uint64_t bound_hits_    = 0;
};

} // namespace market_classifier::books
