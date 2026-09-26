#include "market_classifier/domain/types.hpp"

#include <tuple>
#include <utility>

namespace market_classifier::domain {

InstrumentIdResult InstrumentId::create(Venue venue, std::string_view native_symbol) {
    if (!is_valid(venue)) {
        return {{}, InstrumentIdError::InvalidVenue};
    }
    if (native_symbol.empty()) {
        return {{}, InstrumentIdError::EmptySymbol};
    }
    if (native_symbol.size() > k_max_symbol_length) {
        return {{}, InstrumentIdError::SymbolTooLong};
    }
    for (const char k_character : native_symbol) {
        const auto k_byte = static_cast<unsigned char>(k_character);
        if (k_byte < 0x21 || k_byte > 0x7E) {
            return {{}, InstrumentIdError::InvalidSymbol};
        }
    }
    return {InstrumentId{venue, std::string{native_symbol}}, InstrumentIdError::None};
}

std::strong_ordering InstrumentId::operator<=>(const InstrumentId &other) const noexcept {
    return std::tie(venue_, native_symbol_) <=> std::tie(other.venue_, other.native_symbol_);
}

bool InstrumentId::operator==(const InstrumentId &other) const noexcept {
    return venue_ == other.venue_ && native_symbol_ == other.native_symbol_;
}

bool can_transition(DataQuality from, DataQuality to) noexcept {
    if (!is_valid(from) || !is_valid(to)) {
        return false;
    }
    if (from == to) {
        return true;
    }
    if (to == DataQuality::Unsupported) {
        return true;
    }

    switch (from) {
    case DataQuality::Live:
    case DataQuality::Delayed:
    case DataQuality::Stale:
    case DataQuality::Partial:
        return to != DataQuality::Unsupported;
    case DataQuality::Reconnecting:
        return to == DataQuality::Live || to == DataQuality::Delayed || to == DataQuality::Stale ||
               to == DataQuality::GapDetected || to == DataQuality::Partial ||
               to == DataQuality::Failed;
    case DataQuality::GapDetected:
        return to == DataQuality::Reconnecting || to == DataQuality::Partial ||
               to == DataQuality::Failed;
    case DataQuality::Failed:
        return to == DataQuality::Reconnecting;
    case DataQuality::Unsupported:
        return false;
    }
    return false;
}

EventMetaResult EventMeta::create(InstrumentId instrument, SourceTimeMs source_time,
                                  ReceiveTimeMs receive_time, LocalSequence local_sequence,
                                  DataQuality quality) {
    if (!is_valid(instrument.venue()) || instrument.native_symbol().empty()) {
        return {{}, EventMetaError::InvalidInstrument};
    }
    if (source_time.value < 0) {
        return {{}, EventMetaError::InvalidSourceTime};
    }
    if (receive_time.value < 0) {
        return {{}, EventMetaError::InvalidReceiveTime};
    }
    if (local_sequence.value == 0) {
        return {{}, EventMetaError::InvalidLocalSequence};
    }
    if (!is_valid(quality)) {
        return {{}, EventMetaError::InvalidQuality};
    }
    return {EventMeta{std::move(instrument), source_time, receive_time, local_sequence, quality},
            EventMetaError::None};
}

} // namespace market_classifier::domain
