#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {

[[nodiscard]] int mc_bridge_decode(const std::uint8_t *bytes, std::size_t size) noexcept;
[[nodiscard]] int mc_bridge_submit(const std::uint8_t *bytes, std::size_t size) noexcept;
[[nodiscard]] const char *mc_bridge_result_name(int code) noexcept;
[[nodiscard]] std::uint64_t mc_bridge_read_model_events() noexcept;
[[nodiscard]] std::uint64_t mc_bridge_dropped_batches() noexcept;
}
