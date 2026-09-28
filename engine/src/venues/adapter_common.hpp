#pragma once

// Shared parsing helpers for venue adapters. Private to engine/src/venues.

#include "market_classifier/bridge/raw_frame.hpp"
#include "market_classifier/domain/decimal_math.hpp"
#include "market_classifier/domain/events.hpp"
#include "market_classifier/json/json.hpp"
#include "market_classifier/runtime/ingress.hpp"
#include "market_classifier/venues/adapter_result.hpp"
#include "market_classifier/venues/instruments.hpp"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace market_classifier::venues::detail {

// Thrown-free error propagation: every helper returns optional and the caller maps a
// failure to the frame-level AdapterError it recorded in `Parse::error`.
struct Parse {
    AdapterError error = AdapterError::None;

    bool fail(AdapterError e) noexcept {
        if (error == AdapterError::None) {
            error = e;
        }
        return false;
    }
};

[[nodiscard]] inline bool positive(const domain::Decimal &d) noexcept {
    return d.mantissa() > 0;
}

[[nodiscard]] inline bool non_negative(const domain::Decimal &d) noexcept {
    return d.mantissa() >= 0;
}

// Field must exist and parse as Decimal; Malformed otherwise.
inline bool read_decimal(Parse &p, const json::Value &obj, std::string_view key,
                         domain::Decimal &out) noexcept {
    const auto v = obj.get(key);
    const auto d = v ? v->decimal() : std::nullopt;
    if (!d) {
        return p.fail(AdapterError::Malformed);
    }
    out = *d;
    return true;
}

inline bool read_price(Parse &p, const json::Value &obj, std::string_view key,
                       domain::Decimal &out) noexcept {
    return read_decimal(p, obj, key, out) && (positive(out) || p.fail(AdapterError::OutOfBounds));
}

inline bool read_quantity(Parse &p, const json::Value &obj, std::string_view key,
                          domain::Decimal &out) noexcept {
    return read_decimal(p, obj, key, out) &&
           (non_negative(out) || p.fail(AdapterError::OutOfBounds));
}

inline bool read_int(Parse &p, const json::Value &obj, std::string_view key,
                     std::int64_t &out) noexcept {
    const auto v = obj.get(key);
    const auto i = v ? v->int64() : std::nullopt;
    if (!i) {
        return p.fail(AdapterError::Malformed);
    }
    out = *i;
    return true;
}

inline bool read_time(Parse &p, const json::Value &obj, std::string_view key,
                      std::int64_t &out) noexcept {
    return read_int(p, obj, key, out) && (out >= 0 || p.fail(AdapterError::OutOfBounds));
}

inline bool read_string(Parse &p, const json::Value &obj, std::string_view key,
                        std::string_view &out) noexcept {
    const auto v = obj.get(key);
    const auto s = v ? v->string() : std::nullopt;
    if (!s) {
        return p.fail(AdapterError::Malformed);
    }
    out = *s;
    return true;
}

// Integer given as JSON number or as decimal-digit string (Binance ids).
inline std::optional<std::uint64_t> as_u64(const json::Value &v) noexcept {
    if (const auto i = v.int64(); i && *i >= 0) {
        return static_cast<std::uint64_t>(*i);
    }
    return std::nullopt;
}

inline bool read_u64(Parse &p, const json::Value &obj, std::string_view key,
                     std::uint64_t &out) noexcept {
    const auto v = obj.get(key);
    const auto u = v ? as_u64(*v) : std::nullopt;
    if (!u) {
        return p.fail(AdapterError::Malformed);
    }
    out = *u;
    return true;
}

// Builds EventMeta for the venue's fixed instrument with a fresh local sequence.
inline std::optional<domain::EventMeta> make_meta(Parse &p, domain::Venue venue,
                                                  std::int64_t source_time,
                                                  std::int64_t receive_time,
                                                  std::uint64_t &sequence) noexcept {
    const auto id = domain::InstrumentId::create(venue, instrument_for(venue).native_symbol);
    auto meta     = domain::EventMeta::create(
        id.value, domain::SourceTimeMs{source_time}, domain::ReceiveTimeMs{receive_time},
        domain::LocalSequence{sequence}, domain::DataQuality::Live);
    if (!id || !meta) {
        p.fail(AdapterError::OutOfBounds);
        return std::nullopt;
    }
    ++sequence;
    return meta.value;
}

inline std::string to_text(std::uint64_t value) {
    return std::to_string(value);
}

// Appends `event` with no Binance pu side-channel value.
template <typename Event> void push(AdapterResult &r, Event &&event) {
    r.events.emplace_back(std::forward<Event>(event));
    r.binance_prev_final_update_ids.push_back(0);
}

inline AdapterResult failed(AdapterError e) {
    AdapterResult r;
    r.error = e;
    return r;
}

inline std::optional<json::Document> parse_frame(const bridge::RawFrame &frame) noexcept {
    return json::Document::parse(frame.payload, bridge::k_max_raw_frame_bytes);
}

[[nodiscard]] inline bool level_count_ok(std::size_t n) noexcept {
    return n <= runtime::k_max_levels_per_event;
}

} // namespace market_classifier::venues::detail
