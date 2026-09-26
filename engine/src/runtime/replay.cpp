#include "market_classifier/runtime/replay.hpp"

#include <utility>
#include <vector>

namespace market_classifier::runtime {

ReplayResult ReplayHarness::run(std::span<const ReplayEntry> entries) {
    if (entries.empty())
        return {0, ReplayError::Empty};
    if (entries.size() > k_max_replay_entries)
        return {0, ReplayError::TooManyEntries};

    std::int64_t previous_offset = 0;
    for (const auto &entry : entries) {
        if (entry.offset_ms < previous_offset)
            return {0, ReplayError::InvalidTimeline};
        previous_offset = entry.offset_ms;
    }

    previous_offset       = 0;
    std::size_t submitted = 0;
    for (const auto &entry : entries) {
        if (!clock_.advance_ms(entry.offset_ms - previous_offset)) {
            return {submitted, ReplayError::ClockOverflow};
        }
        previous_offset = entry.offset_ms;

        std::vector<domain::NormalizedEvent> events;
        events.push_back(entry.event);
        const auto result = ingress_.submit({std::move(events), entry.encoded_bytes});
        if (result != SubmitResult::Accepted && result != SubmitResult::AcceptedWithDrop) {
            return {submitted, ReplayError::IngressRejected};
        }
        ++submitted;
        drain_ingress(ingress_, model_);
    }
    return {submitted, ReplayError::None};
}

} // namespace market_classifier::runtime
