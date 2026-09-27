#include "market_classifier/runtime/ring.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace market_classifier;

TEST_CASE("ring overwrites oldest and counts") {
    runtime::Ring<int, 3> ring;
    CHECK(ring.size() == 0);
    CHECK(ring.empty());
    for (int i = 0; i < 5; ++i) {
        ring.push(i);
    }
    REQUIRE(ring.size() == 3);
    CHECK(ring[0] == 2);
    CHECK(ring[1] == 3);
    CHECK(ring[2] == 4);
    CHECK(ring.back() == 4);
    CHECK(ring.overwritten() == 2);
    ring.back() = 40;
    CHECK(ring[2] == 40);
    ring.clear();
    CHECK(ring.size() == 0);
    CHECK(ring.overwritten() == 2); // lifetime counter survives clear
    ring.push(7);
    CHECK(ring[0] == 7);
}

TEST_CASE("ring capacity is fixed at compile time") {
    STATIC_REQUIRE(runtime::Ring<int, 5>::capacity() == 5);
}
