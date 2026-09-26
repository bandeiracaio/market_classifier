#pragma once

#include "market_classifier/runtime/ingress.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace market_classifier::runtime {

inline constexpr std::size_t k_normalized_event_kind_count = 11;

struct DummyReadModel {
    std::array<std::uint64_t, k_normalized_event_kind_count> event_counts{};
    std::uint64_t total_events        = 0;
    std::uint64_t last_local_sequence = 0;
    std::int64_t last_source_time_ms  = 0;
    std::int64_t last_receive_time_ms = 0;
    domain::DataQuality quality       = domain::DataQuality::Partial;
    IngressCounters ingress_counters{};
    bool has_data = false;

    [[nodiscard]] bool operator==(const DummyReadModel &) const noexcept = default;
};

void drain_ingress(BoundedIngress &ingress, DummyReadModel &model);

} // namespace market_classifier::runtime
