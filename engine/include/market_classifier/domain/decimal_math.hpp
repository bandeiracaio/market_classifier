#pragma once

#include "market_classifier/domain/decimal.hpp"

namespace market_classifier::domain {

// Exact, overflow-checked arithmetic over Decimal (ADR-0003). Results are never
// rounded: if the exact result cannot be represented (mantissa overflow or scale
// above k_max_scale after removing trailing zeros) the error is Overflow or
// Inexact and the value is unspecified.
[[nodiscard]] DecimalResult add(Decimal a, Decimal b) noexcept;
[[nodiscard]] DecimalResult sub(Decimal a, Decimal b) noexcept;
[[nodiscard]] DecimalResult mul(Decimal a, Decimal b) noexcept;
// Floors `value` to a multiple of the positive `step` (toward negative infinity).
[[nodiscard]] DecimalResult floor_to(Decimal value, Decimal step) noexcept;
// Presentation-only conversion (plots, colors). Never feed back into domain math.
[[nodiscard]] double to_double(Decimal value) noexcept;

} // namespace market_classifier::domain
