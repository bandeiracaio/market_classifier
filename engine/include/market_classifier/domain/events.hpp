#pragma once

#include "market_classifier/domain/decimal.hpp"
#include "market_classifier/domain/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace market_classifier::domain {

enum class ContractKind : std::uint8_t { LinearPerpetual, InversePerpetual };
enum class AggressorSide : std::uint8_t { Buy, Sell, Unknown };
enum class LiquidationSide : std::uint8_t { Long, Short };
enum class LiquidationMethod : std::uint8_t { VenueReported, ForceOrder };
enum class FeedLifecycle : std::uint8_t { Closed, Connecting, Open, Reconnecting, Failed };
enum class GapStatus : std::uint8_t { None, Detected, Rebuilding };
enum class FeedError : std::uint8_t {
    None,
    Network,
    Protocol,
    MalformedData,
    RateLimited,
    Capacity,
    Unsupported,
};

struct InstrumentDefinition {
    EventMeta meta;
    Decimal tick_size;
    Decimal quantity_step;
    ContractKind contract_kind;
    std::string base_asset;
    std::string quote_asset;
    bool active;
    std::optional<Decimal> contract_multiplier;
};

struct MarketSummary {
    EventMeta meta;
    std::optional<Decimal> last_price;
    std::optional<Decimal> mid_price;
    std::optional<Decimal> change_24h;
    std::optional<Decimal> volume_24h;
    std::optional<Decimal> open_interest;
    std::optional<Decimal> funding_rate;
};

struct Trade {
    EventMeta meta;
    std::string source_id;
    AggressorSide aggressor_side;
    Decimal price;
    Decimal quantity;
    Decimal usd_notional;
};

struct BookLevel {
    Decimal price;
    Decimal quantity;
    std::optional<std::uint32_t> order_count;
};

struct BookSnapshot {
    EventMeta meta;
    std::vector<BookLevel> bids;
    std::vector<BookLevel> asks;
    std::optional<std::uint64_t> source_sequence;
    std::string source_cursor;
};

struct BookDelta {
    EventMeta meta;
    std::vector<BookLevel> changed_bids;
    std::vector<BookLevel> changed_asks;
    std::optional<std::uint64_t> first_source_sequence;
    std::optional<std::uint64_t> last_source_sequence;
};

struct Bbo {
    EventMeta meta;
    Decimal bid_price;
    Decimal bid_quantity;
    Decimal ask_price;
    Decimal ask_quantity;
};

struct AssetMetrics {
    EventMeta meta;
    Decimal mark_price;
    std::optional<Decimal> index_price;
    std::optional<Decimal> oracle_price;
    Decimal funding_rate;
    std::optional<std::int64_t> next_funding_time_ms;
    std::optional<Decimal> basis;
};

struct OpenInterest {
    EventMeta meta;
    Decimal native_quantity;
    std::optional<Decimal> usd_notional;
    std::int64_t sample_interval_ms = 0;
};

struct Candle {
    EventMeta meta;
    std::int64_t interval_ms   = 0;
    std::int64_t open_time_ms  = 0;
    std::int64_t close_time_ms = 0;
    Decimal open;
    Decimal high;
    Decimal low;
    Decimal close;
    Decimal base_volume;
    std::optional<Decimal> quote_volume;
    std::optional<std::uint64_t> trade_count;
    bool closed = false;
};

struct Liquidation {
    EventMeta meta;
    LiquidationSide side;
    Decimal price;
    Decimal quantity;
    Decimal notional;
    std::string source_id;
    LiquidationMethod method;
};

struct FeedStatus {
    EventMeta meta;
    FeedLifecycle lifecycle         = FeedLifecycle::Closed;
    std::int64_t last_event_age_ms  = 0;
    std::uint32_t reconnect_attempt = 0;
    GapStatus gap_status            = GapStatus::None;
    FeedError error                 = FeedError::None;
};

using NormalizedEvent =
    std::variant<InstrumentDefinition, MarketSummary, Trade, BookSnapshot, BookDelta, Bbo,
                 AssetMetrics, OpenInterest, Candle, Liquidation, FeedStatus>;

} // namespace market_classifier::domain
