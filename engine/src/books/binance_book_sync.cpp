#include "market_classifier/books/binance_book_sync.hpp"

namespace market_classifier::books {

SyncAction BinanceBookSync::buffer(const domain::BookDelta &delta, std::uint64_t prev) {
    if (buffer_.size() >= k_max_buffered_diffs) {
        buffer_.pop_front(); // bounded: drop oldest, stay awaiting
        ++overflows_;
    }
    buffer_.push_back({delta, prev});
    SyncAction action;
    if (!snapshot_requested_) {
        snapshot_requested_     = true;
        action.request_snapshot = true;
    }
    return action;
}

SyncAction BinanceBookSync::gap() {
    state_ = SyncState::GapDetected;
    book_.clear();
    buffer_.clear();
    snapshot_id_.reset();
    ++resyncs_;
    snapshot_requested_ = true;
    return {true};
}

bool BinanceBookSync::apply(const domain::BookDelta &delta) {
    if (!book_.apply_levels(delta.changed_bids, delta.changed_asks)) {
        ++bound_hits_;
    }
    last_final_id_ = delta.last_source_sequence.value_or(0);
    return !book_.crossed();
}

bool BinanceBookSync::drain_buffer() {
    while (!buffer_.empty()) {
        const auto pending = buffer_.front();
        buffer_.pop_front();
        const auto first = pending.delta.first_source_sequence.value_or(0);
        const auto last  = pending.delta.last_source_sequence.value_or(0);
        if (snapshot_id_) {
            const auto l = *snapshot_id_;
            if (last < l) {
                ++stale_; // entirely older than the snapshot
                continue;
            }
            if (first > l) {
                return true; // updates between snapshot and this diff are missing
            }
            // First diff straddles lastUpdateId: book becomes live.
            snapshot_id_.reset();
            state_ = SyncState::Live;
            if (!apply(pending.delta)) {
                return true;
            }
            continue;
        }
        if (pending.prev != last_final_id_) {
            return true;
        }
        if (!apply(pending.delta)) {
            return true;
        }
    }
    return false;
}

SyncAction BinanceBookSync::on_delta(const domain::BookDelta &delta,
                                     std::uint64_t prev_final_update_id) {
    if (state_ == SyncState::Live && !snapshot_id_) {
        if (prev_final_update_id != last_final_id_) {
            return gap();
        }
        if (!apply(delta)) {
            return gap();
        }
        return {};
    }
    auto action = buffer(delta, prev_final_update_id);
    if (snapshot_id_ && drain_buffer()) {
        return gap();
    }
    return action;
}

SyncAction BinanceBookSync::on_snapshot(const domain::BookSnapshot &snapshot) {
    if (!snapshot.source_sequence) {
        return gap();
    }
    snapshot_requested_ = false;
    book_.apply_snapshot(snapshot);
    snapshot_id_   = *snapshot.source_sequence;
    last_final_id_ = *snapshot.source_sequence;
    state_         = SyncState::AwaitingSnapshot;
    if (drain_buffer()) {
        return gap();
    }
    // Snapshot held until a straddling diff arrives; do not request another yet.
    snapshot_requested_ = true;
    return {};
}

} // namespace market_classifier::books
