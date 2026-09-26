#include "market_classifier/bridge/protocol.hpp"
#include "market_classifier/runtime/clock.hpp"
#include "market_classifier/runtime/ingress.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace domain  = market_classifier::domain;
namespace runtime = market_classifier::runtime;
namespace bridge  = market_classifier::bridge;

namespace {

domain::NormalizedEvent event(std::uint64_t sequence) {
    const auto instrument = domain::InstrumentId::create(domain::Venue::Hyperliquid, "BTC").value;
    const auto meta       = domain::EventMeta::create(
                          instrument, domain::SourceTimeMs{1000}, domain::ReceiveTimeMs{1001},
                          domain::LocalSequence{sequence}, domain::DataQuality::Live)
                          .value;
    return domain::FeedStatus{meta, domain::FeedLifecycle::Open, 0,
                              0,    domain::GapStatus::None,     domain::FeedError::None};
}

runtime::IngressBatch batch(std::uint64_t sequence,
                            std::size_t bytes = runtime::k_max_batch_bytes) {
    return {{event(sequence)}, bytes};
}

std::uint64_t sequence_of(const runtime::IngressBatch &value) {
    return std::visit([](const auto &item) { return item.meta.local_sequence().value; },
                      value.events.front());
}

} // namespace

TEST_CASE("fake clock advances both clocks deterministically", "[runtime][clock]") {
    runtime::FakeClock clock{1'000, 50};
    CHECK(clock.wall_time_ms() == 1'000);
    CHECK(clock.monotonic_time_ms() == 50);
    REQUIRE(clock.advance_ms(25));
    CHECK(clock.wall_time_ms() == 1'025);
    CHECK(clock.monotonic_time_ms() == 75);
    CHECK_FALSE(clock.advance_ms(-1));

    runtime::FakeClock maximum{std::numeric_limits<std::int64_t>::max(), 0};
    CHECK_FALSE(maximum.advance_ms(1));
}

TEST_CASE("ingress limits are explicit and bounded", "[runtime][ingress]") {
    CHECK(runtime::k_max_events_per_batch == 256);
    CHECK(runtime::k_max_batch_bytes == 64 * 1024);
    CHECK(runtime::k_max_queued_batches == 8);
    CHECK(runtime::k_max_queued_bytes == 512 * 1024);
    CHECK(runtime::k_max_levels_per_event == 1024);
    CHECK(runtime::k_max_dynamic_text_bytes == 128);
    CHECK(runtime::k_max_events_per_batch == bridge::k_max_events_per_batch);
    CHECK(runtime::k_max_batch_bytes == bridge::k_max_message_bytes);
}

TEST_CASE("ingress accepts exact capacity and consumes in stable order", "[runtime][ingress]") {
    runtime::BoundedIngress ingress;
    for (std::uint64_t sequence = 1; sequence <= runtime::k_max_queued_batches; ++sequence) {
        CHECK(ingress.submit(batch(sequence)) == runtime::SubmitResult::Accepted);
    }

    const auto full = ingress.status();
    CHECK(full.queued_batches == runtime::k_max_queued_batches);
    CHECK(full.queued_bytes == runtime::k_max_queued_bytes);
    CHECK(full.counters.accepted_batches == runtime::k_max_queued_batches);
    CHECK(full.counters.dropped_batches == 0);
    CHECK(full.quality == domain::DataQuality::Live);

    for (std::uint64_t sequence = 1; sequence <= runtime::k_max_queued_batches; ++sequence) {
        const auto next = ingress.consume_next();
        REQUIRE(next);
        CHECK(sequence_of(*next) == sequence);
    }
    CHECK_FALSE(ingress.consume_next());
    CHECK(ingress.status().queued_bytes == 0);
}

TEST_CASE("ingress overflow drops oldest and latches a visible gap", "[runtime][ingress]") {
    runtime::BoundedIngress ingress;
    for (std::uint64_t sequence = 1; sequence <= runtime::k_max_queued_batches; ++sequence) {
        REQUIRE(ingress.submit(batch(sequence)) == runtime::SubmitResult::Accepted);
    }
    CHECK(ingress.submit(batch(9)) == runtime::SubmitResult::AcceptedWithDrop);

    const auto pressure = ingress.status();
    CHECK(pressure.queued_batches == runtime::k_max_queued_batches);
    CHECK(pressure.counters.accepted_batches == 9);
    CHECK(pressure.counters.accepted_events == 9);
    CHECK(pressure.counters.dropped_batches == 1);
    CHECK(pressure.counters.dropped_events == 1);
    CHECK(pressure.quality == domain::DataQuality::GapDetected);
    const auto first_retained = ingress.consume_next();
    REQUIRE(first_retained);
    CHECK(sequence_of(*first_retained) == 2);

    CHECK_FALSE(ingress.transition_quality(domain::DataQuality::Live));
    CHECK(ingress.transition_quality(domain::DataQuality::Partial));
    CHECK(ingress.transition_quality(domain::DataQuality::Live));
    CHECK(ingress.status().quality == domain::DataQuality::Live);
}

TEST_CASE("ingress rejects invalid batches without consuming capacity", "[runtime][ingress]") {
    runtime::BoundedIngress ingress;
    CHECK(ingress.submit({{}, 1}) == runtime::SubmitResult::RejectedEmpty);

    std::vector<domain::NormalizedEvent> too_many;
    too_many.reserve(runtime::k_max_events_per_batch + 1);
    for (std::size_t index = 0; index <= runtime::k_max_events_per_batch; ++index) {
        too_many.push_back(event(index + 1));
    }
    CHECK(ingress.submit({std::move(too_many), 1}) == runtime::SubmitResult::RejectedEventLimit);
    CHECK(ingress.submit(batch(1, runtime::k_max_batch_bytes + 1)) ==
          runtime::SubmitResult::RejectedByteLimit);

    const auto meta = std::get<domain::FeedStatus>(event(2)).meta;
    const domain::Trade oversized_text{meta,
                                       std::string(runtime::k_max_dynamic_text_bytes + 1, 'x'),
                                       domain::AggressorSide::Buy,
                                       domain::Decimal{},
                                       domain::Decimal{},
                                       domain::Decimal{}};
    CHECK(ingress.submit({{oversized_text}, 1}) == runtime::SubmitResult::RejectedOwnedSize);

    const auto status = ingress.status();
    CHECK(status.queued_batches == 0);
    CHECK(status.counters.rejected_batches == 4);
    CHECK(status.counters.rejected_events == runtime::k_max_events_per_batch + 3);
    CHECK(status.counters.accepted_batches == 0);
}

TEST_CASE("submission and render-owned consumption are distinct operations", "[runtime][ingress]") {
    runtime::BoundedIngress ingress;
    auto submitted = batch(7, 128);
    REQUIRE(ingress.submit(std::move(submitted)) == runtime::SubmitResult::Accepted);
    CHECK(ingress.status().queued_events == 1);
    const auto consumed = ingress.consume_next();
    REQUIRE(consumed);
    CHECK(sequence_of(*consumed) == 7);
    CHECK(ingress.status().queued_events == 0);
}

TEST_CASE("overflow never weakens an unsupported quality state", "[runtime][ingress]") {
    runtime::BoundedIngress ingress;
    REQUIRE(ingress.transition_quality(domain::DataQuality::Unsupported));
    for (std::uint64_t sequence = 1; sequence <= runtime::k_max_queued_batches + 1; ++sequence) {
        const auto expected = sequence <= runtime::k_max_queued_batches
                                  ? runtime::SubmitResult::Accepted
                                  : runtime::SubmitResult::AcceptedWithDrop;
        REQUIRE(ingress.submit(batch(sequence)) == expected);
    }
    CHECK(ingress.status().quality == domain::DataQuality::Unsupported);
    CHECK(ingress.status().counters.dropped_batches == 1);
}
