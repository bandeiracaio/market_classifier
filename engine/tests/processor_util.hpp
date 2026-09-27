#pragma once

#include "market_classifier/domain/decimal_math.hpp"
#include "market_classifier/domain/events.hpp"

#include <cstdint>
#include <string>

namespace mc_test {

inline market_classifier::domain::Decimal dec(const char *text) {
    return market_classifier::domain::Decimal::parse(text).value;
}

inline market_classifier::domain::EventMeta
meta(std::int64_t source_ms,
     market_classifier::domain::Venue venue = market_classifier::domain::Venue::BinanceUsdM) {
    using namespace market_classifier::domain;
    static std::uint64_t seq = 1;
    const auto id =
        InstrumentId::create(venue, venue == Venue::BinanceUsdM ? "BTCUSDT" : "BTC").value;
    return EventMeta::create(id, SourceTimeMs{source_ms}, ReceiveTimeMs{source_ms},
                             LocalSequence{seq++}, DataQuality::Live)
        .value;
}

inline market_classifier::domain::Trade trade(std::int64_t t, const char *price, const char *qty,
                                              market_classifier::domain::AggressorSide side) {
    using namespace market_classifier::domain;
    Trade out{};
    out.meta           = meta(t);
    out.source_id      = std::to_string(t);
    out.aggressor_side = side;
    out.price          = dec(price);
    out.quantity       = dec(qty);
    out.usd_notional   = mul(out.price, out.quantity).value;
    return out;
}

inline market_classifier::domain::Candle candle(std::int64_t open_time, const char *close,
                                                std::int64_t interval = 60'000) {
    using namespace market_classifier::domain;
    Candle c{};
    c.meta          = meta(open_time);
    c.interval_ms   = interval;
    c.open_time_ms  = open_time;
    c.close_time_ms = open_time + interval - 1;
    c.open = c.high = c.low = c.close = dec(close);
    c.base_volume                     = dec("1");
    c.closed                          = true;
    return c;
}

} // namespace mc_test
