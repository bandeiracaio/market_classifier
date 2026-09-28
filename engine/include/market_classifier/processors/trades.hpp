#pragma once

#include "market_classifier/domain/events.hpp"
#include "market_classifier/processors/limits.hpp"
#include "market_classifier/runtime/ring.hpp"

#include <cstdint>

namespace market_classifier::processors {

class TradeTape {
  public:
    void on_trade(const domain::Trade &trade);
    void clear() noexcept { trades_.clear(); }

    [[nodiscard]] const runtime::Ring<domain::Trade, k_tape_capacity> &trades() const noexcept {
        return trades_;
    }
    [[nodiscard]] std::int64_t last_update_ms() const noexcept { return last_update_ms_; }

  private:
    runtime::Ring<domain::Trade, k_tape_capacity> trades_;
    std::int64_t last_update_ms_ = 0;
};

// Liquidations share the tape pattern (bounded, newest last).
class LiquidationLog {
  public:
    void on_liquidation(const domain::Liquidation &liquidation);
    [[nodiscard]] const runtime::Ring<domain::Liquidation, k_max_liquidations> &
    items() const noexcept {
        return items_;
    }
    [[nodiscard]] std::int64_t last_update_ms() const noexcept { return last_update_ms_; }

  private:
    runtime::Ring<domain::Liquidation, k_max_liquidations> items_;
    std::int64_t last_update_ms_ = 0;
};

} // namespace market_classifier::processors
