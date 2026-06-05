#include <gui/preview_conversion.hpp>

#include <nozzle/format_resolve.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace nozzle_viewer {

namespace {

constexpr std::uint8_t k_alpha_opaque = 255;

void set_error(std::string *error_message, const char *message) {
    if (error_message) {
        *error_message = message;
    }
}

bool safe_abs_stride(std::ptrdiff_t stride, std::uint64_t &absolute_stride) {
    if (stride == std::numeric_limits<std::ptrdiff_t>::min()) {
        return false;
    }
    if (stride < 0) {
        absolute_stride = static_cast<std::uint64_t>(-stride);
    } else {
        absolute_stride = static_cast<std::uint64_t>(stride);
    }
    return true;
}

bool row_pointer(const std::uint8_t *base, std::ptrdiff_t stride, std::uint32_t row, const std::uint8_t *&result) {
    if (row == 0) {
        result = base;
        return true;
    }
    const auto row_index = static_cast<std::uint64_t>(row);
    if (0 < stride) {
        const auto positive_stride = static_cast<std::uint64_t>(stride);
        if (static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) / row_index < positive_stride) {
            return false;
        }
        result = base + static_cast<std::ptrdiff_t>(positive_stride * row_index);
        return true;
    }
    if (stride < 0) {
        if (stride == std::numeric_limits<std::ptrdiff_t>::min()) {
            return false;
        }
        const auto positive_stride = static_cast<std::uint64_t>(-stride);
        if (static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) / row_index < positive_stride) {
            return false;
        }
        result = base - static_cast<std::ptrdiff_t>(positive_stride * row_index);
        return true;
    }
    return false;
}

std::uint16_t read_u16(const std::uint8_t *source) {
    std::uint16_t value{};
    std::memcpy(&value, source, sizeof(value));
    return value;
}

std::uint32_t read_u32(const std::uint8_t *source) {
    std::uint32_t value{};
    std::memcpy(&value, source, sizeof(value));
    return value;
}

float read_f32(const std::uint8_t *source) {
    float value{};
    std::memcpy(&value, source, sizeof(value));
    return value;
}

float half_to_float(std::uint16_t half) {
    std::uint32_t sign = static_cast<std::uint32_t>(half & 0x8000) << 16;
    std::uint32_t exponent = (half >> 10) & 0x1f;
    std::uint32_t mantissa = half & 0x03ff;

    std::uint32_t bits{};
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            int shift = 0;
            while ((mantissa & 0x0400) == 0) {
                mantissa <<= 1;
                shift = shift + 1;
            }
            mantissa &= 0x03ff;
            exponent = static_cast<std::uint32_t>(113 - shift);
            bits = sign | (exponent << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1f) {
        bits = sign | (0xffu << 23) | (mantissa << 13);
    } else {
        exponent = exponent - 15 + 127;
        bits = sign | (exponent << 23) | (mantissa << 13);
    }

    float value{};
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint8_t normalized_u16_to_u8(std::uint16_t value) {
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) * 255u + 32767u) / 65535u);
}

std::uint8_t clamped_float_to_u8(float value) {
    if (std::isnan(value)) {
        return 0;
    }
    if (value <= 0.0f) {
        return 0;
    }
    if (1.0f <= value) {
        return 255;
    }
    return static_cast<std::uint8_t>(std::lround(value * 255.0f));
}

std::uint8_t clamped_alpha_to_u8(float value) {
    if (std::isnan(value)) {
        return k_alpha_opaque;
    }
    return clamped_float_to_u8(value);
}

void write_nan_diagnostic(std::uint8_t *destination) {
    destination[0] = 255;
    destination[1] = 0;
    destination[2] = 255;
    destination[3] = k_alpha_opaque;
}

