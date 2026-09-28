#include "market_classifier/bridge/browser_api.hpp"

#include "market_classifier/bridge/protocol.hpp"
#include "market_classifier/bridge/raw_frame.hpp"
#include "market_classifier/runtime/engine.hpp"

#include <span>
#include <utility>
#include <vector>

namespace market_classifier::bridge {

runtime::Engine &app_engine() {
    static const runtime::SystemClock clock;
    static runtime::Engine instance(clock);
    return instance;
}

} // namespace market_classifier::bridge

namespace {

market_classifier::runtime::Engine &engine() {
    return market_classifier::bridge::app_engine();
}

bool venue_from_int(int venue, market_classifier::domain::Venue &out) noexcept {
    if (venue == 0) {
        out = market_classifier::domain::Venue::BinanceUsdM;
        return true;
    }
    if (venue == 1) {
        out = market_classifier::domain::Venue::Hyperliquid;
        return true;
    }
    return false;
}

} // namespace

extern "C" int mc_bridge_decode(const std::uint8_t *bytes, std::size_t size) noexcept {
    if (bytes == nullptr && size != 0) {
        return static_cast<int>(market_classifier::bridge::DecodeError::Truncated);
    }
    try {
        const std::span<const std::uint8_t> input{bytes, size};
        if (size > 6 && input[6] == static_cast<std::uint8_t>(
                                        market_classifier::bridge::MessageKind::RawFrameBatch)) {
            return static_cast<int>(market_classifier::bridge::decode_raw_frames(input).error);
        }
        return static_cast<int>(market_classifier::bridge::decode(input).error);
    } catch (...) {
        return 255;
    }
}

extern "C" const char *mc_bridge_result_name(int code) noexcept {
    using market_classifier::bridge::DecodeError;
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
    case 255:
        return "internal-error";
    default:
        return "unknown-error";
    }
}

extern "C" int mc_submit_raw(const std::uint8_t *bytes, std::size_t size) noexcept {
    if (bytes == nullptr && size != 0) {
        return static_cast<int>(market_classifier::runtime::RawSubmit::RejectedDecode);
    }
    try {
        return static_cast<int>(engine().submit_raw({bytes, size}));
    } catch (...) {
        return 255;
    }
}

extern "C" void mc_socket_event(int venue, int kind) noexcept {
    market_classifier::domain::Venue v{};
    if (!venue_from_int(venue, v) || kind < 0 || kind > 2) {
        return;
    }
    try {
        engine().on_socket_event(v, static_cast<market_classifier::runtime::SocketEvent>(kind));
    } catch (...) {
    }
}

extern "C" int mc_should_reconnect(int venue) noexcept {
    market_classifier::domain::Venue v{};
    return venue_from_int(venue, v) && engine().should_reconnect(v) ? 1 : 0;
}

extern "C" int mc_request_snapshot(int venue) noexcept {
    market_classifier::domain::Venue v{};
    return venue_from_int(venue, v) && engine().take_snapshot_request(v) ? 1 : 0;
}

extern "C" void mc_metadata_failed(int venue) noexcept {
    market_classifier::domain::Venue v{};
    if (venue_from_int(venue, v)) {
        engine().on_metadata_failed(v);
    }
}

extern "C" void mc_frames_dropped(int venue, int count) noexcept {
    market_classifier::domain::Venue v{};
    if (venue_from_int(venue, v) && count > 0) {
        engine().on_frames_dropped(v, static_cast<std::uint32_t>(count));
    }
}

extern "C" void mc_retry_venue(int venue) noexcept {
    market_classifier::domain::Venue v{};
    if (venue_from_int(venue, v)) {
        engine().retry(v);
    }
}

extern "C" void mc_engine_frame(int budget_ms) noexcept {
    try {
        engine().frame(budget_ms < 0 ? 0 : budget_ms);
    } catch (...) {
    }
}

extern "C" double mc_venue_stat(int venue, int which) noexcept {
    market_classifier::domain::Venue v{};
    if (which == 7) {
        return static_cast<double>(engine().rejected_batches());
    }
    if (!venue_from_int(venue, v)) {
        return -1;
    }
    const auto &c = engine().counters(v);
    switch (which) {
    case 0:
        return static_cast<double>(c.frames_received);
    case 1:
        return static_cast<double>(c.frames_adapted);
    case 2:
        return static_cast<double>(c.frames_dropped);
    case 3:
        return static_cast<double>(c.events);
    case 4:
        return static_cast<double>(c.reconnects);
    case 5:
        return static_cast<double>(engine().queued_frames(v));
    case 6:
        return static_cast<double>(engine().feed(v).phase());
    case 8:
        return static_cast<double>(engine().feed(v).age_ms(engine().now_ms()));
    default:
        return -1;
    }
}
