// Acceptance #5: every bounded store stays within its bound under a synthetic flood.
#include "market_classifier/processors/limits.hpp"
#include "market_classifier/runtime/clock.hpp"
#include "market_classifier/runtime/engine.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace market_classifier;
using venues::StreamTag;

namespace {

constexpr std::int64_t k_start_ms = 1'790'000'000'000;
constexpr int k_minutes           = 120;       // 2 simulated hours
constexpr int k_trades_total      = 1'000'000; // across both venues
constexpr int k_deltas_total      = 100'000;
constexpr int k_trades_per_minute = k_trades_total / k_minutes;
constexpr int k_deltas_per_minute = k_deltas_total / k_minutes;

std::string binance_trade(std::int64_t t, int i) {
    const auto price = std::to_string(84000 + i % 500) + "." + std::to_string(i % 10);
    return R"({"stream":"btcusdt@aggTrade","data":{"e":"aggTrade","s":"BTCUSDT","a":)" +
           std::to_string(i) + R"(,"p":")" + price + R"(","q":"0.00)" + std::to_string(1 + i % 9) +
           R"(","T":)" + std::to_string(t) + R"(,"m":)" + (i % 2 == 0 ? "true" : "false") + "}}";
}

std::string hyperliquid_trade(std::int64_t t, int i) {
    return R"({"channel":"trades","data":[{"coin":"BTC","side":")" +
           std::string(i % 2 == 0 ? "B" : "A") + R"(","px":")" + std::to_string(84000 + i % 400) +
           R"(","sz":"0.01","time":)" + std::to_string(t) + R"(,"tid":)" + std::to_string(i) +
           "}]}";
}

// Continuous Binance diffs: U = previous u + 1, pu = previous u.
std::string depth_delta(std::int64_t t, std::uint64_t first, std::uint64_t last, std::uint64_t prev,
                        int i) {
    const auto bid = std::to_string(83990 - i % 300) + ".0";
    const auto ask = std::to_string(84010 + i % 300) + ".0";
    return R"({"stream":"btcusdt@depth@100ms","data":{"e":"depthUpdate","s":"BTCUSDT","T":)" +
           std::to_string(t) + R"(,"U":)" + std::to_string(first) + R"(,"u":)" +
           std::to_string(last) + R"(,"pu":)" + std::to_string(prev) + R"(,"b":[[")" + bid +
           R"(","1.5"]],"a":[[")" + ask + R"(","2"]]}})";
}

std::string kline(std::int64_t open) {
    return R"({"stream":"btcusdt@kline_1m","data":{"e":"kline","E":)" + std::to_string(open) +
           R"(,"s":"BTCUSDT","k":{"t":)" + std::to_string(open) + R"(,"T":)" +
           std::to_string(open + 59'999) +
           R"(,"i":"1m","o":"84000","h":"84010","l":"83990","c":"84005","v":"1","q":"84000","n":10,"x":true}}})";
}

class Flood {
  public:
    Flood() : clock_(k_start_ms, 0), engine_(clock_) {
        engine_.on_socket_event(domain::Venue::BinanceUsdM, runtime::SocketEvent::Open);
        engine_.on_socket_event(domain::Venue::Hyperliquid, runtime::SocketEvent::Open);
    }

    void add(StreamTag tag, std::string payload) {
        pending_.push_back({tag, clock_.wall_time_ms(), std::move(payload)});
        if (pending_.size() == bridge::k_max_frames_per_batch) {
            flush();
        }
    }

    // One animation frame: submit what arrived, then drain within the frame budget.
    void flush() {
        if (!pending_.empty()) {
            const auto bytes = bridge::encode_raw_frames(pending_);
            REQUIRE_FALSE(bytes.empty());
            const auto r = engine_.submit_raw(bytes);
            REQUIRE(r != runtime::RawSubmit::RejectedDecode);
            pending_.clear();
        }
        engine_.frame(16);
    }

    void advance(std::int64_t ms) {
        REQUIRE(clock_.advance_ms(ms));
        clock_.set_wall_time_ms(clock_.wall_time_ms() + ms);
    }

    runtime::Engine &engine() { return engine_; }
    std::int64_t now() const { return clock_.wall_time_ms(); }

  private:
    runtime::FakeClock clock_;
    runtime::Engine engine_;
    std::vector<bridge::RawFrame> pending_;
};

void check_bounds(const runtime::Engine &e) {
    for (const auto v : {domain::Venue::BinanceUsdM, domain::Venue::Hyperliquid}) {
        const auto &p = e.view(v);
        CHECK(p.tape.trades().size() <= processors::k_tape_capacity);
        CHECK(p.heatmap.columns().size() <= processors::k_heatmap_columns);
        CHECK(p.heatmap.trades().size() <= processors::k_heatmap_trades);
        for (const auto interval : processors::k_candle_intervals_ms) {
            CHECK(p.candles.candles(interval).size() <= processors::k_candle_capacity);
        }
        CHECK(p.cvd.series().size() <= processors::k_series_minutes);
        CHECK(p.footprint.candles().size() <= processors::k_series_minutes);
        for (std::size_t i = 0; i < p.footprint.candles().size(); ++i) {
            CHECK(p.footprint.candles()[i].cells.size() <= processors::k_max_footprint_cells);
        }
        CHECK(p.footprint.session_profile().buckets().size() <= processors::k_max_profile_buckets);
        CHECK(p.metrics.view().spread.size() <= processors::k_spread_seconds);
        CHECK(p.liquidations.items().size() <= processors::k_max_liquidations);
        CHECK(e.queued_frames(v) <= runtime::k_max_queued_raw_frames);
        CHECK(e.queued_bytes(v) <= runtime::k_max_queued_raw_bytes);
        CHECK(e.book(v).bids().size() <= books::k_max_book_levels_per_side);
        CHECK(e.book(v).asks().size() <= books::k_max_book_levels_per_side);
    }
    CHECK(e.binance_book().buffered() <= books::BinanceBookSync::k_max_buffered_diffs);
}

} // namespace

