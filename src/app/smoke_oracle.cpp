#include <app/smoke_oracle.hpp>

#include <cstddef>
#include <limits>
#include <sstream>

namespace nozzle_viewer {
namespace {

constexpr std::uint32_t moving_marker_margin = 12u;
constexpr std::uint32_t moving_marker_diameter = 24u;
constexpr std::uint32_t moving_marker_y = 144u;

bool is_color(
    std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b,
    std::uint8_t a,
    std::uint8_t expected_r,
    std::uint8_t expected_g,
    std::uint8_t expected_b,
    std::uint8_t expected_a) {
    return r == expected_r && g == expected_g && b == expected_b && a == expected_a;
}

std::string out_of_bounds_reason(const preview_image &image, std::uint32_t x, std::uint32_t y) {
    std::ostringstream stream;
    stream << "sample_out_of_bounds:x=" << x << ",y=" << y << ",width=" << image.width << ",height=" << image.height;
    return stream.str();
}

bool calculate_rgba_offset(const preview_image &image, std::uint32_t x, std::uint32_t y, std::size_t &offset) {
    if (image.width <= x || image.height <= y) {
        return false;
    }
    const std::size_t width = image.width;
    const std::size_t sample_x = x;
    const std::size_t sample_y = y;
    if (width != 0u && std::numeric_limits<std::size_t>::max() / width < sample_y) {
        return false;
    }
    const std::size_t pixel_index_base = sample_y * width;
    if (std::numeric_limits<std::size_t>::max() - pixel_index_base < sample_x) {
        return false;
    }
    const std::size_t pixel_index = pixel_index_base + sample_x;
    if ((std::numeric_limits<std::size_t>::max() - 3u) / 4u < pixel_index) {
        return false;
    }
    offset = pixel_index * 4u;
    return offset + 3u < image.pixels.size();
}

} // namespace

bool validate_smoke_dimensions(
    std::uint32_t width,
    std::uint32_t height,
    bool expect_alpha_patch,
    bool expect_moving_marker,
    std::string &failure_reason) {
    if (width == 0u || height == 0u) {
        failure_reason = "invalid_smoke_dimensions:width_height_must_be_positive";
        return false;
    }
    if (expect_alpha_patch) {
        const std::uint32_t alpha_x = width / 2u;
        const std::uint32_t alpha_y = (height / 2u) - (height / 16u);
        if (width <= alpha_x || height <= alpha_y) {
            std::ostringstream stream;
            stream << "invalid_smoke_dimensions:alpha_patch_out_of_range:x=" << alpha_x << ",y=" << alpha_y << ",width=" << width << ",height=" << height;
            failure_reason = stream.str();
            return false;
        }
    }
    if (expect_moving_marker) {
        if (width <= moving_marker_diameter) {
            std::ostringstream stream;
            stream << "invalid_smoke_dimensions:moving_marker_width_too_small:min_width=" << (moving_marker_diameter + 1u) << ",width=" << width;
            failure_reason = stream.str();
            return false;
        }
        if (height <= moving_marker_y) {
            std::ostringstream stream;
            stream << "invalid_smoke_dimensions:moving_marker_height_too_small:min_height=" << (moving_marker_y + 1u) << ",height=" << height;
            failure_reason = stream.str();
            return false;
        }
    }
    failure_reason.clear();
    return true;
}

smoke_sample_result sample_smoke_pixel(
    const preview_image &image,
    const std::string &name,
    std::uint32_t x,
    std::uint32_t y,
    std::uint8_t expected_r,
    std::uint8_t expected_g,
    std::uint8_t expected_b,
    std::uint8_t expected_a) {
    smoke_sample_result result{};
    result.name = name;
    result.x = x;
    result.y = y;
    result.expected_r = expected_r;
    result.expected_g = expected_g;
    result.expected_b = expected_b;
    result.expected_a = expected_a;
    std::size_t offset = 0u;
    if (!calculate_rgba_offset(image, x, y, offset)) {
        result.failure_reason = out_of_bounds_reason(image, x, y);
        return result;
    }
    result.actual_r = image.pixels[offset + 0u];
    result.actual_g = image.pixels[offset + 1u];
    result.actual_b = image.pixels[offset + 2u];
    result.actual_a = image.pixels[offset + 3u];
    result.passed = is_color(result.actual_r, result.actual_g, result.actual_b, result.actual_a, expected_r, expected_g, expected_b, expected_a);
    if (!result.passed) {
        result.failure_reason = "sample_color_mismatch";
    }
    return result;
}

std::uint64_t count_distinct_passed_marker_frames(const std::vector<smoke_marker_sample_result> &samples) {
    std::uint64_t count = 0u;
    std::uint64_t last_frame_index = 0u;
    bool have_last = false;
    for (const smoke_marker_sample_result &marker_sample : samples) {
        if (!marker_sample.sample.passed) {
            continue;
        }
        if (!have_last || marker_sample.frame_index != last_frame_index) {
            count = count + 1u;
            last_frame_index = marker_sample.frame_index;
            have_last = true;
        }
    }
    return count;
}

bool passed_marker_x_changed(const std::vector<smoke_marker_sample_result> &samples) {
    std::uint32_t first_x = 0u;
    bool have_first = false;
    for (const smoke_marker_sample_result &marker_sample : samples) {
        if (!marker_sample.sample.passed) {
            continue;
        }
        if (!have_first) {
            first_x = marker_sample.sample.x;
            have_first = true;
            continue;
        }
        if (marker_sample.sample.x != first_x) {
            return true;
        }
    }
    return false;
}

void verify_smoke_pattern(
    const preview_image &image,
    const smoke_oracle_options &options,
    std::uint64_t observed_frame_index,
    smoke_oracle_state &state) {
    state.samples.clear();
    state.top_left_red = false;
    state.top_right_green = false;
    state.bottom_left_blue = false;
    state.bottom_right_white = false;
    state.orientation_ok = false;
    state.channel_order_ok = false;
    state.alpha_patch_ok = false;
    state.moving_marker_ok = false;

    const std::uint32_t left_x = image.width / 8u;
    const std::uint32_t right_x = image.width == 0u ? 0u : image.width - 1u - image.width / 8u;
    const std::uint32_t top_y = image.height / 8u;
    const std::uint32_t bottom_y = image.height == 0u ? 0u : image.height - 1u - image.height / 8u;

    state.samples.push_back(sample_smoke_pixel(image, "top_left_red", left_x, top_y, 255u, 0u, 0u));
    state.samples.push_back(sample_smoke_pixel(image, "top_right_green", right_x, top_y, 0u, 255u, 0u));
    state.samples.push_back(sample_smoke_pixel(image, "bottom_left_blue", left_x, bottom_y, 0u, 0u, 255u));
    state.samples.push_back(sample_smoke_pixel(image, "bottom_right_white", right_x, bottom_y, 255u, 255u, 255u));
    if (options.expect_alpha_patch) {
        state.samples.push_back(sample_smoke_pixel(image, "center_magenta_alpha_patch", image.width / 2u, (image.height / 2u) - (image.height / 16u), 255u, 0u, 255u, 64u));
    }
    if (options.expect_moving_marker) {
        const std::uint64_t source_frame_index = observed_frame_index == 0u ? 0u : observed_frame_index - 1u;
        const std::uint32_t marker_x = image.width <= moving_marker_diameter ? moving_marker_margin : static_cast<std::uint32_t>((source_frame_index * 29u) % (image.width - moving_marker_diameter)) + moving_marker_margin;
        smoke_sample_result marker_sample = sample_smoke_pixel(image, "moving_yellow_marker", marker_x, moving_marker_y, 255u, 255u, 0u, 255u);
        state.samples.push_back(marker_sample);
        state.observed_marker_samples.push_back(smoke_marker_sample_result{observed_frame_index, marker_sample});
        if (marker_sample.passed) {
            state.observed_marker_x.push_back(marker_x);
        }
    }

    state.top_left_red = 0u < state.samples.size() && state.samples[0].passed;
    state.top_right_green = 1u < state.samples.size() && state.samples[1].passed;
    state.bottom_left_blue = 2u < state.samples.size() && state.samples[2].passed;
    state.bottom_right_white = 3u < state.samples.size() && state.samples[3].passed;
    state.alpha_patch_ok = !options.expect_alpha_patch || (4u < state.samples.size() && state.samples[4].passed);
    const std::size_t marker_sample_index = options.expect_alpha_patch ? 5u : 4u;
    state.moving_marker_ok = !options.expect_moving_marker || (
        marker_sample_index < state.samples.size() &&
        state.samples[marker_sample_index].passed &&
        options.min_frames <= count_distinct_passed_marker_frames(state.observed_marker_samples) &&
        passed_marker_x_changed(state.observed_marker_samples));
    state.orientation_ok = state.top_left_red && state.top_right_green && state.bottom_left_blue && state.bottom_right_white;
    state.channel_order_ok = state.top_left_red && state.bottom_left_blue;
}

} // namespace nozzle_viewer
