#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace market_classifier::domain {

enum class DecimalError : std::uint8_t {
    None,
    Empty,
    TooLong,
    InvalidSyntax,
    ScaleOutOfRange,
    Overflow,
    Inexact,
};

class Decimal;

struct DecimalResult;

class Decimal {
  public:
    static constexpr std::uint8_t k_max_scale      = 18;
    static constexpr std::size_t k_max_text_length = 64;

    constexpr Decimal() noexcept = default;

    [[nodiscard]] static DecimalResult parse(std::string_view text) noexcept;
    [[nodiscard]] static constexpr DecimalResult from_parts(std::int64_t mantissa,
                                                            std::uint8_t scale) noexcept;

    [[nodiscard]] constexpr std::int64_t mantissa() const noexcept { return mantissa_; }
    [[nodiscard]] constexpr std::uint8_t scale() const noexcept { return scale_; }

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] DecimalResult rescale_exact(std::uint8_t target_scale) const noexcept;

    [[nodiscard]] std::strong_ordering operator<=>(const Decimal &other) const noexcept;
    [[nodiscard]] bool operator==(const Decimal &other) const noexcept;

  private:
    constexpr Decimal(std::int64_t mantissa, std::uint8_t scale) noexcept
        : mantissa_(mantissa), scale_(scale) {}

    std::int64_t mantissa_ = 0;
    std::uint8_t scale_    = 0;
};

struct DecimalResult {
    constexpr DecimalResult(Decimal result_value      = {},
                            DecimalError result_error = DecimalError::None) noexcept
        : value(result_value), error(result_error) {}

    Decimal value;
    DecimalError error = DecimalError::None;

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return error == DecimalError::None;
    }
};

constexpr DecimalResult Decimal::from_parts(std::int64_t mantissa, std::uint8_t scale) noexcept {
    if (scale > k_max_scale) {
        return {{}, DecimalError::ScaleOutOfRange};
    }
    return {Decimal{mantissa, scale}, DecimalError::None};
}

} // namespace market_classifier::domain