TEST_CASE("bounded stores hold under a two-hour synthetic flood") {
    Flood flood;
    // Seed the Binance book so deltas apply (snapshot at id 1000).
    std::uint64_t last_u = 1000;
    flood.add(StreamTag::BinanceWs, depth_delta(flood.now(), 999, 1000, 998, 0));
    flood.add(StreamTag::BinanceDepthSnapshot,
              R"({"lastUpdateId":1000,"T":1,"bids":[["83999.0","1"]],"asks":[["84001.0","1"]]})");
    flood.flush();
    REQUIRE(flood.engine().binance_book().state() == books::SyncState::Live);

    int trade = 0;
    int delta = 0;
    // 600 steps of 100 ms per simulated minute: heatmap columns (250 ms) and spread samples
    // (1 s) keep advancing, and 2 h overflows the 60-minute heatmap ring.
    constexpr int k_steps_per_minute = 600;
    for (int minute = 0; minute < k_minutes; ++minute) {
        for (int step = 0; step < k_steps_per_minute; ++step) {
            const auto t = flood.now();
            int trades   = k_trades_per_minute / k_steps_per_minute;
            int deltas   = k_deltas_per_minute / k_steps_per_minute;
            if (step == 0) { // remainders land at the start of each minute (a burst)
                trades += k_trades_per_minute % k_steps_per_minute;
                deltas += k_deltas_per_minute % k_steps_per_minute;
            }
            for (int k = 0; k < trades; ++k, ++trade) {
                if (trade % 2 == 0) {
                    flood.add(StreamTag::BinanceWs, binance_trade(t, trade));
                } else {
                    flood.add(StreamTag::HyperliquidWs, hyperliquid_trade(t, trade));
                }
            }
            for (int k = 0; k < deltas; ++k, ++delta) {
                flood.add(StreamTag::BinanceWs,
                          depth_delta(t, last_u + 1, last_u + 3, last_u, delta));
                last_u += 3;
            }
            flood.flush();
            flood.advance(100);
        }
        flood.add(StreamTag::BinanceWs, kline(k_start_ms + minute * 60'000LL));
        flood.flush();
        check_bounds(flood.engine());
    }
    CHECK(trade == k_trades_per_minute * k_minutes); // 999,960 trades
    CHECK(delta == k_deltas_per_minute * k_minutes); // 99,960 deltas
    CHECK(flood.engine().view(domain::Venue::BinanceUsdM).heatmap.columns().size() ==
          processors::k_heatmap_columns); // ring wrapped, still bounded
    CHECK(flood.engine().counters(domain::Venue::BinanceUsdM).frames_dropped == 0);
    // Nothing silently wrapped: CVD and footprint are still valid.
    CHECK_FALSE(flood.engine().view(domain::Venue::BinanceUsdM).cvd.failed());
    CHECK_FALSE(flood.engine().view(domain::Venue::Hyperliquid).footprint.failed());
    CHECK(flood.engine().binance_book().resync_count() == 0);
}

TEST_CASE("a burst beyond queue capacity drops oldest frames and stays bounded") {
    Flood flood;
    for (int i = 0; i < 20'000; ++i) { // no frame() in between: pure backlog
        std::vector<bridge::RawFrame> batch;
        batch.push_back({StreamTag::BinanceWs, flood.now(), binance_trade(flood.now(), i)});
        (void)flood.engine().submit_raw(bridge::encode_raw_frames(batch));
    }
    CHECK(flood.engine().queued_frames(domain::Venue::BinanceUsdM) ==
          runtime::k_max_queued_raw_frames);
    CHECK(flood.engine().counters(domain::Venue::BinanceUsdM).frames_dropped ==
          20'000 - runtime::k_max_queued_raw_frames);
    CHECK(flood.engine().data_gap(domain::Venue::BinanceUsdM));
    CHECK_FALSE(flood.engine().data_gap(domain::Venue::Hyperliquid)); // isolation
    check_bounds(flood.engine());
}
