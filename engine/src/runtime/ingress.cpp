#include "market_classifier/runtime/ingress.hpp"

#include <type_traits>
#include <utility>

namespace market_classifier::runtime {
namespace {

bool add_size(std::size_t &total, std::size_t value) {
    if (value > k_max_batch_bytes - total) {
        return false;
    }
    total += value;
    return true;
}

bool measure_event(const domain::NormalizedEvent &event, std::size_t &total) {
    if (!add_size(total, sizeof(domain::NormalizedEvent))) {
        return false;
    }
    return std::visit(
        [&total](const auto &value) {
            using Event = std::decay_t<decltype(value)>;
            if (!add_size(total, value.meta.instrument().native_symbol().size())) {
                return false;
            }
            if constexpr (std::is_same_v<Event, domain::InstrumentDefinition>) {
                return value.base_asset.size() <= k_max_dynamic_text_bytes &&
                       value.quote_asset.size() <= k_max_dynamic_text_bytes &&
                       add_size(total, value.base_asset.size()) &&
                       add_size(total, value.quote_asset.size());
            } else if constexpr (std::is_same_v<Event, domain::Trade> ||
                                 std::is_same_v<Event, domain::Liquidation>) {
                return value.source_id.size() <= k_max_dynamic_text_bytes &&
                       add_size(total, value.source_id.size());
            } else if constexpr (std::is_same_v<Event, domain::BookSnapshot>) {
                if (value.bids.size() > k_max_levels_per_event ||
                    value.asks.size() > k_max_levels_per_event ||
                    value.source_cursor.size() > k_max_dynamic_text_bytes) {
                    return false;
                }
                return add_size(total, (value.bids.size() + value.asks.size()) *
                                           sizeof(domain::BookLevel)) &&
                       add_size(total, value.source_cursor.size());
            } else if constexpr (std::is_same_v<Event, domain::BookDelta>) {
                if (value.changed_bids.size() > k_max_levels_per_event ||
                    value.changed_asks.size() > k_max_levels_per_event) {
                    return false;
                }
                return add_size(total, (value.changed_bids.size() + value.changed_asks.size()) *
                                           sizeof(domain::BookLevel));
            } else {
                return true;
            }
        },
        event);
}

bool valid_owned_size(const IngressBatch &batch) {
    std::size_t total = sizeof(IngressBatch);
    for (const auto &event : batch.events) {
        if (!measure_event(event, total)) {
            return false;
        }
    }
    return true;
}

} // namespace

SubmitResult BoundedIngress::submit(IngressBatch batch) {
    const auto event_count = batch.events.size();
    if (event_count == 0 || batch.encoded_bytes == 0) {
        ++counters_.rejected_batches;
        counters_.rejected_events += event_count;
        return SubmitResult::RejectedEmpty;
    }
    if (event_count > k_max_events_per_batch) {
        ++counters_.rejected_batches;
        counters_.rejected_events += event_count;
        return SubmitResult::RejectedEventLimit;
    }
    if (batch.encoded_bytes > k_max_batch_bytes) {
        ++counters_.rejected_batches;
        counters_.rejected_events += event_count;
        return SubmitResult::RejectedByteLimit;
    }
    if (!valid_owned_size(batch)) {
        ++counters_.rejected_batches;
        counters_.rejected_events += event_count;
        return SubmitResult::RejectedOwnedSize;
    }

    // Shed caller-controlled excess capacities before retaining the batch.
    batch.events = std::vector<domain::NormalizedEvent>{batch.events.begin(), batch.events.end()};

    bool dropped = false;
    while (!queue_.empty() && (queue_.size() == k_max_queued_batches ||
                               queued_bytes_ + batch.encoded_bytes > k_max_queued_bytes)) {
        const auto &oldest = queue_.front();
        ++counters_.dropped_batches;
        counters_.dropped_events += oldest.events.size();
        queued_events_ -= oldest.events.size();
        queued_bytes_ -= oldest.encoded_bytes;
        queue_.pop_front();
        dropped = true;
    }

    queued_events_ += event_count;
    queued_bytes_ += batch.encoded_bytes;
    queue_.push_back(std::move(batch));
    ++counters_.accepted_batches;
    counters_.accepted_events += event_count;
    if (dropped) {
        if (domain::can_transition(quality_, domain::DataQuality::GapDetected)) {
            quality_ = domain::DataQuality::GapDetected;
        }
        return SubmitResult::AcceptedWithDrop;
    }
    return SubmitResult::Accepted;
}

std::optional<IngressBatch> BoundedIngress::consume_next() {
    if (queue_.empty()) {
        return std::nullopt;
    }
    IngressBatch batch = std::move(queue_.front());
    queue_.pop_front();
    queued_events_ -= batch.events.size();
    queued_bytes_ -= batch.encoded_bytes;
    return batch;
}

bool BoundedIngress::transition_quality(domain::DataQuality next) noexcept {
    if (!domain::can_transition(quality_, next)) {
        return false;
    }
    quality_ = next;
    return true;
}

IngressStatus BoundedIngress::status() const noexcept {
    return {counters_, queue_.size(), queued_events_, queued_bytes_, quality_};
}

} // namespace market_classifier::runtime
