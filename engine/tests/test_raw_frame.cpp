#include "market_classifier/bridge/raw_frame.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace market_classifier;

namespace {

std::vector<std::uint8_t> read_hex(const std::string &path) {
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::vector<std::uint8_t> out;
    std::string digits;
    for (const char c : buffer.str()) {
        if (std::isxdigit(static_cast<unsigned char>(c)) != 0) {
            digits.push_back(c);
        }
    }
    for (std::size_t i = 0; i + 1 < digits.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>(std::stoul(digits.substr(i, 2), nullptr, 16)));
    }
    return out;
}

void set_u32(std::vector<std::uint8_t> &bytes, std::size_t at, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<std::uint8_t>(v >> (i * 8U));
    }
}

} // namespace

TEST_CASE("raw frame batch round-trips") {
    std::vector<bridge::RawFrame> in{{venues::StreamTag::BinanceWs, 1700000000000, R"({"a":1})"},
                                     {venues::StreamTag::HyperliquidWs, 1700000000001, "{}"}};
    const auto bytes = bridge::encode_raw_frames(in);
    const auto out   = bridge::decode_raw_frames(bytes);
    REQUIRE(out);
    REQUIRE(out.frames.size() == 2);
    CHECK(out.frames[0].payload == R"({"a":1})");
    CHECK(out.frames[0].receive_time_ms == 1700000000000);
    CHECK(out.frames[1].tag == venues::StreamTag::HyperliquidWs);
}

TEST_CASE("raw frame rejects unknown tag, oversize frame, count overflow, truncation") {
    const auto bytes = bridge::encode_raw_frames(
        std::vector<bridge::RawFrame>{{venues::StreamTag::BinanceWs, 1, "{}"}});
    REQUIRE_FALSE(bytes.empty());

    // Frame layout: u8 tag at the start of the body (count lives in the header).
    auto bad_tag                    = bytes;
    bad_tag[bridge::k_header_bytes] = 0xEE;
    CHECK(bridge::decode_raw_frames(bad_tag).error == bridge::DecodeError::InvalidEnum);

    auto truncated = bytes;
    truncated.pop_back();
    CHECK_FALSE(bridge::decode_raw_frames(truncated));

    std::vector<bridge::RawFrame> many(bridge::k_max_frames_per_batch + 1,
                                       {venues::StreamTag::BinanceWs, 1, "{}"});
    CHECK(bridge::encode_raw_frames(many).empty());

    std::vector<bridge::RawFrame> huge{
        {venues::StreamTag::BinanceWs, 1, std::string(bridge::k_max_raw_frame_bytes + 1, 'x')}};
    CHECK(bridge::encode_raw_frames(huge).empty());

    // Frame length field claims more than the bound.
    auto lying = bytes;
    set_u32(lying, bridge::k_header_bytes + 9, bridge::k_max_raw_frame_bytes + 1);
    CHECK(bridge::decode_raw_frames(lying).error == bridge::DecodeError::Oversized);

    // Header count above the batch bound.
    auto counted                        = bytes;
    counted[bridge::k_header_bytes - 4] = 0xFF;
    counted[bridge::k_header_bytes - 3] = 0x00;
    CHECK(bridge::decode_raw_frames(counted).error == bridge::DecodeError::InvalidCount);

    // A trade-batch kind byte is not a raw frame batch.
    auto wrong_kind = bytes;
    wrong_kind[6]   = static_cast<std::uint8_t>(bridge::MessageKind::TradeBatch);
    CHECK(bridge::decode_raw_frames(wrong_kind).error == bridge::DecodeError::UnknownKind);

    CHECK(bridge::decode_raw_frames({}).error == bridge::DecodeError::Truncated);
}

TEST_CASE("raw frame decoder refuses an empty payload") {
    const auto bytes = bridge::encode_raw_frames(
        std::vector<bridge::RawFrame>{{venues::StreamTag::BinanceWs, 1, ""}});
    CHECK(bytes.empty());
}

TEST_CASE("TS-encoded fixture decodes identically") {
    const auto bytes = read_hex(MC_SOURCE_DIR "/fixtures/mvp/raw-frame-batch-v1.hex");
    const auto out   = bridge::decode_raw_frames(bytes);
    REQUIRE(out);
    REQUIRE(out.frames.size() == 2);
    CHECK(out.frames[0].tag == venues::StreamTag::BinanceWs);
    CHECK(out.frames[0].receive_time_ms == 1700000000000);
    CHECK(out.frames[0].payload == R"({"stream":"btcusdt@aggTrade"})");
    CHECK(out.frames[1].tag == venues::StreamTag::HyperliquidWs);
    CHECK(out.frames[1].receive_time_ms == 1700000000001);
    CHECK(out.frames[1].payload == R"({"channel":"pong"})");
    CHECK(bridge::encode_raw_frames(out.frames) == bytes);
}
