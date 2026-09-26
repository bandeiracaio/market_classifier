#include "market_classifier/runtime/read_model.hpp"
#include "market_classifier/runtime/replay.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace domain  = market_classifier::domain;
namespace runtime = market_classifier::runtime;

namespace {

domain::NormalizedEvent status_event(std::uint64_t sequence, std::int64_t source_time,
                                     std::int64_t receive_time, domain::DataQuality quality) {
    const auto instrument =
        domain::InstrumentId::create(domain::Venue::BinanceUsdM, "BTCUSDT").value;
    const auto meta = domain::EventMeta::create(instrument, domain::SourceTimeMs{source_time},
                                                domain::ReceiveTimeMs{receive_time},
                                                domain::LocalSequence{sequence}, quality)
                          .value;
    return domain::FeedStatus{meta, domain::FeedLifecycle::Open, 0,
                              0,    domain::GapStatus::None,     domain::FeedError::None};
}

std::vector<runtime::ReplayEntry> load_reference_fixture() {
    std::ifstream input(std::string{MC_SOURCE_DIR} + "/fixtures/m1/reference-replay.csv");
    REQUIRE(input);
    std::string line;
    REQUIRE(std::getline(input, line));
    std::vector<runtime::ReplayEntry> entries;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string offset, sequence, source, receive, quality;
        REQUIRE(std::getline(row, offset, ','));
        REQUIRE(std::getline(row, sequence, ','));
        REQUIRE(std::getline(row, source, ','));
        REQUIRE(std::getline(row, receive, ','));
        REQUIRE(std::getline(row, quality, ','));
        const auto data_quality =
            quality == "Live" ? domain::DataQuality::Live : domain::DataQuality::Delayed;
        entries.push_back({std::stoll(offset),
                           status_event(std::stoull(sequence), std::stoll(source),
                                        std::stoll(receive), data_quality),
                           64});
    }
    return entries;
}

struct ReplayOutcome {
    runtime::ReplayResult result;
    runtime::DummyReadModel model;
    std::int64_t wall_time;
    std::int64_t monotonic_time;
    bool operator==(const ReplayOutcome &) const = default;
};

ReplayOutcome replay_once(const std::vector<runtime::ReplayEntry> &entries) {
    runtime::FakeClock clock{1'000, 200};
    runtime::BoundedIngress ingress;
    runtime::DummyReadModel model;
    runtime::ReplayHarness harness{clock, ingress, model};
    const auto result = harness.run(entries);
    return {result, model, clock.wall_time_ms(), clock.monotonic_time_ms()};
}

} // namespace

TEST_CASE("replay limits and errors are explicit", "[runtime][replay]") {
    CHECK(runtime::k_max_replay_entries == 4096);
    CHECK(runtime::ReplayError::None != runtime::ReplayError::InvalidTimeline);
}

TEST_CASE("reference replay is deterministic and advances without sleeping", "[runtime][replay]") {
    const auto entries = load_reference_fixture();
    REQUIRE(entries.size() == 4);

    const auto first  = replay_once(entries);
    const auto second = replay_once(entries);
    CHECK(first == second);
    REQUIRE(first.result);
    CHECK(first.result.submitted_entries == 4);
    CHECK(first.wall_time == 1'025);
    CHECK(first.monotonic_time == 225);
    CHECK(first.model.total_events == 4);
    CHECK(first.model.event_counts[10] == 4);
    CHECK(first.model.last_local_sequence == 4);
    CHECK(first.model.last_source_time_ms == 1'700'000'000'025);
    CHECK(first.model.last_receive_time_ms == 1'700'000'000'029);
    CHECK(first.model.quality == domain::DataQuality::Live);
    CHECK(first.model.ingress_counters.accepted_batches == 4);
    CHECK(first.model.ingress_counters.dropped_batches == 0);
}

TEST_CASE("replay rejects an unordered timeline before changing state", "[runtime][replay]") {
    auto entries = load_reference_fixture();
    std::swap(entries[1], entries[3]);
    runtime::FakeClock clock{10, 20};
    runtime::BoundedIngress ingress;
    runtime::DummyReadModel model;
    runtime::ReplayHarness harness{clock, ingress, model};

    const auto result = harness.run(entries);
    CHECK(result.error == runtime::ReplayError::InvalidTimeline);
    CHECK(result.submitted_entries == 0);
    CHECK(clock.wall_time_ms() == 10);
    CHECK(clock.monotonic_time_ms() == 20);
    CHECK_FALSE(model.has_data);
    CHECK(ingress.status().counters.accepted_batches == 0);
}

TEST_CASE("read model exposes ingress loss after bounded overflow", "[runtime][read-model]") {
    runtime::BoundedIngress ingress;
    for (std::uint64_t sequence = 1; sequence <= runtime::k_max_queued_batches + 1; ++sequence) {
        const auto result = ingress.submit(
            {{status_event(sequence, 1000 + sequence, 1001 + sequence, domain::DataQuality::Live)},
             64});
        REQUIRE(result == (sequence <= runtime::k_max_queued_batches
                               ? runtime::SubmitResult::Accepted
                               : runtime::SubmitResult::AcceptedWithDrop));
    }

    runtime::DummyReadModel model;
    runtime::drain_ingress(ingress, model);
    CHECK(model.total_events == runtime::k_max_queued_batches);
    CHECK(model.last_local_sequence == 9);
    CHECK(model.quality == domain::DataQuality::GapDetected);
    CHECK(model.ingress_counters.accepted_events == 9);
    CHECK(model.ingress_counters.dropped_events == 1);
}

TEST_CASE("read model preserves terminal unsupported quality", "[runtime][read-model]") {
    runtime::BoundedIngress ingress;
    REQUIRE(ingress.submit({{status_event(1, 1000, 1001, domain::DataQuality::Unsupported),
                             status_event(2, 1002, 1003, domain::DataQuality::Live)},
                            128}) == runtime::SubmitResult::Accepted);
    runtime::DummyReadModel model;
    runtime::drain_ingress(ingress, model);
    CHECK(model.total_events == 2);
    CHECK(model.last_local_sequence == 2);
    CHECK(model.quality == domain::DataQuality::Unsupported);
}
