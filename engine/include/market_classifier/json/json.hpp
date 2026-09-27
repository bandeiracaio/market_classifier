#pragma once

#include "market_classifier/domain/decimal.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

// Opaque parser types (ADR-0005: yyjson). Only json.cpp includes yyjson.h. The names are
// yyjson's own C types, so project naming rules do not apply.
struct yyjson_doc; // NOLINT(readability-identifier-naming)
struct yyjson_val; // NOLINT(readability-identifier-naming)

namespace market_classifier::json {

inline constexpr std::size_t k_max_depth = 64;

// Read-only view into a Document; must not outlive it. Every accessor is
// type-checked and returns nullopt on mismatch, never throws.
class Value {
  public:
    explicit Value(yyjson_val *value) noexcept : value_(value) {}

    [[nodiscard]] std::optional<Value> get(std::string_view key) const noexcept;
    [[nodiscard]] std::optional<Value> at(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept; // array/object length, 0 otherwise
    [[nodiscard]] bool is_array() const noexcept;
    [[nodiscard]] bool is_object() const noexcept;
    [[nodiscard]] bool is_null() const noexcept;
    [[nodiscard]] std::optional<std::string_view> string() const noexcept;
    // Integer syntax only (no fraction/exponent), must fit in int64.
    [[nodiscard]] std::optional<std::int64_t> int64() const noexcept;
    [[nodiscard]] std::optional<bool> boolean() const noexcept;
    // Accepts a JSON string or number; the exact source text goes through
    // Decimal::parse (ADR-0003), so exponents and over-long text are rejected.
    [[nodiscard]] std::optional<domain::Decimal> decimal() const noexcept;

  private:
    yyjson_val *value_;
};

class Document {
  public:
    // nullopt when text is empty, larger than max_bytes, invalid JSON, or
    // nested deeper than k_max_depth.
    [[nodiscard]] static std::optional<Document> parse(std::string_view text,
                                                       std::size_t max_bytes) noexcept;
    [[nodiscard]] Value root() const noexcept;

  private:
    struct Free {
        void operator()(yyjson_doc *doc) const noexcept;
    };
    explicit Document(yyjson_doc *doc) noexcept : doc_(doc) {}
    std::unique_ptr<yyjson_doc, Free> doc_;
};

} // namespace market_classifier::json
