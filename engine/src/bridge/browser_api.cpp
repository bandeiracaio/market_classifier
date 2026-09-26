#include "market_classifier/bridge/browser_api.hpp"

#include "market_classifier/bridge/protocol.hpp"
#include "market_classifier/runtime/ingress.hpp"
#include "market_classifier/runtime/read_model.hpp"

#include <span>
#include <utility>
#include <vector>

namespace {

market_classifier::runtime::BoundedIngress g_ingress;
market_classifier::runtime::DummyReadModel g_read_model;

} // namespace

extern "C" int mc_bridge_decode(const std::uint8_t *bytes, std::size_t size) noexcept {
    if (bytes == nullptr && size != 0) {
        return static_cast<int>(market_classifier::bridge::DecodeError::Truncated);
    }
    try {
        const auto decoded = market_classifier::bridge::decode({bytes, size});
        return static_cast<int>(decoded.error);
    } catch (...) {
        return 255;
    }
}

extern "C" int mc_bridge_submit(const std::uint8_t *bytes, std::size_t size) noexcept {
    if (bytes == nullptr && size != 0) {
        return static_cast<int>(market_classifier::bridge::DecodeError::Truncated);
    }
    try {
        auto decoded = market_classifier::bridge::decode({bytes, size});
        if (!decoded) {
            return static_cast<int>(decoded.error);
        }

        std::vector<market_classifier::domain::NormalizedEvent> events;
        events.reserve(decoded.trades.size());
        for (auto &trade : decoded.trades) {
            events.emplace_back(std::move(trade));
        }
        const auto result = g_ingress.submit({std::move(events), size});
        if (result != market_classifier::runtime::SubmitResult::Accepted &&
            result != market_classifier::runtime::SubmitResult::AcceptedWithDrop) {
            return 100 + static_cast<int>(result);
        }
        market_classifier::runtime::drain_ingress(g_ingress, g_read_model);
        return 0;
    } catch (...) {
        return 255;
    }
}

extern "C" const char *mc_bridge_result_name(int code) noexcept {
    using market_classifier::bridge::DecodeError;
    using market_classifier::runtime::SubmitResult;
    switch (code) {
    case 0:
        return "accepted";
    case static_cast<int>(DecodeError::Truncated):
        return "truncated";
    case static_cast<int>(DecodeError::Oversized):
        return "oversized";
    case static_cast<int>(DecodeError::InvalidMagic):
        return "invalid-magic";
    case static_cast<int>(DecodeError::UnsupportedVersion):
        return "unsupported-version";
    case static_cast<int>(DecodeError::UnknownKind):
        return "unknown-kind";
    case static_cast<int>(DecodeError::InvalidFlags):
        return "invalid-flags";
    case static_cast<int>(DecodeError::InvalidLength):
        return "invalid-length";
    case static_cast<int>(DecodeError::InvalidCount):
        return "invalid-count";
    case static_cast<int>(DecodeError::InvalidEnum):
        return "invalid-enum";
    case static_cast<int>(DecodeError::InvalidDecimal):
        return "invalid-decimal";
    case static_cast<int>(DecodeError::InvalidInstrument):
        return "invalid-instrument";
    case static_cast<int>(DecodeError::InvalidMetadata):
        return "invalid-metadata";
    case static_cast<int>(DecodeError::InvalidSourceId):
        return "invalid-source-id";
    case 100 + static_cast<int>(SubmitResult::RejectedEmpty):
        return "rejected-empty";
    case 100 + static_cast<int>(SubmitResult::RejectedEventLimit):
        return "rejected-event-limit";
    case 100 + static_cast<int>(SubmitResult::RejectedByteLimit):
        return "rejected-byte-limit";
    case 100 + static_cast<int>(SubmitResult::RejectedOwnedSize):
        return "rejected-owned-size";
    case 255:
        return "internal-error";
    default:
        return "unknown-error";
    }
}

extern "C" std::uint64_t mc_bridge_read_model_events() noexcept {
    return g_read_model.total_events;
}

extern "C" std::uint64_t mc_bridge_dropped_batches() noexcept {
    return g_read_model.ingress_counters.dropped_batches;
}