std::uint8_t source_component_offset(nozzle::channel_order order, std::uint8_t channel, std::uint8_t component_bytes) {
    switch (order) {
    case nozzle::channel_order::r:
        return channel == 0 ? 0 : 0xff;
    case nozzle::channel_order::rg:
        return channel < 2 ? static_cast<std::uint8_t>(channel * component_bytes) : 0xff;
    case nozzle::channel_order::rgb:
        return channel < 3 ? static_cast<std::uint8_t>(channel * component_bytes) : 0xff;
    case nozzle::channel_order::rgba:
        return channel < 4 ? static_cast<std::uint8_t>(channel * component_bytes) : 0xff;
    case nozzle::channel_order::bgra:
        if (channel == 0) {
            return static_cast<std::uint8_t>(2 * component_bytes);
        }
        if (channel == 1) {
            return static_cast<std::uint8_t>(component_bytes);
        }
        if (channel == 2) {
            return 0;
        }
        if (channel == 3) {
            return static_cast<std::uint8_t>(3 * component_bytes);
        }
        return 0xff;
    case nozzle::channel_order::argb:
    case nozzle::channel_order::unknown:
        return 0xff;
    }
    return 0xff;
}

bool convert_unorm_pixel(const std::uint8_t *source, const nozzle::cpu_layout_desc &layout, std::uint8_t *destination) {
    destination[0] = 0;
    destination[1] = 0;
    destination[2] = 0;
    destination[3] = k_alpha_opaque;

    const std::uint8_t component_bytes = static_cast<std::uint8_t>(layout.component_bits / 8);
    for (std::uint8_t channel = 0; channel < 4; channel = static_cast<std::uint8_t>(channel + 1)) {
        const std::uint8_t offset = source_component_offset(layout.order, channel, component_bytes);
        if (offset == 0xff) {
            continue;
        }
        if (layout.component_bits == 8) {
            destination[channel] = source[offset];
        } else if (layout.component_bits == 16) {
            destination[channel] = normalized_u16_to_u8(read_u16(source + offset));
        } else {
            return false;
        }
    }
    return true;
}

bool convert_uint_pixel(const std::uint8_t *source, const nozzle::cpu_layout_desc &layout, std::uint8_t *destination) {
    destination[0] = 0;
    destination[1] = 0;
    destination[2] = 0;
    destination[3] = k_alpha_opaque;

    if (layout.component_bits != 32) {
        return false;
    }

    for (std::uint8_t channel = 0; channel < 4; channel = static_cast<std::uint8_t>(channel + 1)) {
        const std::uint8_t offset = source_component_offset(layout.order, channel, sizeof(std::uint32_t));
        if (offset == 0xff) {
            continue;
        }
        destination[channel] = static_cast<std::uint8_t>(read_u32(source + offset) & 0xffu);
    }
    return true;
}

bool convert_float_pixel(const std::uint8_t *source, const nozzle::cpu_layout_desc &layout, std::uint8_t *destination) {
    std::array<float, 4> values{0.0f, 0.0f, 0.0f, 1.0f};
    const std::uint8_t component_bytes = static_cast<std::uint8_t>(layout.component_bits / 8);
    bool has_nan_color = false;

    for (std::uint8_t channel = 0; channel < 4; channel = static_cast<std::uint8_t>(channel + 1)) {
        const std::uint8_t offset = source_component_offset(layout.order, channel, component_bytes);
        if (offset == 0xff) {
            continue;
        }
        if (layout.component_bits == 16) {
            values[channel] = half_to_float(read_u16(source + offset));
        } else if (layout.component_bits == 32) {
            values[channel] = read_f32(source + offset);
        } else {
            return false;
        }
        if (channel < 3 && std::isnan(values[channel])) {
            has_nan_color = true;
        }
    }

    if (has_nan_color) {
        write_nan_diagnostic(destination);
        return true;
    }

    destination[0] = clamped_float_to_u8(values[0]);
    destination[1] = clamped_float_to_u8(values[1]);
    destination[2] = clamped_float_to_u8(values[2]);
    destination[3] = clamped_alpha_to_u8(values[3]);
    return true;
}

