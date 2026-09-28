#pragma once

#include <cstdint>

namespace market_classifier::runtime {

class Clock {
  public:
    Clock()                                                               = default;
    Clock(const Clock &)                                                  = default;
    Clock &operator=(const Clock &)                                       = default;
    Clock(Clock &&)                                                       = default;
    Clock &operator=(Clock &&)                                            = default;
    virtual ~Clock()                                                      = default;
    [[nodiscard]] virtual std::int64_t wall_time_ms() const noexcept      = 0;
    [[nodiscard]] virtual std::int64_t monotonic_time_ms() const noexcept = 0;
};

class SystemClock final : public Clock {
  public:
    [[nodiscard]] std::int64_t wall_time_ms() const noexcept override;
    [[nodiscard]] std::int64_t monotonic_time_ms() const noexcept override;
};

class FakeClock final : public Clock {
  public:
    constexpr FakeClock(std::int64_t wall_time_ms = 0, std::int64_t monotonic_time_ms = 0) noexcept
        : wall_time_ms_(wall_time_ms), monotonic_time_ms_(monotonic_time_ms) {}

    [[nodiscard]] constexpr std::int64_t wall_time_ms() const noexcept override {
        return wall_time_ms_;
    }
    [[nodiscard]] constexpr std::int64_t monotonic_time_ms() const noexcept override {
        return monotonic_time_ms_;
    }
    [[nodiscard]] bool advance_ms(std::int64_t delta_ms) noexcept;
    void set_wall_time_ms(std::int64_t value) noexcept { wall_time_ms_ = value; }

  private:
    std::int64_t wall_time_ms_      = 0;
    std::int64_t monotonic_time_ms_ = 0;
};

} // namespace market_classifier::runtime
