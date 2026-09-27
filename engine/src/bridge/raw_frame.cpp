#include "market_classifier/bridge/raw_frame.hpp"

#include <algorithm>
#include <array>
#include <type_traits>

namespace market_classifier::bridge {
namespace {

constexpr std::array<std::uint8_t, 4> k_magic{'M', 'C', 'B', '1'};

template <typename T>
bool read_le(std::span<const std::uint8_t> bytes, std::size_t &offset, T &out) {
    using Unsigned = std::make_unsigned_t<T>;
    if (bytes.size() - offset < sizeof(T)) {
        return false;
    }
    Unsigned value = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        value |= static_cast<Unsigned>(bytes[offset + index]) << (index * 8U);
    }
    out = static_cast<T>(value);
    offset += sizeof(T);
    return true;
}

template <typename T> void write_le(std::vector<std::uint8_t> &bytes, T input) {
    using Unsigned   = std::make_unsigned_t<T>;
    const auto value = static_cast<Unsigned>(input);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8U)));
    }
}

} // namespace

RawFrameDecode decode_raw_frames(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < k_header_bytes) {
        return {{}, DecodeError::Truncated};
    }
    if (bytes.size() > k_max_raw_batch_bytes) {
        return {{}, DecodeError::Oversized};
    }
    if (!std::equal(k_magic.begin(), k_magic.end(), bytes.begin())) {
        return {{}, DecodeError::InvalidMagic};
    }
    std::size_t offset = 4;
    std::uint16_t version{};
    std::uint8_t kind{};
    std::uint8_t flags{};
    std::uint32_t payload_bytes{};
    std::uint16_t count{};
    std::uint16_t reserved{};
    if (!read_le(bytes, offset, version) || !read_le(bytes, offset, kind) ||
        !read_le(bytes, offset, flags) || !read_le(bytes, offset, payload_bytes) ||
        !read_le(bytes, offset, count) || !read_le(bytes, offset, reserved)) {
        return {{}, DecodeError::Truncated};
    }
    if (version != k_protocol_version) {
        return {{}, DecodeError::UnsupportedVersion};
    }
    if (kind != static_cast<std::uint8_t>(MessageKind::RawFrameBatch)) {
        return {{}, DecodeError::UnknownKind};
    }
    if (flags != 0 || reserved != 0) {
        return {{}, DecodeError::InvalidFlags};
    }
    if (payload_bytes != bytes.size() - k_header_bytes) {
        return {{}, DecodeError::InvalidLength};
    }
    if (count == 0 || count > k_max_frames_per_batch) {
        return {{}, DecodeError::InvalidCount};
    }

    RawFrameDecode out;
    out.frames.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        std::uint8_t tag{};
        std::int64_t receive_time{};
        std::uint32_t length{};
        if (!read_le(bytes, offset, tag) || !read_le(bytes, offset, receive_time) ||
            !read_le(bytes, offset, length)) {
            return {{}, DecodeError::Truncated};
        }
        if (!venues::is_valid(static_cast<venues::StreamTag>(tag))) {
            return {{}, DecodeError::InvalidEnum};
        }
        if (length > k_max_raw_frame_bytes) {
            return {{}, DecodeError::Oversized};
        }
        if (length == 0) {
            return {{}, DecodeError::InvalidLength};
        }
        if (bytes.size() - offset < length) {
            return {{}, DecodeError::Truncated};
        }
        const auto text = bytes.subspan(offset, length);
        out.frames.push_back({static_cast<venues::StreamTag>(tag), receive_time,
                              std::string(text.begin(), text.end())});
        offset += length;
    }
    if (offset != bytes.size()) {
        return {{}, DecodeError::InvalidLength};
    }
    return out;
}

std::vector<std::uint8_t> encode_raw_frames(std::span<const RawFrame> frames) {
    if (frames.empty() || frames.size() > k_max_frames_per_batch) {
        return {};
    }
    std::size_t body = 0;
    for (const auto &frame : frames) {
        if (!venues::is_valid(frame.tag) || frame.payload.empty() ||
            frame.payload.size() > k_max_raw_frame_bytes) {
            return {};
        }
        body += k_raw_frame_fixed_bytes + frame.payload.size();
    }
    if (k_header_bytes + body > k_max_raw_batch_bytes) {
        return {};
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(k_header_bytes + body);
    bytes.insert(bytes.end(), k_magic.begin(), k_magic.end());
    write_le(bytes, k_protocol_version);
    write_le(bytes, static_cast<std::uint8_t>(MessageKind::RawFrameBatch));
    write_le(bytes, std::uint8_t{0});
    write_le(bytes, static_cast<std::uint32_t>(body));
    write_le(bytes, static_cast<std::uint16_t>(frames.size()));
    write_le(bytes, std::uint16_t{0});
    for (const auto &frame : frames) {
        write_le(bytes, static_cast<std::uint8_t>(frame.tag));
        write_le(bytes, frame.receive_time_ms);
        write_le(bytes, static_cast<std::uint32_t>(frame.payload.size()));
        bytes.insert(bytes.end(), frame.payload.begin(), frame.payload.end());
    }
    return bytes;
}

} // namespace market_classifier::bridge
