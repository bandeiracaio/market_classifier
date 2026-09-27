#pragma once

#include "market_classifier/domain/events.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace market_classifier::bridge {

inline constexpr std::uint16_t k_protocol_version     = 1;
inline constexpr std::size_t k_header_bytes           = 16;
inline constexpr std::size_t k_max_message_bytes      = 64 * 1024;
inline constexpr std::uint16_t k_max_events_per_batch = 256;
inline constexpr std::size_t k_max_source_id_bytes    = 128;

enum class MessageKind : std::uint8_t { TradeBatch = 1, RawFrameBatch = 2 };

enum class DecodeError : std::uint8_t {
    None,
    Truncated,
    Oversized,
    InvalidMagic,
    UnsupportedVersion,
    UnknownKind,
    InvalidFlags,
    InvalidLength,
    InvalidCount,
    InvalidEnum,
    InvalidDecimal,
    InvalidInstrument,
    InvalidMetadata,
    InvalidSourceId,
};

struct DecodeResult {
    std::vector<domain::Trade> trades;
    DecodeError error = DecodeError::None;

    [[nodiscard]] explicit operator bool() const noexcept { return error == DecodeError::None; }
};

[[nodiscard]] DecodeResult decode(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::uint8_t> encode_trade_batch(std::span<const domain::Trade> trades);

} // namespace market_classifier::bridge
