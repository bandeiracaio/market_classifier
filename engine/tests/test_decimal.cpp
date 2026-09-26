#include "market_classifier/domain/decimal.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <compare>
#include <cstdint>
#include <limits>
#include <string_view>

namespace domain = market_classifier::domain;

TEST_CASE("decimal parsing produces exact canonical values", "[decimal]") {
    struct Case {
        std::string_view input;
        std::int64_t mantissa;
        std::uint8_t scale;
        std::string_view canonical;
    };

    constexpr std::array cases{
        Case{"0", 0, 0, "0"},
        Case{"-0.000", 0, 0, "0"},
        Case{"+00123.4500", 12345, 2, "123.45"},
        Case{"0.000000000000000001", 1, 18, "0.000000000000000001"},
        Case{"9223372036854775807", std::numeric_limits<std::int64_t>::max(), 0,
             "9223372036854775807"},
        Case{"-9223372036854775808", std::numeric_limits<std::int64_t>::min(), 0,
             "-9223372036854775808"},
    };

    for (const auto &test_case : cases) {
        const auto result = domain::Decimal::parse(test_case.input);
        REQUIRE(result);
        CHECK(result.value.mantissa() == test_case.mantissa);
        CHECK(result.value.scale() == test_case.scale);
        CHECK(result.value.to_string() == test_case.canonical);
    }
}

TEST_CASE("decimal parsing rejects untrusted invalid input", "[decimal]") {
    using enum domain::DecimalError;

    struct Case {
        std::string_view input;
        domain::DecimalError error;
    };

    constexpr std::array cases{
        Case{"", Empty},
        Case{"+", InvalidSyntax},
        Case{".1", InvalidSyntax},
        Case{"1.", InvalidSyntax},
        Case{" 1", InvalidSyntax},
        Case{"1 ", InvalidSyntax},
        Case{"1e3", InvalidSyntax},
        Case{"1_000", InvalidSyntax},
        Case{"1.2.3", InvalidSyntax},
        Case{"0.0000000000000000001", ScaleOutOfRange},
        Case{"9223372036854775808", Overflow},
        Case{"-9223372036854775809", Overflow},
    };

    for (const auto &test_case : cases) {
        const auto result = domain::Decimal::parse(test_case.input);
        CHECK_FALSE(result);
        CHECK(result.error == test_case.error);
    }

    const std::string too_long(domain::Decimal::k_max_text_length + 1, '0');
    const auto result = domain::Decimal::parse(too_long);
    REQUIRE_FALSE(result);
    CHECK(result.error == TooLong);
}

TEST_CASE("decimal comparison is numeric across scales without overflow", "[decimal]") {
    const auto one               = domain::Decimal::parse("1").value;
    const auto one_scaled        = domain::Decimal::parse("1.000000000000000000").value;
    const auto below_one         = domain::Decimal::parse("0.999999999999999999").value;
    const auto negative          = domain::Decimal::parse("-1000000000000000000").value;
    const auto negative_fraction = domain::Decimal::parse("-0.000000000000000001").value;

    CHECK(one == one_scaled);
    CHECK((domain::Decimal{} <=> below_one) == std::strong_ordering::less);
    CHECK((below_one <=> domain::Decimal{}) == std::strong_ordering::greater);
    CHECK((domain::Decimal{} <=> negative_fraction) == std::strong_ordering::greater);
    CHECK((negative_fraction <=> domain::Decimal{}) == std::strong_ordering::less);
    CHECK((below_one <=> one) == std::strong_ordering::less);
    CHECK((negative <=> negative_fraction) == std::strong_ordering::less);

    const auto maximum = domain::Decimal::from_parts(std::numeric_limits<std::int64_t>::max(), 0);
    const auto tiny    = domain::Decimal::from_parts(1, domain::Decimal::k_max_scale);
    REQUIRE(maximum);
    REQUIRE(tiny);
    CHECK((maximum.value <=> tiny.value) == std::strong_ordering::greater);
}

TEST_CASE("decimal exact rescaling reports inexact and overflow operations", "[decimal]") {
    using enum domain::DecimalError;

    const auto value = domain::Decimal::parse("12.34").value;

    const auto expanded = value.rescale_exact(4);
    REQUIRE(expanded);
    CHECK(expanded.value.mantissa() == 123400);
    CHECK(expanded.value.scale() == 4);
    CHECK(expanded.value.to_string() == "12.34");

    const auto reduced = expanded.value.rescale_exact(2);
    REQUIRE(reduced);
    CHECK(reduced.value == value);

    const auto inexact = value.rescale_exact(1);
    REQUIRE_FALSE(inexact);
    CHECK(inexact.error == Inexact);

    const auto maximum = domain::Decimal::from_parts(std::numeric_limits<std::int64_t>::max(), 0);
    REQUIRE(maximum);
    const auto overflow = maximum.value.rescale_exact(1);
    REQUIRE_FALSE(overflow);
    CHECK(overflow.error == Overflow);

    const auto invalid_scale = value.rescale_exact(domain::Decimal::k_max_scale + 1);
    REQUIRE_FALSE(invalid_scale);
    CHECK(invalid_scale.error == ScaleOutOfRange);
}

TEST_CASE("decimal generated round trips are deterministic", "[decimal]") {
    constexpr std::array<std::int64_t, 9> mantissas{
        std::numeric_limits<std::int64_t>::min(), -1000001, -10, -1, 0, 1, 10, 1000001,
        std::numeric_limits<std::int64_t>::max()};
    constexpr std::array<std::uint8_t, 5> scales{0, 1, 2, 9, 18};

    for (const auto mantissa : mantissas) {
        for (const auto scale : scales) {
            const auto original = domain::Decimal::from_parts(mantissa, scale);
            REQUIRE(original);
            const auto reparsed = domain::Decimal::parse(original.value.to_string());
            REQUIRE(reparsed);
            CHECK(reparsed.value == original.value);
        }
    }
}
