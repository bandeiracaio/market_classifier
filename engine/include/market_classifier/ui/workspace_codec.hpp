#pragma once

#include "market_classifier/ui/workspace.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace market_classifier::ui {

enum class CodecError : std::uint8_t { None, TooLarge, Malformed, UnsupportedVersion, OutOfBounds };

// {"schema":1,"active":N,"utcTime":b,"layouts":[user layouts only]}. Builtin presets are
// never serialized; `active` indexes builtins-then-user layouts.
[[nodiscard]] std::string encode_workspace(const Workspace &ws);

struct DecodedWorkspace {
    Workspace value;
    CodecError error = CodecError::None;
};

// Validates size, schema version, shape and bounds, running migrations up to the current
// version (docs/runtime/mvp-workspace.md). Never partially applies: on error `value` is
// the default workspace.
[[nodiscard]] DecodedWorkspace decode_workspace(std::string_view json);

[[nodiscard]] std::string_view codec_error_name(CodecError error);

} // namespace market_classifier::ui
