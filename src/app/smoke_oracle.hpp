#pragma once

#include <gui/preview_conversion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace nozzle_viewer {

struct smoke_sample_result {
    std::string name{};
    std::uint32_t x{0};
    std::uint32_t y{0};
    std::uint8_t expected_r{0};
    std::uint8_t expected_g{0};
    std::uint8_t expected_b{0};
    std::uint8_t expected_a{255};
    std::uint8_t actual_r{0};
    std::uint8_t actual_g{0};
    std::uint8_t actual_b{0};
    std::uint8_t actual_a{255};
    bool passed{false};
    std::string failure_reason{};
};

struct smoke_marker_sample_result {
    std::uint64_t frame_index{0};
    smoke_sample_result sample{};
};

struct smoke_oracle_state {
    bool top_left_red{false};
    bool top_right_green{false};
    bool bottom_left_blue{false};
    bool bottom_right_white{false};
    bool orientation_ok{false};
    bool channel_order_ok{false};
    bool alpha_patch_ok{false};
    bool moving_marker_ok{false};
    std::vector<smoke_sample_result> samples{};
    std::vector<std::uint32_t> observed_marker_x{};
    std::vector<smoke_marker_sample_result> observed_marker_samples{};
};

struct smoke_oracle_options {
    bool expect_alpha_patch{false};
    bool expect_moving_marker{false};
    std::uint64_t min_frames{1};
};

bool validate_smoke_dimensions(
    std::uint32_t width,
    std::uint32_t height,
    bool expect_alpha_patch,
    bool expect_moving_marker,
    std::string &failure_reason);

smoke_sample_result sample_smoke_pixel(
    const preview_image &image,
    const std::string &name,
    std::uint32_t x,
    std::uint32_t y,
    std::uint8_t expected_r,
    std::uint8_t expected_g,
    std::uint8_t expected_b,
    std::uint8_t expected_a = 255u);

std::uint64_t count_distinct_passed_marker_frames(const std::vector<smoke_marker_sample_result> &samples);

bool passed_marker_x_changed(const std::vector<smoke_marker_sample_result> &samples);

void verify_smoke_pattern(
    const preview_image &image,
    const smoke_oracle_options &options,
    std::uint64_t observed_frame_index,
    smoke_oracle_state &state);

} // namespace nozzle_viewer
