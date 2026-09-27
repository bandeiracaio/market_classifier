#include "market_classifier/domain/decimal_math.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace market_classifier::domain {
namespace {

constexpr std::int64_t k_max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t k_min = std::numeric_limits<std::int64_t>::min();

bool checked_mul(std::int64_t a, std::int64_t b, std::int64_t &out) noexcept {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    if (a == -1) {
        if (b == k_min)
            return false;
        out = -b;
        return true;
    }
    if (b == -1) {
        if (a == k_min)
            return false;
        out = -a;
        return true;
    }
    if (a > 0 ? (b > 0 ? a > k_max / b : b < k_min / a) : (b > 0 ? a < k_min / b : b < k_max / a))
        return false;
    out = a * b;
    return true;
}

bool checked_add(std::int64_t a, std::int64_t b, std::int64_t &out) noexcept {
    if ((b > 0 && a > k_max - b) || (b < 0 && a < k_min - b))
        return false;
    out = a + b;
    return true;
}

bool pow10(std::uint8_t exponent, std::int64_t &out) noexcept {
    out = 1;
    for (std::uint8_t i = 0; i < exponent; ++i) {
        if (!checked_mul(out, 10, out))
            return false;
    }
    return true;
}

// Brings both operands to the larger scale.
bool align(Decimal a, Decimal b, std::int64_t &ma, std::int64_t &mb, std::uint8_t &scale) noexcept {
    scale           = a.scale() > b.scale() ? a.scale() : b.scale();
    std::int64_t fa = 1;
    std::int64_t fb = 1;
    return pow10(static_cast<std::uint8_t>(scale - a.scale()), fa) &&
           pow10(static_cast<std::uint8_t>(scale - b.scale()), fb) &&
           checked_mul(a.mantissa(), fa, ma) && checked_mul(b.mantissa(), fb, mb);
}

DecimalResult make(std::int64_t mantissa, int scale) noexcept {
    // Drop trailing zeros until the scale fits.
    while (scale > Decimal::k_max_scale && mantissa % 10 == 0) {
        mantissa /= 10;
        --scale;
    }
    if (scale > Decimal::k_max_scale)
        return {{}, DecimalError::Inexact};
    return Decimal::from_parts(mantissa, static_cast<std::uint8_t>(scale));
}

} // namespace

DecimalResult add(Decimal a, Decimal b) noexcept {
    std::int64_t ma{}, mb{}, sum{};
    std::uint8_t scale{};
    if (!align(a, b, ma, mb, scale) || !checked_add(ma, mb, sum))
        return {{}, DecimalError::Overflow};
    return make(sum, scale);
}

DecimalResult sub(Decimal a, Decimal b) noexcept {
    if (b.mantissa() == k_min)
        return {{}, DecimalError::Overflow};
    const auto negated = Decimal::from_parts(-b.mantissa(), b.scale());
    return add(a, negated.value);
}

DecimalResult mul(Decimal a, Decimal b) noexcept {
    std::int64_t product{};
    if (!checked_mul(a.mantissa(), b.mantissa(), product))
        return {{}, DecimalError::Overflow};
    return make(product, a.scale() + b.scale());
}

DecimalResult floor_to(Decimal value, Decimal step) noexcept {
    if (step.mantissa() <= 0)
        return {{}, DecimalError::InvalidSyntax};
    std::int64_t mv{}, ms{};
    std::uint8_t scale{};
    if (!align(value, step, mv, ms, scale))
        return {{}, DecimalError::Overflow};
    std::int64_t q = mv / ms;
    if (mv % ms != 0 && mv < 0)
        --q;
    std::int64_t floored{};
    if (!checked_mul(q, ms, floored))
        return {{}, DecimalError::Overflow};
    return make(floored, scale);
}

double to_double(Decimal value) noexcept {
    return static_cast<double>(value.mantissa()) / std::pow(10.0, value.scale());
}

} // namespace market_classifier::domain
