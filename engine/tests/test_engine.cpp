#include "market_classifier/runtime/clock.hpp"
#include "market_classifier/runtime/engine.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "fixture_util.hpp"

using namespace market_classifier;
using venues::StreamTag;

namespace {

std::vector<std::uint8_t> batch(StreamTag tag, const std::string &payload, std::int64_t t = 1) {
    return bridge::encode_raw_frames(std::vector<bridge::RawFrame>{{tag, t, payload}});
}

void feed_live(runtime::Engine &e, domain::Venue v) {
    e.on_socket_event(v, runtime::SocketEvent::Open);
    const auto tag =
        v == domain::Venue::BinanceUsdM ? StreamTag::BinanceWs : StreamTag::HyperliquidWs;
    const auto file =
        v == domain::Venue::BinanceUsdM ? "binance/aggTrade.json" : "hyperliquid/trades.json";
    const auto bytes = batch(tag, mc_test::load_frame(file, tag, 1).payload);
    REQUIRE(e.submit_raw(bytes) == runtime::RawSubmit::Accepted);
    e.frame(16);
    REQUIRE(e.feed(v).phase() == runtime::FeedPhase::Live);
}

} // namespace

TEST_CASE("disconnecting one venue leaves the other untouched") {
    runtime::FakeClock clock;
    runtime::Engine e(clock);
    feed_live(e, domain::Venue::BinanceUsdM);
    feed_live(e, domain::Venue::Hyperliquid);
    const auto binance_frames = e.counters(domain::Venue::BinanceUsdM).frames_adapted;
    e.on_socket_event(domain::Venue::Hyperliquid, runtime::SocketEvent::Close);
    CHECK(e.feed(domain::Venue::Hyperliquid).phase() == runtime::FeedPhase::Reconnecting);
    CHECK(e.feed(domain::Venue::BinanceUsdM).phase() == runtime::FeedPhase::Live);
    CHECK(e.counters(domain::Venue::BinanceUsdM).frames_adapted == binance_frames);
    CHECK(e.counters(domain::Venue::Hyperliquid).reconnects == 1);
    CHECK(e.counters(domain::Venue::BinanceUsdM).reconnects == 0);
}

TEST_CASE("silent feed becomes stale on frame tick") {
    runtime::FakeClock clock;
    runtime::Engine e(clock);
    feed_live(e, domain::Venue::BinanceUsdM);
    REQUIRE(clock.advance_ms(6000));
    e.frame(16);
    CHECK(e.feed(domain::Venue::BinanceUsdM).phase() == runtime::FeedPhase::Stale);
    CHECK(e.feed(domain::Venue::BinanceUsdM).age_ms(clock.monotonic_time_ms()) == 6000);
}

TEST_CASE("engine frame drains at most budget and keeps remainder bounded") {
    runtime::FakeClock clock;
    runtime::Engine e(clock);
    e.on_socket_event(domain::Venue::BinanceUsdM, runtime::SocketEvent::Open);
    const auto payload =
        mc_test::load_frame("binance/aggTrade.json", StreamTag::BinanceWs, 1).payload;
    std::vector<bridge::RawFrame> frames(bridge::k_max_frames_per_batch,
                                         {StreamTag::BinanceWs, 1, payload});
    const auto bytes              = bridge::encode_raw_frames(frames);
    const std::size_t submissions = runtime::k_max_queued_raw_frames / frames.size() + 3;
    for (std::size_t i = 0; i < submissions; ++i) {
        const auto r = e.submit_raw(bytes);
        CHECK((r == runtime::RawSubmit::Accepted || r == runtime::RawSubmit::AcceptedWithDrop));
    }
    const auto &c = e.counters(domain::Venue::BinanceUsdM);
    CHECK(e.queued_frames(domain::Venue::BinanceUsdM) <= runtime::k_max_queued_raw_frames);
    CHECK(c.frames_dropped == submissions * frames.size() - runtime::k_max_queued_raw_frames);
    CHECK(e.data_gap(domain::Venue::BinanceUsdM));
    e.frame(16);
    CHECK(c.frames_adapted == runtime::k_max_frames_per_drain);
    CHECK(e.queued_frames(domain::Venue::BinanceUsdM) ==
          runtime::k_max_queued_raw_frames - runtime::k_max_frames_per_drain);
}

TEST_CASE("engine rejects invalid batches and counts adapter errors") {
    runtime::FakeClock clock;
    runtime::Engine e(clock);
    const std::vector<std::uint8_t> garbage{1, 2, 3};
    CHECK(e.submit_raw(garbage) == runtime::RawSubmit::RejectedDecode);
    CHECK(e.rejected_batches() == 1);
    const auto bad = batch(StreamTag::BinanceWs, mc_test::load_malformed("bad_price").payload);
    REQUIRE(e.submit_raw(bad) == runtime::RawSubmit::Accepted);
    const auto wrong = batch(StreamTag::BinanceWs, mc_test::load_malformed("wrong_symbol").payload);
    REQUIRE(e.submit_raw(wrong) == runtime::RawSubmit::Accepted);
    e.frame(16);
    const auto &c = e.counters(domain::Venue::BinanceUsdM);
    CHECK(c.adapter_errors[static_cast<std::size_t>(venues::AdapterError::Malformed)] == 1);
    CHECK(c.adapter_errors[static_cast<std::size_t>(venues::AdapterError::WrongSymbol)] == 1);
    CHECK(c.frames_adapted == 0);
}

