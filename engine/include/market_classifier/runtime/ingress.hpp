#pragma once

#include "market_classifier/domain/events.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace market_classifier::runtime {

inline constexpr std::size_t k_max_events_per_batch   = 256;
inline constexpr std::size_t k_max_batch_bytes        = 64 * 1024;
inline constexpr std::size_t k_max_queued_batches     = 8;
inline constexpr std::size_t k_max_queued_bytes       = 512 * 1024;
inline constexpr std::size_t k_max_levels_per_event   = 1024;
inline constexpr std::size_t k_max_dynamic_text_bytes = 128;

struct IngressBatch {
    std::vector<domain::NormalizedEvent> events;
    std::size_t encoded_bytes = 0;
};

enum class SubmitResult : std::uint8_t {
    Accepted,
    AcceptedWithDrop,
    RejectedEmpty,
    RejectedEventLimit,
    RejectedByteLimit,
    RejectedOwnedSize,
};

struct IngressCounters {
    std::uint64_t accepted_batches = 0;
    std::uint64_t accepted_events  = 0;
    std::uint64_t rejected_batches = 0;
    std::uint64_t rejected_events  = 0;
    std::uint64_t dropped_batches  = 0;
    std::uint64_t dropped_events   = 0;
};

struct IngressStatus {
    IngressCounters counters;
    std::size_t queued_batches  = 0;
    std::size_t queued_events   = 0;
    std::size_t queued_bytes    = 0;
    domain::DataQuality quality = domain::DataQuality::Live;
};

class BoundedIngress {
  public:
    [[nodiscard]] SubmitResult submit(IngressBatch batch);
    [[nodiscard]] std::optional<IngressBatch> consume_next();
    [[nodiscard]] bool transition_quality(domain::DataQuality next) noexcept;
    [[nodiscard]] IngressStatus status() const noexcept;

  private:
    std::deque<IngressBatch> queue_;
    IngressCounters counters_{};
    std::size_t queued_events_   = 0;
    std::size_t queued_bytes_    = 0;
    domain::DataQuality quality_ = domain::DataQuality::Live;
};

} // namespace market_classifier::runtime
