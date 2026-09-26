#pragma once

#include <cstdint>
#include <string_view>

namespace market_classifier {

struct Version {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
};

// Project version, kept in sync with CMakeLists.txt project() VERSION field.
inline constexpr Version k_version{0, 1, 0};

inline constexpr std::string_view k_version_string{"0.1.0"};

} // namespace market_classifier
