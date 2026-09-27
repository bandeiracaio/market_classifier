#include "market_classifier/json/json.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace market_classifier;

TEST_CASE("json parses objects and exposes checked accessors") {
    const auto doc = json::Document::parse(
        R"({"s":"BTCUSDT","n":42,"b":true,"a":[1,"x"],"p":"63000.10"})", 1024);
    REQUIRE(doc);
    const auto root = doc->root();
    CHECK(root.size() == 5);
    CHECK(root.get("s")->string() == "BTCUSDT");
    CHECK(root.get("n")->int64() == 42);
    CHECK(root.get("b")->boolean() == true);
    CHECK(root.get("a")->size() == 2);
    CHECK(root.get("a")->at(1)->string() == "x");
    CHECK_FALSE(root.get("a")->at(2));
    CHECK_FALSE(root.get("missing"));
    CHECK_FALSE(root.get("s")->int64());
    CHECK_FALSE(root.get("n")->string());
}

TEST_CASE("json decimal reads exact text from strings and numbers") {
    const auto doc = json::Document::parse(R"({"p":"63000.10","n":0.25,"e":"1e5","ne":1e5})", 1024);
    REQUIRE(doc);
    const auto p = doc->root().get("p")->decimal();
    REQUIRE(p);
    // ADR-0003: parsing removes fractional trailing zeros.
    CHECK(p->mantissa() == 630001);
    CHECK(p->scale() == 1);
    CHECK(*p == domain::Decimal::parse("63000.10").value);
    const auto n = doc->root().get("n")->decimal();
    REQUIRE(n);
    CHECK(n->mantissa() == 25);
    CHECK(n->scale() == 2);
    CHECK_FALSE(doc->root().get("e")->decimal());
    CHECK_FALSE(doc->root().get("ne")->decimal());
}

TEST_CASE("json int64 rejects fractional and out-of-range numbers") {
    const auto doc =
        json::Document::parse(R"({"f":1.5,"big":99999999999999999999,"neg":-7})", 1024);
    REQUIRE(doc);
    CHECK_FALSE(doc->root().get("f")->int64());
    CHECK_FALSE(doc->root().get("big")->int64());
    CHECK(doc->root().get("neg")->int64() == -7);
}

TEST_CASE("json rejects oversize, truncated and too-deep input") {
    CHECK_FALSE(json::Document::parse(R"({"a":1})", 3));
    CHECK_FALSE(json::Document::parse(R"({"a":)", 1024));
    CHECK_FALSE(json::Document::parse("", 1024));
    std::string deep(json::k_max_depth + 1, '[');
    deep += std::string(json::k_max_depth + 1, ']');
    CHECK_FALSE(json::Document::parse(deep, 4096));
    std::string ok(json::k_max_depth, '[');
    ok += std::string(json::k_max_depth, ']');
    CHECK(json::Document::parse(ok, 4096));
    // Brackets inside strings do not count toward depth.
    std::string quoted = "[\"" + std::string(200, '[') + "\"]";
    CHECK(json::Document::parse(quoted, 4096));
}
