#pragma once

#include "market_classifier/bridge/raw_frame.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mc_test {

inline std::string read_file(const std::string &relative) {
    std::ifstream in(std::string(MC_SOURCE_DIR "/fixtures/mvp/") + relative, std::ios::binary);
    if (!in) {
        throw std::runtime_error("missing fixture " + relative);
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Splits a top-level JSON array into the raw text of each element (string-aware).
inline std::vector<std::string> split_array(const std::string &text) {
    std::vector<std::string> out;
    int depth         = 0;
    bool in_string    = false;
    bool escaped      = false;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
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
            if (depth == 1 && c == '{') {
                start = i;
            }
            ++depth;
        } else if (c == ']' || c == '}') {
            --depth;
            if (depth == 1 && c == '}') {
                out.push_back(text.substr(start, i - start + 1));
            }
        }
    }
    return out;
}

inline std::vector<std::string> read_lines(const std::string &relative) {
    std::istringstream in(read_file(relative));
    std::vector<std::string> out;
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (!line.empty()) {
            out.push_back(line);
        }
    }
    return out;
}

// Element `index` of a captured WS frame list.
inline market_classifier::bridge::RawFrame load_frame(const std::string &relative,
                                                      market_classifier::venues::StreamTag tag,
                                                      std::int64_t receive_time_ms,
                                                      std::size_t index = 0) {
    return {tag, receive_time_ms, split_array(read_file(relative)).at(index)};
}

// Whole file as one payload (REST responses).
inline market_classifier::bridge::RawFrame load_payload(const std::string &relative,
                                                        market_classifier::venues::StreamTag tag,
                                                        std::int64_t receive_time_ms) {
    return {tag, receive_time_ms, read_file(relative)};
}

inline market_classifier::bridge::RawFrame load_malformed(const std::string &name) {
    return {market_classifier::venues::StreamTag::BinanceWs, 1,
            read_file("malformed/" + name + ".json")};
}

} // namespace mc_test
