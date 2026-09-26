#include <catch2/catch_test_macros.hpp>

#include "market_classifier/version.hpp"

TEST_CASE("version constants are self-consistent", "[version]") {
    using namespace market_classifier;

    SECTION("major.minor.patch values match string") {
        REQUIRE(k_version.major == 0);
        REQUIRE(k_version.minor == 1);
        REQUIRE(k_version.patch == 0);
        REQUIRE(k_version_string == "0.1.0");
    }

    SECTION("version string is non-empty") {
        REQUIRE(!k_version_string.empty());
    }

    SECTION("version string contains expected separators") {
        // Dot-separated version has exactly two dots
        int dot_count = 0;
        for (char c : k_version_string) {
            if (c == '.') { ++dot_count; }
        }
        REQUIRE(dot_count == 2);
    }
}
