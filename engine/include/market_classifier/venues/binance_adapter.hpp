#pragma once

#include "market_classifier/bridge/raw_frame.hpp"
#include "market_classifier/venues/adapter_result.hpp"

#include <cstdint>

namespace market_classifier::venues {

// Parses Binance USD-M BTCUSDT frames (docs/protocols/binance.md) into normalized
// events. Every field is validated; any failure rejects the whole frame.
class BinanceAdapter {
  public:
    [[nodiscard]] AdapterResult adapt(const bridge::RawFrame &frame);

  private:
    std::uint64_t next_sequence_ = 1; // local_sequence, owned by the adapter
};

} // namespace market_classifier::venues
