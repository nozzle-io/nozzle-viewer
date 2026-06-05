#pragma once

#include <nozzle/pixel_access.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nozzle_viewer {

struct preview_image {
    std::vector<std::uint8_t> pixels{};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::ptrdiff_t row_stride_bytes{0};
};

bool is_known_preview_format(nozzle::texture_format format) noexcept;

bool convert_to_rgba8_preview(
    const nozzle::mapped_pixels &source,
    preview_image &destination,
    std::string *error_message = nullptr);

} // namespace nozzle_viewer
