#include "market_classifier/json/json.hpp"

#include <charconv>
#include <yyjson.h>

namespace market_classifier::json {
namespace {

// Iterative pre-scan: yyjson has no depth limit, so reject deep nesting before
// parsing. Brackets inside strings are skipped (escape-aware).
bool within_depth(std::string_view text) noexcept {
    std::size_t depth = 0;
    bool in_string    = false;
    bool escaped      = false;
    for (const char c : text) {
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '[' || c == '{') {
            if (++depth > k_max_depth) {
                return false;
            }
        } else if ((c == ']' || c == '}') && depth > 0) {
            --depth;
        }
    }
    return true;
}

std::optional<std::string_view> number_text(yyjson_val *value) noexcept {
    if (yyjson_is_raw(value)) {
        return std::string_view(yyjson_get_raw(value), yyjson_get_len(value));
    }
    return std::nullopt;
}

} // namespace

void Document::Free::operator()(yyjson_doc *doc) const noexcept {
    yyjson_doc_free(doc);
}

std::optional<Document> Document::parse(std::string_view text, std::size_t max_bytes) noexcept {
    if (text.empty() || text.size() > max_bytes || !within_depth(text)) {
        return std::nullopt;
    }
    // NUMBER_AS_RAW keeps every number as its source text so prices never pass
    // through double.
    yyjson_read_err err{};
    auto *doc = yyjson_read_opts(const_cast<char *>(text.data()), text.size(),
                                 YYJSON_READ_NUMBER_AS_RAW, nullptr, &err);
    if (doc == nullptr) {
        return std::nullopt;
    }
    return Document(doc);
}

Value Document::root() const noexcept {
    return Value(yyjson_doc_get_root(doc_.get()));
}

std::optional<Value> Value::get(std::string_view key) const noexcept {
    if (!yyjson_is_obj(value_)) {
        return std::nullopt;
    }
    auto *child = yyjson_obj_getn(value_, key.data(), key.size());
    if (child == nullptr) {
        return std::nullopt;
    }
    return Value(child);
}

std::optional<Value> Value::at(std::size_t index) const noexcept {
    if (!yyjson_is_arr(value_) || index >= yyjson_arr_size(value_)) {
        return std::nullopt;
    }
    return Value(yyjson_arr_get(value_, index));
}

std::size_t Value::size() const noexcept {
    if (yyjson_is_arr(value_)) {
        return yyjson_arr_size(value_);
    }
    if (yyjson_is_obj(value_)) {
        return yyjson_obj_size(value_);
    }
    return 0;
}

bool Value::is_array() const noexcept {
    return yyjson_is_arr(value_);
}
bool Value::is_object() const noexcept {
    return yyjson_is_obj(value_);
}
bool Value::is_null() const noexcept {
    return yyjson_is_null(value_);
}

std::optional<std::string_view> Value::string() const noexcept {
    if (!yyjson_is_str(value_)) {
        return std::nullopt;
    }
    return std::string_view(yyjson_get_str(value_), yyjson_get_len(value_));
}

std::optional<std::int64_t> Value::int64() const noexcept {
    const auto text = number_text(value_);
    if (!text) {
        return std::nullopt;
    }
    std::int64_t out{};
    const auto *end      = text->data() + text->size();
    const auto [ptr, ec] = std::from_chars(text->data(), end, out);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return out;
}

std::optional<bool> Value::boolean() const noexcept {
    if (!yyjson_is_bool(value_)) {
        return std::nullopt;
    }
    return yyjson_get_bool(value_);
}

std::optional<domain::Decimal> Value::decimal() const noexcept {
    auto text = number_text(value_);
    if (!text) {
        text = string();
    }
    if (!text) {
        return std::nullopt;
    }
    const auto parsed = domain::Decimal::parse(*text);
    if (!parsed) {
        return std::nullopt;
    }
    return parsed.value;
}

} // namespace market_classifier::json
