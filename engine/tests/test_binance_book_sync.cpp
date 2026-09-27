#include "market_classifier/books/binance_book_sync.hpp"
#include "market_classifier/venues/binance_adapter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>
#include <vector>

#include "fixture_util.hpp"

using namespace market_classifier;

namespace {

using Levels = std::vector<std::pair<const char *, const char *>>;

domain::Decimal dec(const char *text) {
    return domain::Decimal::parse(text).value;
}

std::vector<domain::BookLevel> levels(const Levels &in) {
    std::vector<domain::BookLevel> out;
    for (const auto &[px, qty] : in) {
        out.push_back({dec(px), dec(qty), std::nullopt});
    }
    return out;
}

domain::BookDelta delta(std::uint64_t first, std::uint64_t last, const Levels &bids,
                        const Levels &asks) {
    domain::BookDelta d;
    d.first_source_sequence = first;
    d.last_source_sequence  = last;
    d.changed_bids          = levels(bids);
    d.changed_asks          = levels(asks);
    return d;
}

domain::BookSnapshot snapshot(std::uint64_t last_update_id, const Levels &bids,
                              const Levels &asks) {
    domain::BookSnapshot s;
    s.source_sequence = last_update_id;
    s.bids            = levels(bids);
    s.asks            = levels(asks);
    return s;
}

books::BinanceBookSync live_sync_at(std::uint64_t u) {
    books::BinanceBookSync sync;
    sync.on_delta(delta(u - 5, u, {}, {}), u - 6);
    sync.on_snapshot(snapshot(u - 1, {{"99.0", "2"}}, {{"101.0", "3"}}));
    REQUIRE(sync.state() == books::SyncState::Live);
    return sync;
}

} // namespace

TEST_CASE("sync applies first diff straddling snapshot then continuous diffs") {
    books::BinanceBookSync sync;
    CHECK(sync.state() == books::SyncState::AwaitingSnapshot);
    CHECK(sync.on_delta(delta(95, 105, {{"100.0", "1"}}, {}), 90).request_snapshot);
    CHECK(sync.on_snapshot(snapshot(100, {{"99.0", "2"}}, {{"101.0", "3"}})).request_snapshot ==
          false);
    CHECK(sync.state() == books::SyncState::Live);
    CHECK(sync.book().best_bid()->price == dec("100.0"));
    CHECK_FALSE(sync.on_delta(delta(106, 110, {{"100.0", "0"}}, {}), 105).request_snapshot);
    CHECK(sync.book().best_bid()->price == dec("99.0"));
    CHECK(sync.state() == books::SyncState::Live);
}

TEST_CASE("pu discontinuity triggers gap and resync request") {
    auto sync = live_sync_at(110);
    CHECK(sync.on_delta(delta(115, 120, {}, {}), /*pu*/ 112).request_snapshot);
    CHECK(sync.state() == books::SyncState::GapDetected);
    CHECK(sync.resync_count() == 1);
    CHECK(sync.book().bids().empty());
    // Diffs after the gap are buffered, and a fresh snapshot recovers.
    sync.on_delta(delta(121, 125, {{"98.0", "1"}}, {}), 120);
    sync.on_snapshot(snapshot(122, {{"97.0", "1"}}, {{"101.0", "1"}}));
    CHECK(sync.state() == books::SyncState::Live);
    CHECK(sync.book().best_bid()->price == dec("98.0"));
}

TEST_CASE("stale diffs before snapshot are discarded") {
    books::BinanceBookSync sync;
    sync.on_delta(delta(80, 90, {{"500.0", "1"}}, {}), 79);   // u < L: stale
    sync.on_delta(delta(91, 99, {{"400.0", "1"}}, {}), 90);   // u < L: stale
    sync.on_delta(delta(100, 104, {{"100.5", "1"}}, {}), 99); // straddles L=100
    sync.on_snapshot(snapshot(100, {{"99.0", "2"}}, {{"101.0", "3"}}));
    REQUIRE(sync.state() == books::SyncState::Live);
    CHECK(sync.book().best_bid()->price == dec("100.5"));
    CHECK(sync.discarded_stale() == 2);
}