bool convert_depth_pixel(const std::uint8_t *source, const nozzle::cpu_layout_desc &layout, std::uint8_t *destination) {
    if (layout.component_bits != 32) {
        return false;
    }
    const float value = read_f32(source);
    if (std::isnan(value)) {
        write_nan_diagnostic(destination);
        return true;
    }
    const std::uint8_t luminance = clamped_float_to_u8(value);
    destination[0] = luminance;
    destination[1] = luminance;
    destination[2] = luminance;
    destination[3] = k_alpha_opaque;
    return true;
}

bool convert_pixel(const std::uint8_t *source, const nozzle::cpu_layout_desc &layout, std::uint8_t *destination) {
    switch (layout.component) {
    case nozzle::component_type::unorm:
        return convert_unorm_pixel(source, layout, destination);
    case nozzle::component_type::uint:
        return convert_uint_pixel(source, layout, destination);
    case nozzle::component_type::floating:
        return convert_float_pixel(source, layout, destination);
    case nozzle::component_type::depth:
        return convert_depth_pixel(source, layout, destination);
    case nozzle::component_type::snorm:
    case nozzle::component_type::sint:
    case nozzle::component_type::unknown:
        return false;
    }
    return false;
}

} // namespace

bool is_known_preview_format(nozzle::texture_format format) noexcept {
    return format != nozzle::texture_format::unknown &&
        nozzle::resolve_bytes_per_pixel(format) != 0 &&
        nozzle::resolve_channel_count(format) != 0;
}

bool convert_to_rgba8_preview(
    const nozzle::mapped_pixels &source,
    preview_image &destination,
    std::string *error_message)
{
    destination = {};
    if (!source.data) {
        set_error(error_message, "source pixels are null");
        return false;
    }
    if (source.width == 0 || source.height == 0) {
        set_error(error_message, "source dimensions are zero");
        return false;
    }
    if (!is_known_preview_format(source.format)) {
        set_error(error_message, "unsupported preview format");
        return false;
    }

    const auto layout = nozzle::resolve_cpu_layout(source.format);
    const std::uint64_t minimum_row_stride = static_cast<std::uint64_t>(source.width) * layout.bytes_per_pixel;
    std::uint64_t absolute_row_stride{};
    if (!safe_abs_stride(source.row_stride_bytes, absolute_row_stride)) {
        set_error(error_message, "source row stride is invalid");
        return false;
    }
    if (absolute_row_stride < minimum_row_stride) {
        set_error(error_message, "source row stride is too small");
        return false;
    }

    const std::uint64_t output_size = static_cast<std::uint64_t>(source.width) * source.height * 4u;
    if (static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) < output_size) {
        set_error(error_message, "preview buffer is too large");
        return false;
    }

    destination.width = source.width;
    destination.height = source.height;
    destination.row_stride_bytes = static_cast<std::ptrdiff_t>(source.width) * 4;
    destination.pixels.assign(static_cast<std::size_t>(output_size), 0);

    const auto *base = static_cast<const std::uint8_t *>(source.data);
    for (std::uint32_t row = 0; row < source.height; row = row + 1) {
        const std::uint8_t *source_row{};
        if (!row_pointer(base, source.row_stride_bytes, row, source_row)) {
            set_error(error_message, "source row pointer overflow");
            destination = {};
            return false;
        }
        auto *destination_row = destination.pixels.data() +
            static_cast<std::uint64_t>(row) * static_cast<std::uint64_t>(destination.row_stride_bytes);
        for (std::uint32_t column = 0; column < source.width; column = column + 1) {
            const auto *source_pixel = source_row + static_cast<std::uint64_t>(column) * layout.bytes_per_pixel;
            auto *destination_pixel = destination_row + static_cast<std::uint64_t>(column) * 4u;
            if (!convert_pixel(source_pixel, layout, destination_pixel)) {
                set_error(error_message, "preview conversion failed");
                destination = {};
                return false;
            }
        }
    }
    if (error_message) {
        error_message->clear();
    }
    return true;
}

} // namespace nozzle_viewer
