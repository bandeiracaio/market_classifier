#pragma once

#include "market_classifier/bridge/protocol.hpp"
#include "market_classifier/venues/stream.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace market_classifier::bridge {

// A 1000-level Binance depth snapshot is ~42 KB; trimmed exchangeInfo ~3 KB.
inline constexpr std::size_t k_max_raw_frame_bytes   = 512 * 1024;
inline constexpr std::size_t k_max_frames_per_batch  = 64;
inline constexpr std::size_t k_raw_frame_fixed_bytes = 13; // u8 tag, i64 time, u32 length
// The TS driver flushes at most 1 MiB of payload per batch; allow framing on top.
inline constexpr std::size_t k_max_raw_batch_bytes =
    1024 * 1024 + k_header_bytes + k_max_frames_per_batch * k_raw_frame_fixed_bytes;

struct RawFrame {
    venues::StreamTag tag;
    std::int64_t receive_time_ms;
    std::string payload;
};

struct RawFrameDecode {
    std::vector<RawFrame> frames;
    DecodeError error = DecodeError::None;

    [[nodiscard]] explicit operator bool() const noexcept { return error == DecodeError::None; }
};

// Wire: ADR-0004 header (kind = RawFrameBatch, count = frame count), then per
// frame `u8 tag, i64 receive_time_ms, u32 length, bytes`, little-endian.
[[nodiscard]] RawFrameDecode decode_raw_frames(std::span<const std::uint8_t> bytes);
// Returns an empty vector when any bound is violated.
[[nodiscard]] std::vector<std::uint8_t> encode_raw_frames(std::span<const RawFrame> frames);

} // namespace market_classifier::bridge