TEST_CASE("binance book resync is requested once and satisfied by a snapshot") {
    runtime::FakeClock clock;
    runtime::Engine e(clock);
    e.on_socket_event(domain::Venue::BinanceUsdM, runtime::SocketEvent::Open);
    const auto lines = mc_test::read_lines("binance/depth_sequence.jsonl");
    for (std::size_t i = 1; i <= 20; ++i) {
        REQUIRE(e.submit_raw(batch(StreamTag::BinanceWs, lines[i])) ==
                runtime::RawSubmit::Accepted);
    }
    e.frame(16);
    CHECK(e.take_snapshot_request(domain::Venue::BinanceUsdM));
    CHECK_FALSE(e.take_snapshot_request(domain::Venue::BinanceUsdM));
    CHECK_FALSE(e.take_snapshot_request(domain::Venue::Hyperliquid));
    const std::string prefix = R"({"snapshot":)";
    const auto snap          = lines[0].substr(prefix.size(), lines[0].size() - prefix.size() - 1);
    REQUIRE(e.submit_raw(batch(StreamTag::BinanceDepthSnapshot, snap)) ==
            runtime::RawSubmit::Accepted);
    for (std::size_t i = 21; i < lines.size(); ++i) {
        REQUIRE(e.submit_raw(batch(StreamTag::BinanceWs, lines[i])) ==
                runtime::RawSubmit::Accepted);
        e.frame(16);
    }
    CHECK(e.binance_book().state() == books::SyncState::Live);
    // Reconnect invalidates the book and asks for a new snapshot.
    e.on_socket_event(domain::Venue::BinanceUsdM, runtime::SocketEvent::Close);
    CHECK(e.binance_book().state() != books::SyncState::Live);
    e.on_socket_event(domain::Venue::BinanceUsdM, runtime::SocketEvent::Open);
    REQUIRE(e.submit_raw(batch(StreamTag::BinanceWs, lines[5])) == runtime::RawSubmit::Accepted);
    e.frame(16);
    CHECK(e.take_snapshot_request(domain::Venue::BinanceUsdM));
}

TEST_CASE("hyperliquid l2Book snapshots replace the venue book") {
    runtime::FakeClock clock;
    runtime::Engine e(clock);
    e.on_socket_event(domain::Venue::Hyperliquid, runtime::SocketEvent::Open);
    const auto frame = mc_test::load_frame("hyperliquid/l2Book.json", StreamTag::HyperliquidWs, 1);
    REQUIRE(e.submit_raw(batch(frame.tag, frame.payload)) == runtime::RawSubmit::Accepted);
    e.frame(16);
    REQUIRE(e.hyperliquid_book().bids().size() == 20);
    CHECK_FALSE(e.hyperliquid_book().crossed());
}

TEST_CASE("metadata failure fails only that venue and retry restarts it") {
    runtime::FakeClock clock;
    runtime::Engine e(clock);
    feed_live(e, domain::Venue::Hyperliquid);
    e.on_metadata_failed(domain::Venue::BinanceUsdM);
    CHECK(e.feed_quality(domain::Venue::BinanceUsdM) == domain::DataQuality::Failed);
    CHECK(e.feed(domain::Venue::Hyperliquid).phase() == runtime::FeedPhase::Live);
    CHECK_FALSE(e.should_reconnect(domain::Venue::BinanceUsdM));
    e.retry(domain::Venue::BinanceUsdM);
    CHECK(e.should_reconnect(domain::Venue::BinanceUsdM));
}

// --- Final-review fixes (docs/reviews/mvp-verification.md) ---

TEST_CASE("replayed Hyperliquid trades after reconnect are not double-counted") {
    runtime::FakeClock clock;
    runtime::Engine e(clock);
    e.on_socket_event(domain::Venue::Hyperliquid, runtime::SocketEvent::Open);
    const auto frame = mc_test::load_frame("hyperliquid/trades.json", StreamTag::HyperliquidWs, 1);
    REQUIRE(e.submit_raw(batch(frame.tag, frame.payload)) == runtime::RawSubmit::Accepted);
    e.frame(16);
    const auto tape = e.view(domain::Venue::Hyperliquid).tape.trades().size();
    const auto cvd  = e.view(domain::Venue::Hyperliquid).cvd.value();
    REQUIRE(tape == 30);
    // Hyperliquid replays recent trades on (re)subscribe (observed 2026-09-27).
    e.on_socket_event(domain::Venue::Hyperliquid, runtime::SocketEvent::Close);
    e.on_socket_event(domain::Venue::Hyperliquid, runtime::SocketEvent::Open);
    REQUIRE(e.submit_raw(batch(frame.tag, frame.payload)) == runtime::RawSubmit::Accepted);
    e.frame(16);
    CHECK(e.view(domain::Venue::Hyperliquid).tape.trades().size() == tape);
    CHECK(e.view(domain::Venue::Hyperliquid).cvd.value() == cvd);
    CHECK(e.counters(domain::Venue::Hyperliquid).duplicate_trades == 30);
}

