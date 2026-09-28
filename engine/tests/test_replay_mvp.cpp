#include "market_classifier/runtime/clock.hpp"
#include "market_classifier/runtime/engine.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "fixture_util.hpp"

using namespace market_classifier;
using venues::StreamTag;

namespace {

constexpr std::int64_t k_receive_ms = 1790488600000; // after every captured timestamp

void submit(runtime::Engine &e, StreamTag tag, const std::string &payload) {
    const auto bytes =
        bridge::encode_raw_frames(std::vector<bridge::RawFrame>{{tag, k_receive_ms, payload}});
    REQUIRE(e.submit_raw(bytes) == runtime::RawSubmit::Accepted);
    e.frame(16);
}

void submit_all(runtime::Engine &e, const std::string &file, StreamTag tag) {
    for (const auto &frame : mc_test::split_array(mc_test::read_file(file))) {
        submit(e, tag, frame);
    }
}

std::string opt(const std::optional<domain::Decimal> &d) {
    return d ? d->to_string() : "-";
}

std::string quality(domain::DataQuality q) {
    static const char *names[] = {"Live",        "Delayed", "Stale",       "Reconnecting",
                                  "GapDetected", "Partial", "Unsupported", "Failed"};
    return names[static_cast<int>(q)];
}

// Feeds every committed fixture through the full engine path and renders key view
// values. Deterministic: FakeClock, fixed receive time, fixed order.
std::string run_replay() {
    runtime::FakeClock clock(k_receive_ms, 0);
    runtime::Engine e(clock);
    e.on_socket_event(domain::Venue::BinanceUsdM, runtime::SocketEvent::Open);
    e.on_socket_event(domain::Venue::Hyperliquid, runtime::SocketEvent::Open);

    submit(e, StreamTag::BinanceExchangeInfo, mc_test::read_file("binance/exchangeInfo.json"));
    submit(e, StreamTag::BinanceKlinesRest, mc_test::read_file("binance/klines_rest.json"));
    const auto lines         = mc_test::read_lines("binance/depth_sequence.jsonl");
    const std::string prefix = R"({"snapshot":)";
    for (std::size_t i = 1; i < lines.size(); ++i) {
        submit(e, StreamTag::BinanceWs, lines[i]);
        if (i == 20) {
            submit(e, StreamTag::BinanceDepthSnapshot,
                   lines[0].substr(prefix.size(), lines[0].size() - prefix.size() - 1));
        }
    }
    for (const auto *f : {"aggTrade", "bookTicker", "markPrice", "ticker", "kline", "forceOrder"}) {
        submit_all(e, std::string("binance/") + f + ".json", StreamTag::BinanceWs);
    }
    submit(e, StreamTag::BinanceOpenInterestRest, mc_test::read_file("binance/openInterest.json"));

    submit(e, StreamTag::HyperliquidMeta, mc_test::read_file("hyperliquid/metaAndAssetCtxs.json"));
    submit(e, StreamTag::HyperliquidCandleSnapshot,
           mc_test::read_file("hyperliquid/candleSnapshot.json"));
    for (const auto *f :
         {"subscription_ack", "l2Book", "trades", "bbo", "activeAssetCtx", "candle"}) {
        submit_all(e, std::string("hyperliquid/") + f + ".json", StreamTag::HyperliquidWs);
    }

    std::ostringstream out;
    for (const auto venue : {domain::Venue::BinanceUsdM, domain::Venue::Hyperliquid}) {
        const auto &p = e.view(venue);
        const auto &c = e.counters(venue);
        out << (venue == domain::Venue::BinanceUsdM ? "[binance]" : "[hyperliquid]") << '\n';
        out << "frames received=" << c.frames_received << " adapted=" << c.frames_adapted
            << " events=" << c.events << " dropped=" << c.frames_dropped << " errors=";
        for (const auto n : c.adapter_errors) {
            out << n << ',';
        }
        out << '\n';
        out << "feed=" << quality(e.feed_quality(venue))
            << " book=" << quality(e.book_quality(venue))
            << " liquidations=" << quality(e.liquidation_quality(venue)) << '\n';
        if (p.definition) {
            out << "tick=" << p.definition->tick_size.to_string()
                << " step=" << p.definition->quantity_step.to_string() << '\n';
        }
        const auto &book = e.book(venue);
        out << "book bids=" << book.bids().size() << " asks=" << book.asks().size()
            << " best_bid=" << (book.best_bid() ? book.best_bid()->price.to_string() : "-")
            << " best_ask=" << (book.best_ask() ? book.best_ask()->price.to_string() : "-")
            << " crossed=" << book.crossed() << '\n';
        out << "tape=" << p.tape.trades().size();
        if (!p.tape.trades().empty()) {
            out << " last=" << p.tape.trades().back().price.to_string();
        }
        out << '\n';
        out << "cvd=" << p.cvd.value().to_string() << " points=" << p.cvd.series().size()
            << " unknown=" << p.cvd.unknown_side_trades() << '\n';
        out << "footprint candles=" << p.footprint.candles().size();
        if (!p.footprint.candles().empty()) {
            const auto &fc = p.footprint.candles().back();
            out << " last_open=" << fc.open_time_ms << " cells=" << fc.cells.size();
        }
        out << " poc=" << opt(p.footprint.session_profile().poc()) << '\n';
        const auto one_min = p.candles.candles(60'000);
        out << "candles_1m=" << one_min.size();
        if (!one_min.empty()) {
            out << " last_open=" << one_min.back().open_time_ms
                << " last_close=" << one_min.back().close.to_string()
                << " closed=" << one_min.back().closed;
        }
        out << '\n';
        out << "heatmap columns=" << p.heatmap.columns().size()
            << " trades=" << p.heatmap.trades().size() << '\n';
        const auto &m = p.metrics.view();
        out << "mark=" << (m.asset ? m.asset->mark_price.to_string() : "-")
            << " funding=" << (m.asset ? m.asset->funding_rate.to_string() : "-") << " oi="
            << (m.open_interest_last ? m.open_interest_last->native_quantity.to_string() : "-")
            << " mid=" << opt(m.mid) << " spread_points=" << m.spread.size() << '\n';
        out << "summary last=" << (m.summary ? opt(m.summary->last_price) : "-")
            << " vol24h=" << (m.summary ? opt(m.summary->volume_24h) : "-") << '\n';
        out << "liquidations=" << p.liquidations.items().size() << '\n';
    }
    const auto basis = e.basis();
    out << "[basis] " << (basis ? basis->basis.to_string() : "-")
        << " bps=" << (basis ? std::to_string(basis->basis_bps) : "-") << '\n';
    return out.str();
}

} // namespace

TEST_CASE("mvp replay over captured fixtures matches the reviewed expectation") {
    const auto first  = run_replay();
    const auto second = run_replay();
    CHECK(first == second); // deterministic in one process

    const std::string path = MC_SOURCE_DIR "/fixtures/mvp/replay-expected.txt";
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        // First run writes the expectation for human review; it must then be committed.
        std::ofstream(path, std::ios::binary) << first;
        FAIL("wrote " << path << " — review and commit it");
    }
    std::stringstream expected;
    expected << in.rdbuf();
    auto text = expected.str();
    std::erase(text, '\r'); // tolerate CRLF checkouts
    CHECK(first == text);
}
