#pragma once

#include "market_classifier/runtime/clock.hpp"
#include "market_classifier/runtime/read_model.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace market_classifier::runtime {

inline constexpr std::size_t k_max_replay_entries = 4096;

struct ReplayEntry {
    std::int64_t offset_ms = 0;
    domain::NormalizedEvent event;
    std::size_t encoded_bytes = 0;
};

enum class ReplayError : std::uint8_t {
    None,
    Empty,
    TooManyEntries,
    InvalidTimeline,
    ClockOverflow,
    IngressRejected,
};

struct ReplayResult {
    std::size_t submitted_entries = 0;
    ReplayError error             = ReplayError::None;

    [[nodiscard]] explicit operator bool() const noexcept { return error == ReplayError::None; }
    [[nodiscard]] bool operator==(const ReplayResult &) const noexcept = default;
};

class ReplayHarness {
  public:
    ReplayHarness(FakeClock &clock, BoundedIngress &ingress, DummyReadModel &model) noexcept
        : clock_(&clock), ingress_(&ingress), model_(&model) {}

    [[nodiscard]] ReplayResult run(std::span<const ReplayEntry> entries);

  private:
    // Borrowed; the harness never outlives the objects it drives.
    FakeClock *clock_;
    BoundedIngress *ingress_;
    DummyReadModel *model_;
};

} // namespace market_classifier::runtime