TEST_CASE("binance aggTrade id gap marks a discontinuity") {
    runtime::FakeClock clock;
    runtime::Engine e(clock);
    e.on_socket_event(domain::Venue::BinanceUsdM, runtime::SocketEvent::Open);
    const auto trade = [](int id) {
        return R"({"stream":"btcusdt@aggTrade","data":{"e":"aggTrade","s":"BTCUSDT","a":)" +
               std::to_string(id) + R"(,"p":"1","q":"1","T":1000,"m":false}})";
    };
    for (const int id : {10, 11, 12}) {
        REQUIRE(e.submit_raw(batch(StreamTag::BinanceWs, trade(id))) ==
                runtime::RawSubmit::Accepted);
    }
    e.frame(16);
    CHECK(e.view(domain::Venue::BinanceUsdM).cvd.gap_count() == 0);
    REQUIRE(e.submit_raw(batch(StreamTag::BinanceWs, trade(20))) == runtime::RawSubmit::Accepted);
    REQUIRE(e.submit_raw(batch(StreamTag::BinanceWs, trade(20))) == runtime::RawSubmit::Accepted);
    e.frame(16);
    CHECK(e.view(domain::Venue::BinanceUsdM).cvd.gap_count() == 1);
    CHECK(e.view(domain::Venue::BinanceUsdM).tape.trades().size() == 4); // duplicate 20 dropped
}

TEST_CASE("frames dropped by the browser buffer mark a gap") {
    runtime::FakeClock clock;
    runtime::Engine e(clock);
    feed_live(e, domain::Venue::Hyperliquid);
    e.on_frames_dropped(domain::Venue::Hyperliquid, 7);
    CHECK(e.counters(domain::Venue::Hyperliquid).frames_dropped == 7);
    CHECK(e.view(domain::Venue::Hyperliquid).cvd.gap_count() == 1);
    CHECK(e.view(domain::Venue::BinanceUsdM).cvd.gap_count() == 0);
}

TEST_CASE("an unanswered depth snapshot request is retried after a timeout") {
    runtime::FakeClock clock;
    runtime::Engine e(clock);
    e.on_socket_event(domain::Venue::BinanceUsdM, runtime::SocketEvent::Open);
    const auto lines = mc_test::read_lines("binance/depth_sequence.jsonl");
    REQUIRE(e.submit_raw(batch(StreamTag::BinanceWs, lines[1])) == runtime::RawSubmit::Accepted);
    e.frame(16);
    REQUIRE(e.take_snapshot_request(domain::Venue::BinanceUsdM));
    REQUIRE(clock.advance_ms(runtime::k_snapshot_retry_ms - 1));
    e.frame(16);
    CHECK_FALSE(e.take_snapshot_request(domain::Venue::BinanceUsdM));
    REQUIRE(clock.advance_ms(1));
    e.frame(16);
    CHECK(e.take_snapshot_request(domain::Venue::BinanceUsdM)); // REST failed or was rejected
}

TEST_CASE("rejected startup metadata fails only that venue") {
    runtime::FakeClock clock;
    runtime::Engine e(clock);
    feed_live(e, domain::Venue::Hyperliquid);
    e.on_socket_event(domain::Venue::BinanceUsdM, runtime::SocketEvent::Open);
    REQUIRE(e.submit_raw(
                batch(StreamTag::BinanceExchangeInfo, R"({"symbols":[{"symbol":"ETHUSDT"}]})")) ==
            runtime::RawSubmit::Accepted);
    e.frame(16);
    CHECK(e.feed_quality(domain::Venue::BinanceUsdM) == domain::DataQuality::Failed);
    CHECK(e.feed(domain::Venue::Hyperliquid).phase() == runtime::FeedPhase::Live);
}

TEST_CASE("a crossed Hyperliquid snapshot is not shown as a live book") {
    runtime::FakeClock clock;
    runtime::Engine e(clock);
    e.on_socket_event(domain::Venue::Hyperliquid, runtime::SocketEvent::Open);
    const std::string crossed =
        R"({"channel":"l2Book","data":{"coin":"BTC","time":1,"levels":[[{"px":"101","sz":"1","n":1}],[{"px":"100","sz":"1","n":1}]]}})";
    REQUIRE(e.submit_raw(batch(StreamTag::HyperliquidWs, crossed)) == runtime::RawSubmit::Accepted);
    e.frame(16);
    CHECK(e.book_quality(domain::Venue::Hyperliquid) == domain::DataQuality::GapDetected);
}
