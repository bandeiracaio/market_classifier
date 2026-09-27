#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace market_classifier::domain {

enum class Venue : std::uint8_t { BinanceUsdM, Hyperliquid };

[[nodiscard]] constexpr bool is_valid(Venue venue) noexcept {
    return venue == Venue::BinanceUsdM || venue == Venue::Hyperliquid;
}

enum class InstrumentIdError : std::uint8_t {
    None,
    InvalidVenue,
    EmptySymbol,
    SymbolTooLong,
    InvalidSymbol,
};

class InstrumentId;
struct InstrumentIdResult;

class InstrumentId {
  public:
    static constexpr std::size_t k_max_symbol_length = 64;

    InstrumentId() = default;

    [[nodiscard]] static InstrumentIdResult create(Venue venue, std::string_view native_symbol);

    [[nodiscard]] constexpr Venue venue() const noexcept { return venue_; }
    [[nodiscard]] const std::string &native_symbol() const noexcept { return native_symbol_; }

    [[nodiscard]] std::strong_ordering operator<=>(const InstrumentId &other) const noexcept;
    [[nodiscard]] bool operator==(const InstrumentId &other) const noexcept;

  private:
    InstrumentId(Venue venue, std::string native_symbol)
        : venue_(venue), native_symbol_(std::move(native_symbol)) {}

    Venue venue_ = Venue::BinanceUsdM;
    std::string native_symbol_;
};

struct InstrumentIdResult {
    InstrumentIdResult(InstrumentId result_value      = {},
                       InstrumentIdError result_error = InstrumentIdError::None)
        : value(std::move(result_value)), error(result_error) {}

    InstrumentId value;
    InstrumentIdError error = InstrumentIdError::None;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == InstrumentIdError::None;
    }
};

struct SourceTimeMs {
    std::int64_t value                           = 0;
    auto operator<=>(const SourceTimeMs &) const = default;
};

struct ReceiveTimeMs {
    std::int64_t value                            = 0;
    auto operator<=>(const ReceiveTimeMs &) const = default;
};

struct LocalSequence {
    std::uint64_t value                           = 0;
    auto operator<=>(const LocalSequence &) const = default;
};

enum class DataQuality : std::uint8_t {
    Live,
    Delayed,
    Stale,
    Reconnecting,
    GapDetected,
    Partial,
    Unsupported,
    Failed,
};

[[nodiscard]] constexpr bool is_valid(DataQuality quality) noexcept {
    return quality <= DataQuality::Failed;
}

// Unsupported is terminal. A detected gap must pass through Partial or Reconnecting before
// returning to Live, so downstream state cannot silently appear complete after data loss.
[[nodiscard]] bool can_transition(DataQuality from, DataQuality to) noexcept;

enum class EventMetaError : std::uint8_t {
    None,
    InvalidInstrument,
    InvalidSourceTime,
    InvalidReceiveTime,
    InvalidLocalSequence,
    InvalidQuality,
};

class EventMeta;
struct EventMetaResult;

class EventMeta {
  public:
    EventMeta() = default;

    [[nodiscard]] static EventMetaResult create(InstrumentId instrument, SourceTimeMs source_time,
                                                ReceiveTimeMs receive_time,
                                                LocalSequence local_sequence, DataQuality quality);

    [[nodiscard]] const InstrumentId &instrument() const noexcept { return instrument_; }
    [[nodiscard]] constexpr SourceTimeMs source_time() const noexcept { return source_time_; }
    [[nodiscard]] constexpr ReceiveTimeMs receive_time() const noexcept { return receive_time_; }
    [[nodiscard]] constexpr LocalSequence local_sequence() const noexcept {
        return local_sequence_;
    }
    [[nodiscard]] constexpr DataQuality quality() const noexcept { return quality_; }

    [[nodiscard]] bool operator==(const EventMeta &other) const noexcept = default;

  private:
    EventMeta(InstrumentId instrument, SourceTimeMs source_time, ReceiveTimeMs receive_time,
              LocalSequence local_sequence, DataQuality quality)
        : instrument_(std::move(instrument)), source_time_(source_time),
          receive_time_(receive_time), local_sequence_(local_sequence), quality_(quality) {}

    InstrumentId instrument_;
    SourceTimeMs source_time_{};
    ReceiveTimeMs receive_time_{};
    LocalSequence local_sequence_{};
    DataQuality quality_ = DataQuality::Failed;
};

struct EventMetaResult {
    EventMetaResult(EventMeta result_value = {}, EventMetaError result_error = EventMetaError::None)
        : value(std::move(result_value)), error(result_error) {}

    EventMeta value;
    EventMetaError error = EventMetaError::None;

    [[nodiscard]] explicit operator bool() const noexcept { return error == EventMetaError::None; }
};

} // namespace market_classifier::domain