TEST_CASE("snapshot newer than every buffered diff waits for the straddling diff") {
    books::BinanceBookSync sync;
    sync.on_delta(delta(80, 90, {}, {}), 79);
    sync.on_snapshot(snapshot(100, {{"99.0", "2"}}, {{"101.0", "3"}}));
    CHECK(sync.state() == books::SyncState::AwaitingSnapshot);
    CHECK_FALSE(sync.on_delta(delta(95, 101, {{"100.0", "1"}}, {}), 94).request_snapshot);
    CHECK(sync.state() == books::SyncState::Live);
    CHECK(sync.book().best_bid()->price == dec("100.0"));
}

TEST_CASE("first diff that skips past the snapshot is a gap") {
    books::BinanceBookSync sync;
    sync.on_delta(delta(105, 110, {}, {}), 104); // U > L: missing updates
    const auto action = sync.on_snapshot(snapshot(100, {{"99.0", "2"}}, {{"101.0", "3"}}));
    CHECK(action.request_snapshot);
    CHECK(sync.state() == books::SyncState::GapDetected);
}

TEST_CASE("crossed book after apply is a gap") {
    auto sync = live_sync_at(110);
    CHECK(sync.on_delta(delta(111, 112, {{"102.0", "1"}}, {}), 110).request_snapshot);
    CHECK(sync.state() == books::SyncState::GapDetected);
}

TEST_CASE("buffer overflow is bounded and counted") {
    books::BinanceBookSync sync;
    for (std::uint64_t i = 0; i < books::BinanceBookSync::k_max_buffered_diffs + 10; ++i) {
        sync.on_delta(delta(10 * i + 1, 10 * i + 10, {}, {}), 10 * i);
    }
    CHECK(sync.buffered() == books::BinanceBookSync::k_max_buffered_diffs);
    CHECK(sync.buffer_overflows() == 10);
    CHECK(sync.state() == books::SyncState::AwaitingSnapshot);
}

TEST_CASE("replay of captured depth_sequence.jsonl yields uncrossed live book") {
    const auto lines = mc_test::read_lines("binance/depth_sequence.jsonl");
    REQUIRE(lines.size() == 201);
    // Line 1 wraps the REST snapshot as {"snapshot":{...}}; strip the wrapper.
    const std::string prefix = R"({"snapshot":)";
    REQUIRE(lines[0].rfind(prefix, 0) == 0);
    const auto snap_text = lines[0].substr(prefix.size(), lines[0].size() - prefix.size() - 1);

    venues::BinanceAdapter adapter;
    books::BinanceBookSync sync;
    std::size_t applied_after_live = 0;
    bool snapshot_sent             = false;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const auto r = adapter.adapt({venues::StreamTag::BinanceWs, 1, lines[i]});
        REQUIRE(r.error == venues::AdapterError::None);
        const auto &d = std::get<domain::BookDelta>(r.events.at(0));
        sync.on_delta(d, r.binance_prev_final_update_ids.at(0));
        if (sync.state() == books::SyncState::Live) {
            ++applied_after_live;
        }
        // The capture requested the snapshot after 20 diffs; deliver it then.
        if (i == 20 && !snapshot_sent) {
            const auto s = adapter.adapt({venues::StreamTag::BinanceDepthSnapshot, 1, snap_text});
            REQUIRE(s.error == venues::AdapterError::None);
            sync.on_snapshot(std::get<domain::BookSnapshot>(s.events.at(0)));
            snapshot_sent = true;
        }
        REQUIRE(sync.state() != books::SyncState::GapDetected);
    }
    CHECK(sync.state() == books::SyncState::Live);
    CHECK(applied_after_live > 100);
    CHECK_FALSE(sync.book().crossed());
    CHECK(sync.resync_count() == 0);
}
