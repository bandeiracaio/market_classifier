#include "market_classifier/ui/workspace_codec.hpp"

#include "market_classifier/json/json.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <optional>

namespace market_classifier::ui {
namespace {

// Stable wire names for PanelKind; never reorder or rename (persisted schema).
constexpr std::array<std::string_view, k_panel_kind_count> k_kind_names{
    "Overview",  "TradesTape", "Ladder",        "Depth",   "Candles",
    "Cvd",       "Footprint",  "VolumeProfile", "Heatmap", "Liquidations",
    "BboSpread", "FundingOi",  "Diagnostics"};
constexpr std::array<std::string_view, 3> k_venue_names{"Binance", "Hyperliquid", "Both"};
constexpr std::size_t k_max_ini_bytes = 64 * 1024;

void append_escaped(std::string &out, std::string_view text) {
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(c));
                out += buffer;
            } else {
                out.push_back(c);
            }
        }
    }
    out.push_back('"');
}

// Fixed-point text (Decimal grammar: no exponent) for the min-trade filter; 6 decimals
// is far below any meaningful USD filter resolution.
std::string number_text(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.6f", value);
    std::string text(buffer);
    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

template <std::size_t N>
std::optional<std::size_t> index_of(const std::array<std::string_view, N> &names,
                                    std::string_view s) {
    for (std::size_t i = 0; i < N; ++i) {
        if (names[i] == s) {
            return i;
        }
    }
    return std::nullopt;
}

struct Reader {
    CodecError error = CodecError::None;
    bool fail(CodecError e) {
        if (error == CodecError::None) {
            error = e;
        }
        return false;
    }
};

// Optional numeric field with range check; absent => default kept.
bool read_int(Reader &r, const json::Value &obj, std::string_view key, std::int64_t lo,
              std::int64_t hi, std::int64_t &out) {
    const auto v = obj.get(key);
    if (!v) {
        return true;
    }
    const auto i = v->int64();
    if (!i) {
        return r.fail(CodecError::Malformed);
    }
    if (*i < lo || *i > hi) {
        return r.fail(CodecError::OutOfBounds);
    }
    out = *i;
    return true;
}

bool read_bool(Reader &r, const json::Value &obj, std::string_view key, bool &out) {
    const auto v = obj.get(key);
    if (!v) {
        return true;
    }
    const auto b = v->boolean();
    if (!b) {
        return r.fail(CodecError::Malformed);
    }
    out = *b;
    return true;
}

bool read_panel(Reader &r, const json::Value &p, PanelInstance &out) {
    if (!p.is_object()) {
        return r.fail(CodecError::Malformed);
    }
    std::int64_t id       = 0;
    const auto kind       = p.get("kind");
    const auto kind_text  = kind ? kind->string() : std::nullopt;
    const auto kind_index = kind_text ? index_of(k_kind_names, *kind_text) : std::nullopt;
    if (!kind_index) {
        return r.fail(CodecError::Malformed);
    }
    if (!read_int(r, p, "id", 1, 1'000'000, id) || id == 0) {
        return r.fail(CodecError::Malformed);
    }
    out.id   = static_cast<std::uint32_t>(id);
    out.kind = static_cast<PanelKind>(*kind_index);
    auto &s  = out.settings;
    if (const auto venue = p.get("venue")) {
        const auto name  = venue->string();
        const auto index = name ? index_of(k_venue_names, *name) : std::nullopt;
        if (!index) {
            return r.fail(CodecError::Malformed);
        }
        s.venue = effective_venue(out.kind, static_cast<VenueSelection>(*index));
    }
    std::int64_t interval = s.interval_ms, bucket = s.bucket_index, grouping = s.grouping_index;
    if (!read_int(r, p, "intervalMs", 60'000, 86'400'000, interval) ||
        !read_int(r, p, "bucket", 0, 3, bucket) || !read_int(r, p, "grouping", 0, 5, grouping) ||
        !read_bool(r, p, "cvdDailyReset", s.cvd_daily_reset)) {
        return false;
    }
    s.interval_ms    = interval;
    s.bucket_index   = static_cast<std::uint8_t>(bucket);
    s.grouping_index = static_cast<std::uint8_t>(grouping);
    if (const auto min = p.get("minTradeUsd")) {
        const auto d = min->decimal();
        if (!d) {
            return r.fail(CodecError::Malformed);
        }
        const double value = static_cast<double>(d->mantissa()) / std::pow(10.0, d->scale());
        if (!(value >= 0 && value <= 1e12)) {
            return r.fail(CodecError::OutOfBounds);
        }
        s.min_trade_usd = value;
    }
    return true;
}

bool read_layout(Reader &r, const json::Value &l, Layout &out) {
    const auto name   = l.get("name");
    const auto text   = name ? name->string() : std::nullopt;
    const auto ini    = l.get("imguiIni");
    const auto ini_s  = ini ? ini->string() : std::nullopt;
    const auto panels = l.get("panels");
    if (!text || !ini_s || !panels || !panels->is_array()) {
        return r.fail(CodecError::Malformed);
    }
    if (!valid_layout_name(*text) || ini_s->size() > k_max_ini_bytes ||
        panels->size() > k_max_panels_per_layout) {
        return r.fail(CodecError::OutOfBounds);
    }
    std::int64_t uid = 0;
    if (!read_int(r, l, "uid", 1, k_max_layout_uid, uid)) {
        return false;
    }
    out.uid       = static_cast<std::uint32_t>(uid); // 0 (absent, v0) => assigned on add
    out.name      = std::string(*text);
    out.imgui_ini = std::string(*ini_s);
    out.builtin   = false;
    for (std::size_t i = 0; i < panels->size(); ++i) {
        PanelInstance p;
        if (!read_panel(r, *panels->at(i), p)) {
            return false;
        }
        out.panels.push_back(p);
    }
    return true;
}

// v0 (pre-release): {"version":0,...} without "utcTime". Returns the schema version found.
std::optional<std::int64_t> schema_of(const json::Value &root) {
    if (const auto s = root.get("schema")) {
        return s->int64();
    }
    if (const auto v = root.get("version")) {
        return v->int64();
    }
    return std::nullopt;
}

} // namespace

std::string encode_workspace(const Workspace &ws) {
    std::string out = "{\"schema\":" + std::to_string(k_workspace_schema_version) +
                      ",\"active\":" + std::to_string(ws.active) +
                      ",\"utcTime\":" + (ws.utc_time ? "true" : "false") + ",\"layouts\":[";
    bool first = true;
    for (const auto &l : ws.layouts) {
        if (l.builtin) {
            continue;
        }
        out += first ? "" : ",";
        first = false;
        out += "{\"uid\":" + std::to_string(l.uid) + ",\"name\":";
        append_escaped(out, l.name);
        out += ",\"imguiIni\":";
        append_escaped(out, l.imgui_ini);
        out += ",\"panels\":[";
        for (std::size_t i = 0; i < l.panels.size(); ++i) {
            const auto &p = l.panels[i];
            const auto &s = p.settings;
            out += i == 0 ? "" : ",";
            out += "{\"id\":" + std::to_string(p.id) + ",\"kind\":\"" +
                   std::string(k_kind_names[static_cast<std::size_t>(p.kind)]) + "\",\"venue\":\"" +
                   std::string(k_venue_names[static_cast<std::size_t>(s.venue)]) +
                   "\",\"intervalMs\":" + std::to_string(s.interval_ms) +
                   ",\"bucket\":" + std::to_string(s.bucket_index) +
                   ",\"grouping\":" + std::to_string(s.grouping_index) +
                   ",\"minTradeUsd\":" + number_text(s.min_trade_usd) +
                   ",\"cvdDailyReset\":" + (s.cvd_daily_reset ? "true" : "false") + "}";
        }
        out += "]}";
    }
    out += "]}";
    return out;
}

DecodedWorkspace decode_workspace(std::string_view text) {
    DecodedWorkspace out{default_workspace(), CodecError::None};
    const auto fail = [&](CodecError e) { return DecodedWorkspace{default_workspace(), e}; };
    if (text.size() > k_max_workspace_json_bytes) {
        return fail(CodecError::TooLarge);
    }
    const auto doc = json::Document::parse(text, k_max_workspace_json_bytes);
    if (!doc || !doc->root().is_object()) {
        return fail(CodecError::Malformed);
    }
    const auto root    = doc->root();
    const auto version = schema_of(root);
    if (!version) {
        return fail(CodecError::Malformed);
    }
    if (*version < 0 || *version > static_cast<std::int64_t>(k_workspace_schema_version)) {
        return fail(CodecError::UnsupportedVersion);
    }
    Reader r;
    // Migration v0 -> v1: utcTime did not exist; default false (already the default).
    if (*version >= 1 && !read_bool(r, root, "utcTime", out.value.utc_time)) {
        return fail(r.error);
    }
    const auto layouts = root.get("layouts");
    if (!layouts || !layouts->is_array()) {
        return fail(CodecError::Malformed);
    }
    if (layouts->size() > k_max_user_layouts) {
        return fail(CodecError::OutOfBounds);
    }
    for (std::size_t i = 0; i < layouts->size(); ++i) {
        Layout l;
        if (!read_layout(r, *layouts->at(i), l)) {
            return fail(r.error);
        }
        if (!add_layout(out.value, std::move(l))) {
            return fail(CodecError::OutOfBounds);
        }
    }
    std::int64_t active = 0;
    if (!read_int(r, root, "active", 0, static_cast<std::int64_t>(out.value.layouts.size()) - 1,
                  active)) {
        return fail(r.error);
    }
    out.value.active = static_cast<std::size_t>(active);
    return out;
}

std::string_view codec_error_name(CodecError error) {
    switch (error) {
    case CodecError::None:
        return "ok";
    case CodecError::TooLarge:
        return "file too large";
    case CodecError::Malformed:
        return "not a valid workspace file";
    case CodecError::UnsupportedVersion:
        return "unsupported workspace version";
    case CodecError::OutOfBounds:
        return "workspace exceeds limits";
    }
    return "unknown error";
}

} // namespace market_classifier::ui
