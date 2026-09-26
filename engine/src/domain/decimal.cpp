#include "market_classifier/domain/decimal.hpp"

#include <algorithm>
#include <limits>

namespace market_classifier::domain {
namespace {

constexpr std::uint64_t absolute_magnitude(std::int64_t value) noexcept {
    if (value >= 0) {
        return static_cast<std::uint64_t>(value);
    }
    return static_cast<std::uint64_t>(-(value + 1)) + 1;
}

constexpr int digit_count(std::uint64_t value) noexcept {
    int count = 1;
    while (value >= 10) {
        value /= 10;
        ++count;
    }
    return count;
}

constexpr std::uint64_t power_of_ten(int exponent) noexcept {
    std::uint64_t result = 1;
    for (int index = 0; index < exponent; ++index) {
        result *= 10;
    }
    return result;
}

constexpr int digit_from_left(std::uint64_t value, int digits, int index) noexcept {
    if (index >= digits) {
        return 0;
    }
    const auto k_divisor = power_of_ten(digits - index - 1);
    return static_cast<int>((value / k_divisor) % 10);
}

constexpr int compare_magnitudes(const Decimal &left, const Decimal &right) noexcept {
    const auto k_left_magnitude  = absolute_magnitude(left.mantissa());
    const auto k_right_magnitude = absolute_magnitude(right.mantissa());
    const int k_left_digits      = digit_count(k_left_magnitude);
    const int k_right_digits     = digit_count(k_right_magnitude);
    const int k_left_exponent    = k_left_digits - static_cast<int>(left.scale());
    const int k_right_exponent   = k_right_digits - static_cast<int>(right.scale());

    if (k_left_exponent < k_right_exponent) {
        return -1;
    }
    if (k_left_exponent > k_right_exponent) {
        return 1;
    }

    const int k_compared_digits = std::max(k_left_digits, k_right_digits);
    for (int index = 0; index < k_compared_digits; ++index) {
        const int k_left_digit  = digit_from_left(k_left_magnitude, k_left_digits, index);
        const int k_right_digit = digit_from_left(k_right_magnitude, k_right_digits, index);
        if (k_left_digit < k_right_digit) {
            return -1;
        }
        if (k_left_digit > k_right_digit) {
            return 1;
        }
    }
    return 0;
}

struct ParsedMagnitude {
    std::uint64_t magnitude = 0;
    std::uint8_t scale      = 0;
    DecimalError error      = DecimalError::None;
};

ParsedMagnitude parse_magnitude(std::string_view text, std::size_t index,
                                std::uint64_t limit) noexcept {
    ParsedMagnitude parsed{};
    bool decimal_point_seen = false;
    bool digit_seen         = false;

    for (; index < text.size(); ++index) {
        const char k_character = text[index];
        if (k_character == '.') {
            if (decimal_point_seen || !digit_seen || index + 1 == text.size()) {
                parsed.error = DecimalError::InvalidSyntax;
                return parsed;
            }
            decimal_point_seen = true;
            continue;
        }
        if (k_character < '0' || k_character > '9') {
            parsed.error = DecimalError::InvalidSyntax;
            return parsed;
        }

        digit_seen         = true;
        const auto k_digit = static_cast<std::uint64_t>(k_character - '0');
        if (parsed.magnitude > (limit - k_digit) / 10) {
            parsed.error = DecimalError::Overflow;
            return parsed;
        }
        parsed.magnitude = parsed.magnitude * 10 + k_digit;

        if (decimal_point_seen) {
            if (parsed.scale == Decimal::k_max_scale) {
                parsed.error = DecimalError::ScaleOutOfRange;
                return parsed;
            }
            ++parsed.scale;
        }
    }
    return parsed;
}

} // namespace

DecimalResult Decimal::parse(std::string_view text) noexcept {
    if (text.empty()) {
        return {{}, DecimalError::Empty};
    }
    if (text.size() > k_max_text_length) {
        return {{}, DecimalError::TooLong};
    }

    std::size_t index = 0;
    bool negative     = false;
    if (text.front() == '+' || text.front() == '-') {
        negative = text.front() == '-';
        index    = 1;
    }
    if (index == text.size()) {
        return {{}, DecimalError::InvalidSyntax};
    }

    constexpr auto k_positive_limit =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    constexpr auto k_negative_limit = k_positive_limit + 1;
    const auto k_limit              = negative ? k_negative_limit : k_positive_limit;
    const auto k_parsed             = parse_magnitude(text, index, k_limit);
    if (k_parsed.error != DecimalError::None) {
        return {{}, k_parsed.error};
    }

    std::int64_t mantissa = 0;
    if (negative && k_parsed.magnitude == k_negative_limit) {
        mantissa = std::numeric_limits<std::int64_t>::min();
    } else {
        mantissa = static_cast<std::int64_t>(k_parsed.magnitude);
        if (negative) {
            mantissa = -mantissa;
        }
    }

    if (mantissa == 0) {
        return {Decimal{0, 0}, DecimalError::None};
    }
    std::uint8_t scale = k_parsed.scale;
    while (scale > 0 && mantissa % 10 == 0) {
        mantissa /= 10;
        --scale;
    }
    return {Decimal{mantissa, scale}, DecimalError::None};
}

std::string Decimal::to_string() const {
    std::int64_t canonical_mantissa = mantissa_;
    std::uint8_t canonical_scale    = scale_;
    if (canonical_mantissa == 0) {
        return "0";
    }
    while (canonical_scale > 0 && canonical_mantissa % 10 == 0) {
        canonical_mantissa /= 10;
        --canonical_scale;
    }

    const bool k_negative = canonical_mantissa < 0;
    std::string digits    = std::to_string(absolute_magnitude(canonical_mantissa));
    std::string result;
    if (k_negative) {
        result.push_back('-');
    }

    if (canonical_scale == 0) {
        result += digits;
        return result;
    }

    const auto k_scale = static_cast<std::size_t>(canonical_scale);
    if (digits.size() <= k_scale) {
        result += "0.";
        result.append(k_scale - digits.size(), '0');
        result += digits;
        return result;
    }

    digits.insert(digits.size() - k_scale, 1, '.');
    result += digits;
    return result;
}

DecimalResult Decimal::rescale_exact(std::uint8_t target_scale) const noexcept {
    if (target_scale > k_max_scale) {
        return {{}, DecimalError::ScaleOutOfRange};
    }
    if (target_scale == scale_) {
        return {*this, DecimalError::None};
    }

    std::int64_t result = mantissa_;
    if (target_scale > scale_) {
        const int k_steps = static_cast<int>(target_scale) - static_cast<int>(scale_);
        for (int index = 0; index < k_steps; ++index) {
            if (result > std::numeric_limits<std::int64_t>::max() / 10 ||
                result < std::numeric_limits<std::int64_t>::min() / 10) {
                return {{}, DecimalError::Overflow};
            }
            result *= 10;
        }
        return {Decimal{result, target_scale}, DecimalError::None};
    }

    const int k_steps = static_cast<int>(scale_) - static_cast<int>(target_scale);
    for (int index = 0; index < k_steps; ++index) {
        if (result % 10 != 0) {
            return {{}, DecimalError::Inexact};
        }
        result /= 10;
    }
    return {Decimal{result, target_scale}, DecimalError::None};
}

std::strong_ordering Decimal::operator<=>(const Decimal &other) const noexcept {
    if (mantissa_ == 0) {
        if (other.mantissa_ == 0) {
            return std::strong_ordering::equal;
        }
        return other.mantissa_ > 0 ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    if (other.mantissa_ == 0) {
        return mantissa_ > 0 ? std::strong_ordering::greater : std::strong_ordering::less;
    }
    if (mantissa_ < 0 && other.mantissa_ >= 0) {
        return std::strong_ordering::less;
    }
    if (mantissa_ >= 0 && other.mantissa_ < 0) {
        return std::strong_ordering::greater;
    }

    const int k_magnitude_order = compare_magnitudes(*this, other);
    if (k_magnitude_order == 0) {
        return std::strong_ordering::equal;
    }
    const bool k_negative = mantissa_ < 0;
    if ((k_magnitude_order < 0) != k_negative) {
        return std::strong_ordering::less;
    }
    return std::strong_ordering::greater;
}

bool Decimal::operator==(const Decimal &other) const noexcept {
    return (*this <=> other) == std::strong_ordering::equal;
}

} // namespace market_classifier::domain
