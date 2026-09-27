#pragma once

#include "market_classifier/domain/events.hpp"

#include <cstdint>
#include <vector>

namespace market_classifier::venues {

enum class AdapterError : std::uint8_t {
    None,
    Malformed,
    WrongSymbol,
    OutOfBounds,
    UnknownStream,
    Ignored, // acks, pongs, empty-side BBO: valid but carries no event
};

struct AdapterResult {
    std::vector<domain::NormalizedEvent> events;
    // Binance futures depth continuity needs `pu` (previous final update id), which the
    // shared BookDelta does not carry. Aligned with `events`; 0 for non-BookDelta entries.
    std::vector<std::uint64_t> binance_prev_final_update_ids;
    AdapterError error = AdapterError::None;
};

} // namespace market_classifier::venues
