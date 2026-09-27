#pragma once

#include "market_classifier/bridge/raw_frame.hpp"
#include "market_classifier/venues/adapter_result.hpp"

#include <cstdint>

namespace market_classifier::venues {

// Parses Hyperliquid BTC frames (docs/protocols/hyperliquid.md) into normalized events.
class HyperliquidAdapter {
  public:
    [[nodiscard]] AdapterResult adapt(const bridge::RawFrame &frame);

  private:
    std::uint64_t next_sequence_ = 1;
};

} // namespace market_classifier::venues
