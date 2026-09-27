#include "market_classifier/bridge/protocol.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace market_classifier::bridge {
namespace {

constexpr std::array<std::uint8_t, 4> k_magic{'M', 'C', 'B', '1'};
constexpr std::size_t k_record_fixed_bytes = 59;

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

bool valid_quality(std::uint8_t value) {
    return domain::is_valid(static_cast<domain::DataQuality>(value));
}

bool valid_text(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte >= 0x21 && byte <= 0x7E;
    });
}

// Decodes one trade record at `offset`, appending it to `trades` on success.
DecodeError decode_record(std::span<const std::uint8_t> bytes, std::size_t &offset,
                          std::vector<domain::Trade> &trades) {
    const auto record_start = offset;
    std::uint16_t record_bytes{};
    std::uint8_t venue{};
    std::uint8_t quality{};
    std::uint8_t side{};
    std::uint8_t symbol_bytes{};
    std::uint8_t source_id_bytes{};
    std::uint8_t record_reserved{};
    std::int64_t source_time{};
    std::int64_t receive_time{};
    std::int64_t price_mantissa{};
    std::int64_t quantity_mantissa{};
    std::int64_t notional_mantissa{};
    std::uint64_t sequence{};
    std::uint8_t price_scale{};
    std::uint8_t quantity_scale{};
    std::uint8_t notional_scale{};
    if (!read_le(bytes, offset, record_bytes) || !read_le(bytes, offset, venue) ||
        !read_le(bytes, offset, quality) || !read_le(bytes, offset, side) ||
        !read_le(bytes, offset, symbol_bytes) || !read_le(bytes, offset, source_id_bytes) ||
        !read_le(bytes, offset, record_reserved) || !read_le(bytes, offset, source_time) ||
        !read_le(bytes, offset, receive_time) || !read_le(bytes, offset, sequence) ||
        !read_le(bytes, offset, price_mantissa) || !read_le(bytes, offset, price_scale) ||
        !read_le(bytes, offset, quantity_mantissa) || !read_le(bytes, offset, quantity_scale) ||
        !read_le(bytes, offset, notional_mantissa) || !read_le(bytes, offset, notional_scale)) {
        return DecodeError::Truncated;
    }
    const auto expected = k_record_fixed_bytes + symbol_bytes + source_id_bytes;
    if (record_reserved != 0 || record_bytes != expected ||
        bytes.size() - record_start < expected) {
        return DecodeError::InvalidLength;
    }
    if (!domain::is_valid(static_cast<domain::Venue>(venue)) || !valid_quality(quality) ||
        side > static_cast<std::uint8_t>(domain::AggressorSide::Unknown)) {
        return DecodeError::InvalidEnum;
    }
    if (symbol_bytes == 0 || symbol_bytes > domain::InstrumentId::k_max_symbol_length) {
        return DecodeError::InvalidInstrument;
    }
    if (source_id_bytes == 0 || source_id_bytes > k_max_source_id_bytes) {
        return DecodeError::InvalidSourceId;
    }

    const auto price    = domain::Decimal::from_parts(price_mantissa, price_scale);
    const auto quantity = domain::Decimal::from_parts(quantity_mantissa, quantity_scale);
    const auto notional = domain::Decimal::from_parts(notional_mantissa, notional_scale);
    if (!price || !quantity || !notional) {
        return DecodeError::InvalidDecimal;
    }

    const auto symbol_span = bytes.subspan(offset, symbol_bytes);
    const std::string symbol(symbol_span.begin(), symbol_span.end());
    offset += symbol_bytes;
    const auto source_span = bytes.subspan(offset, source_id_bytes);
    const std::string source_id(source_span.begin(), source_span.end());
    offset += source_id_bytes;
    if (!valid_text(source_id)) {
        return DecodeError::InvalidSourceId;
    }
    const auto instrument = domain::InstrumentId::create(static_cast<domain::Venue>(venue), symbol);
    if (!instrument) {
        return DecodeError::InvalidInstrument;
    }
    const auto meta = domain::EventMeta::create(
        instrument.value, domain::SourceTimeMs{source_time}, domain::ReceiveTimeMs{receive_time},
        domain::LocalSequence{sequence}, static_cast<domain::DataQuality>(quality));
    if (!meta) {
        return DecodeError::InvalidMetadata;
    }
    trades.push_back({meta.value, source_id, static_cast<domain::AggressorSide>(side), price.value,
                      quantity.value, notional.value});
    return DecodeError::None;
}

} // namespace

