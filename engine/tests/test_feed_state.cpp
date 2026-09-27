#include "market_classifier/runtime/feed_state.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace market_classifier;

TEST_CASE("feed starts connecting and goes live on open") {
    runtime::FeedState f({}, 1);
    CHECK(f.phase() == runtime::FeedPhase::Connecting);
    CHECK(f.quality() == domain::DataQuality::Reconnecting);
    f.on_open(10);
    CHECK(f.phase() == runtime::FeedPhase::Live);
    CHECK(f.quality() == domain::DataQuality::Live);
}

TEST_CASE("silence moves Live to Stale with age") {
    runtime::FeedState f({.stale_after_ms = 5000}, 1);
    f.on_open(0);
    f.on_message(100);
    f.tick(5100);
    CHECK(f.phase() == runtime::FeedPhase::Live); // exactly at threshold is not stale
    f.tick(5101);
    CHECK(f.phase() == runtime::FeedPhase::Stale);
    CHECK(f.quality() == domain::DataQuality::Stale);
    CHECK(f.age_ms(5101) == 5001);
    f.on_message(5200);
    CHECK(f.phase() == runtime::FeedPhase::Live);
    CHECK(f.age_ms(5200) == 0);
}

TEST_CASE("close schedules reconnect after backoff") {
    runtime::FeedState f({.backoff_base_ms = 500, .backoff_cap_ms = 30000}, 7);
    f.on_open(0);
    f.on_close(1000);
    CHECK(f.phase() == runtime::FeedPhase::Reconnecting);
    CHECK(f.quality() == domain::DataQuality::Reconnecting);
    CHECK(f.attempt() == 1);
    const auto at = f.next_retry_ms();
    CHECK(at >= 1000 + 500);  // base * 2^1 * 0.5
    CHECK(at <= 1000 + 1000); // base * 2^1
    CHECK_FALSE(f.should_reconnect(at - 1));
    CHECK(f.should_reconnect(at));
    f.on_reconnect_started(at);
    CHECK(f.phase() == runtime::FeedPhase::Connecting);
    CHECK_FALSE(f.should_reconnect(at)); // only once per scheduled attempt
    f.on_open(at + 10);
    f.on_message(at + 20);
    CHECK(f.attempt() == 0); // data flowing again resets backoff
}

TEST_CASE("backoff grows, caps, and is deterministic for a seed") {
    const runtime::FeedConfig cfg{
        .backoff_base_ms = 500, .backoff_cap_ms = 30000, .max_attempts_before_failed = 100};
    runtime::FeedState a(cfg, 42);
    runtime::FeedState b(cfg, 42);
    runtime::FeedState c(cfg, 43);
    std::int64_t now      = 0;
    bool any_difference   = false;
    std::int64_t previous = 0;
    for (int i = 0; i < 12; ++i) {
        a.on_close(now);
        b.on_close(now);
        c.on_close(now);
        const auto da = a.next_retry_ms() - now;
        CHECK(da == b.next_retry_ms() - now);
        any_difference = any_difference || da != c.next_retry_ms() - now;
        CHECK(da <= cfg.backoff_cap_ms);
        CHECK(da >= 250); // never below base * 0.5
        if (i < 4) {
            CHECK(da >= previous / 2); // grows roughly geometrically before the cap
        }
        previous = da;
        now      = a.next_retry_ms();
        a.on_reconnect_started(now);
        b.on_reconnect_started(now);
        c.on_reconnect_started(now);
    }
    CHECK(any_difference);
}

TEST_CASE("too many attempts fail the feed") {
    runtime::FeedState f({.max_attempts_before_failed = 3}, 1);
    std::int64_t now = 0;
    for (int i = 0; i < 3; ++i) {
        f.on_close(now);
        CHECK(f.phase() == runtime::FeedPhase::Reconnecting);
        now = f.next_retry_ms();
        f.on_reconnect_started(now);
    }
    f.on_close(now);
    CHECK(f.phase() == runtime::FeedPhase::Failed);
    CHECK(f.quality() == domain::DataQuality::Failed);
    CHECK_FALSE(f.should_reconnect(now + 1'000'000));
    f.retry(now); // user-initiated retry from Failed: reconnect is due immediately
    CHECK(f.phase() == runtime::FeedPhase::Reconnecting);
    CHECK(f.should_reconnect(now));
    CHECK(f.attempt() == 0);
}

TEST_CASE("metadata failure fails the feed until retried") {
    runtime::FeedState f({}, 1);
    f.fail();
    CHECK(f.phase() == runtime::FeedPhase::Failed);
    f.on_open(5);
    f.on_message(6);
    CHECK(f.phase() == runtime::FeedPhase::Failed);
    CHECK_FALSE(f.should_reconnect(100));
    f.retry(100);
    CHECK(f.should_reconnect(100));
}
