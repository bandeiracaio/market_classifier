#include "market_classifier/domain/decimal_math.hpp"

#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace market_classifier::domain;

namespace {
Decimal d(const char *text) {
    return Decimal::parse(text).value;
}
} // namespace

TEST_CASE("decimal add/sub align scales exactly") {
    CHECK(add(d("1.5"), d("0.25")).value == d("1.75"));
    CHECK(sub(d("1.0"), d("1.5")).value == d("-0.5"));
    CHECK(add(d("0"), d("-3")).value == d("-3"));
}

TEST_CASE("decimal mul is exact and detects overflow") {
    CHECK(mul(d("84499.5"), d("0.001")).value == d("84.4995"));
    CHECK(mul(d("-2"), d("3.5")).value == d("-7"));
    const auto big = Decimal::from_parts(std::numeric_limits<std::int64_t>::max(), 0).value;
    CHECK(mul(big, d("2")).error == DecimalError::Overflow);
    CHECK(add(big, d("1")).error == DecimalError::Overflow);
    // Scale 12 + 12 = 24 with no trailing zeros cannot be represented exactly.
    CHECK(mul(d("0.000000000001"), d("0.000000000003")).error == DecimalError::Inexact);
}

TEST_CASE("decimal floor_to floors toward negative infinity") {
    CHECK(floor_to(d("63002.4"), d("5")).value == d("63000"));
    CHECK(floor_to(d("63004.9"), d("5")).value == d("63000"));
    CHECK(floor_to(d("63005"), d("5")).value == d("63005"));
    CHECK(floor_to(d("-1.5"), d("1")).value == d("-2"));
    CHECK(floor_to(d("0.37"), d("0.25")).value == d("0.25"));
    CHECK(floor_to(d("1"), d("0")).error != DecimalError::None);
}