DecodeResult decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < k_header_bytes) {
        return {{}, DecodeError::Truncated};
    }
    if (bytes.size() > k_max_message_bytes) {
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
    if (kind != static_cast<std::uint8_t>(MessageKind::TradeBatch)) {
        return {{}, DecodeError::UnknownKind};
    }
    if (flags != 0 || reserved != 0) {
        return {{}, DecodeError::InvalidFlags};
    }
    if (payload_bytes != bytes.size() - k_header_bytes) {
        return {{}, DecodeError::InvalidLength};
    }
    if (count > k_max_events_per_batch) {
        return {{}, DecodeError::InvalidCount};
    }

    std::vector<domain::Trade> trades;
    trades.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        if (const auto error = decode_record(bytes, offset, trades); error != DecodeError::None) {
            return {{}, error};
        }
    }
    if (offset != bytes.size()) {
        return {{}, DecodeError::InvalidLength};
    }
    return {std::move(trades), DecodeError::None};
}

std::vector<std::uint8_t> encode_trade_batch(std::span<const domain::Trade> trades) {
    if (trades.size() > k_max_events_per_batch) {
        return {};
    }
    std::size_t total = k_header_bytes;
    for (const auto &trade : trades) {
        if (!domain::is_valid(trade.meta.instrument().venue()) ||
            !domain::is_valid(trade.meta.quality()) ||
            static_cast<std::uint8_t>(trade.aggressor_side) >
                static_cast<std::uint8_t>(domain::AggressorSide::Unknown) ||
            trade.meta.source_time().value < 0 || trade.meta.receive_time().value < 0 ||
            trade.meta.local_sequence().value == 0 ||
            trade.meta.instrument().native_symbol().empty() ||
            trade.meta.instrument().native_symbol().size() >
                domain::InstrumentId::k_max_symbol_length ||
            !valid_text(trade.source_id) || trade.source_id.size() > k_max_source_id_bytes) {
            return {};
        }
        total += k_record_fixed_bytes + trade.meta.instrument().native_symbol().size() +
                 trade.source_id.size();
    }
    if (total > k_max_message_bytes || total > std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(total);
    bytes.insert(bytes.end(), k_magic.begin(), k_magic.end());
    write_le(bytes, k_protocol_version);
    write_le(bytes, static_cast<std::uint8_t>(MessageKind::TradeBatch));
    write_le(bytes, std::uint8_t{0});
    write_le(bytes, static_cast<std::uint32_t>(total - k_header_bytes));
    write_le(bytes, static_cast<std::uint16_t>(trades.size()));
    write_le(bytes, std::uint16_t{0});
    for (const auto &trade : trades) {
        const auto &symbol = trade.meta.instrument().native_symbol();
        write_le(bytes, static_cast<std::uint16_t>(k_record_fixed_bytes + symbol.size() +
                                                   trade.source_id.size()));
        write_le(bytes, static_cast<std::uint8_t>(trade.meta.instrument().venue()));
        write_le(bytes, static_cast<std::uint8_t>(trade.meta.quality()));
        write_le(bytes, static_cast<std::uint8_t>(trade.aggressor_side));
        write_le(bytes, static_cast<std::uint8_t>(symbol.size()));
        write_le(bytes, static_cast<std::uint8_t>(trade.source_id.size()));
        write_le(bytes, std::uint8_t{0});
        write_le(bytes, trade.meta.source_time().value);
        write_le(bytes, trade.meta.receive_time().value);
        write_le(bytes, trade.meta.local_sequence().value);
        write_le(bytes, trade.price.mantissa());
        write_le(bytes, trade.price.scale());
        write_le(bytes, trade.quantity.mantissa());
        write_le(bytes, trade.quantity.scale());
        write_le(bytes, trade.usd_notional.mantissa());
        write_le(bytes, trade.usd_notional.scale());
        bytes.insert(bytes.end(), symbol.begin(), symbol.end());
        bytes.insert(bytes.end(), trade.source_id.begin(), trade.source_id.end());
    }
    return bytes;
}

} // namespace market_classifier::bridge
