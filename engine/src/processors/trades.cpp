#include "market_classifier/processors/trades.hpp"

#include <algorithm>

namespace market_classifier::processors {

void TradeTape::on_trade(const domain::Trade &trade) {
    trades_.push(trade);
    last_update_ms_ = std::max(last_update_ms_, trade.meta.source_time().value);
}

void LiquidationLog::on_liquidation(const domain::Liquidation &liquidation) {
    items_.push(liquidation);
    last_update_ms_ = std::max(last_update_ms_, liquidation.meta.source_time().value);
}

} // namespace market_classifier::processors
