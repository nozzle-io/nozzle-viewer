#include <gui/gui.hpp>
#include <gui/preview_conversion.hpp>
#include <app/smoke_oracle.hpp>

#include <nozzle/pixel_access.hpp>
#include <nozzle/receiver.hpp>
#include <nozzle/sender.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct smoke_options {
    bool receiver_enabled{false};
    bool sender_enabled{false};
    bool expect_alpha_patch{false};
    bool expect_moving_marker{false};
    std::string source_name{"juce_nozzle_app_smoke"};
    std::uint32_t width{320};
    std::uint32_t height{240};
    std::uint64_t min_frames{1};
    std::uint64_t timeout_ms{10000};
    std::uint64_t frame_count{300};
    std::uint64_t interval_ms{16};
    std::string evidence_path{};
};



struct smoke_sender_result {
    bool passed{false};
    std::string failure_reason{};
    std::uint64_t published_count{0};
    std::uint32_t observed_width{0};
    std::uint32_t observed_height{0};
    int mapped_format{0};
    std::int64_t mapped_row_stride_bytes{0};
};

struct smoke_result {
    bool passed{false};
    std::string failure_reason{};
    std::uint32_t observed_width{0};
    std::uint32_t observed_height{0};
    std::uint64_t observed_frame_index{0};
    std::uint64_t observed_frame_count{0};
    std::uint64_t distinct_frame_count{0};
    std::uint64_t timeout_ms{0};
    int mapped_format{0};
    std::int64_t mapped_row_stride_bytes{0};
    bool dimensions_ok{false};
    bool top_left_red{false};
    bool top_right_green{false};
    bool bottom_left_blue{false};
    bool bottom_right_white{false};
    bool orientation_ok{false};
    bool channel_order_ok{false};
    bool alpha_patch_ok{false};
    bool moving_marker_ok{false};
    bool distinct_frames_ok{false};
    std::vector<nozzle_viewer::smoke_sample_result> samples{};
    std::vector<std::uint64_t> observed_frame_indices{};
    std::vector<std::uint32_t> observed_marker_x{};
    std::vector<nozzle_viewer::smoke_marker_sample_result> observed_marker_samples{};
};

bool parse_u32(const char *text, std::uint32_t &out_value) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }
    char *end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || 0xfffffffful < value) {
        return false;
    }
    out_value = static_cast<std::uint32_t>(value);
    return 0 < out_value;
}

bool parse_u64(const char *text, std::uint64_t &out_value) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }
    char *end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    out_value = static_cast<std::uint64_t>(value);
    return true;
}

bool parse_smoke_options(int argc, char **argv, smoke_options &options) {
    for (int index = 1; index < argc; index = index + 1) {
        const char *arg = argv[index];
        auto require_value = [&](const char *name) -> const char * {
            if (argc <= index + 1) {
                std::fprintf(stderr, "missing value for %s\n", name);
                return nullptr;
            }
            index = index + 1;
            return argv[index];
        };
        if (std::strcmp(arg, "--smoke-receiver") == 0) {
            options.receiver_enabled = true;
        } else if (std::strcmp(arg, "--smoke-sender") == 0) {
            options.sender_enabled = true;
        } else if (std::strcmp(arg, "--source") == 0) {
            const char *value = require_value(arg);
            if (value == nullptr || value[0] == '\0') {
                return false;
            }
            options.source_name = value;
        } else if (std::strcmp(arg, "--width") == 0) {
            const char *value = require_value(arg);
            if (!parse_u32(value, options.width)) {
                return false;
            }
        } else if (std::strcmp(arg, "--height") == 0) {
            const char *value = require_value(arg);
            if (!parse_u32(value, options.height)) {
                return false;
            }
        } else if (std::strcmp(arg, "--min-frames") == 0) {
            const char *value = require_value(arg);
            if (!parse_u64(value, options.min_frames) || options.min_frames == 0) {
                return false;
            }
        } else if (std::strcmp(arg, "--timeout-ms") == 0) {
            const char *value = require_value(arg);
            if (!parse_u64(value, options.timeout_ms)) {
                return false;
            }
        } else if (std::strcmp(arg, "--frames") == 0) {
            const char *value = require_value(arg);
            if (!parse_u64(value, options.frame_count) || options.frame_count == 0u || 12000u < options.frame_count) {
                return false;
            }
        } else if (std::strcmp(arg, "--interval-ms") == 0) {
            const char *value = require_value(arg);
            if (!parse_u64(value, options.interval_ms) || 1000u < options.interval_ms) {
                return false;
            }
        } else if (std::strcmp(arg, "--evidence") == 0) {
            const char *value = require_value(arg);
            if (value == nullptr || value[0] == '\0') {
                return false;
            }
            options.evidence_path = value;
        } else if (std::strcmp(arg, "--expect-alpha-patch") == 0) {
            options.expect_alpha_patch = true;
        } else if (std::strcmp(arg, "--expect-moving-marker") == 0) {
            options.expect_moving_marker = true;
        } else if (std::strcmp(arg, "--help") == 0) {
            std::printf("Usage: nozzle-viewer [--smoke-receiver --source NAME --width N --height N --min-frames N --timeout-ms N --evidence PATH --expect-alpha-patch --expect-moving-marker]\n");
            std::printf("       nozzle-viewer --smoke-sender --source NAME --width N --height N --frames N --interval-ms N --evidence PATH\n");
            return false;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", arg);
            return false;
        }
    }
    if (options.receiver_enabled && options.sender_enabled) {
        std::fprintf(stderr, "choose only one smoke mode\n");
        return false;
    }
    return true;
}

void print_usage() {
    std::printf("Usage: nozzle-viewer [--help]\n");
    std::printf("       nozzle-viewer --smoke-receiver --source NAME --width N --height N --min-frames N --timeout-ms N --evidence PATH [--expect-alpha-patch] [--expect-moving-marker]\n");
    std::printf("       nozzle-viewer --smoke-sender --source NAME --width N --height N --frames N --interval-ms N --evidence PATH\n");
}

std::string json_escape(const std::string &text) {
    std::string result;
    result.reserve(text.size() + 8);
    for (char c : text) {
        switch (c) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20u) {
                result += "?";
            } else {
                result += c;
            }
            break;
        }
    }
    return result;
}

const char *os_name() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown";
#endif
}

const char *bool_check(bool value) {
    return value ? "PASS" : "FAIL";
}

const char *backend_name(nozzle::backend_type backend) {
    switch (backend) {
    case nozzle::backend_type::d3d11: return "D3D11";
    case nozzle::backend_type::metal: return "Metal";
    case nozzle::backend_type::opengl: return "OpenGL";
    case nozzle::backend_type::dma_buf: return "DMA-BUF";
    case nozzle::backend_type::unknown: return "Unknown";
    }
    return "Unknown";
}

std::string make_evidence_json(const smoke_options &options, const smoke_result &result, const nozzle::connected_sender_info &sender_info) {
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"schema_version\": \"0.1.0\",\n";
    stream << "  \"tool\": \"nozzle-viewer\",\n";
    stream << "  \"os\": \"" << os_name() << "\",\n";
    stream << "  \"backend\": \"" << backend_name(sender_info.backend) << "\",\n";
    stream << "  \"role\": \"viewer_receiver\",\n";
    stream << "  \"sender_name\": \"" << json_escape(options.source_name) << "\",\n";
    stream << "  \"connected_sender_name\": \"" << json_escape(sender_info.name) << "\",\n";
    stream << "  \"connected_sender_application\": \"" << json_escape(sender_info.application_name) << "\",\n";
    stream << "  \"receiver_name\": \"nozzle-viewer smoke receiver\",\n";
    stream << "  \"dimensions\": {\n";
    stream << "    \"expected_width\": " << options.width << ",\n";
    stream << "    \"expected_height\": " << options.height << ",\n";
    stream << "    \"observed_width\": " << result.observed_width << ",\n";
    stream << "    \"observed_height\": " << result.observed_height << "\n";
    stream << "  },\n";
    stream << "  \"frame\": {\n";
    stream << "    \"observed_index\": " << result.observed_frame_index << ",\n";
    stream << "    \"observed_count\": " << result.observed_frame_count << ",\n";
    stream << "    \"distinct_count\": " << result.distinct_frame_count << ",\n";
    stream << "    \"minimum_required_count\": " << options.min_frames << ",\n";
    stream << "    \"timeout_ms\": " << result.timeout_ms << ",\n";
    stream << "    \"observed_indices\": [";
    for (std::size_t index = 0; index < result.observed_frame_indices.size(); index = index + 1u) {
        if (0u < index) {
            stream << ",";
        }
        stream << result.observed_frame_indices[index];
    }
    stream << "],\n";
    stream << "    \"observed_marker_x\": [";
    for (std::size_t index = 0; index < result.observed_marker_x.size(); index = index + 1u) {
        if (0u < index) {
            stream << ",";
        }
        stream << result.observed_marker_x[index];
    }
    stream << "],\n";
    stream << "    \"observed_marker_samples\": [\n";
    for (std::size_t index = 0; index < result.observed_marker_samples.size(); index = index + 1u) {
        const nozzle_viewer::smoke_marker_sample_result &marker_sample = result.observed_marker_samples[index];
        const nozzle_viewer::smoke_sample_result &sample = marker_sample.sample;
        stream << "      {\"frame_index\":" << marker_sample.frame_index << ",\"x\":" << sample.x << ",\"y\":" << sample.y;
        stream << ",\"expected_rgba\":[" << static_cast<int>(sample.expected_r) << "," << static_cast<int>(sample.expected_g) << "," << static_cast<int>(sample.expected_b) << "," << static_cast<int>(sample.expected_a) << "]";
        stream << ",\"actual_rgba\":[" << static_cast<int>(sample.actual_r) << "," << static_cast<int>(sample.actual_g) << "," << static_cast<int>(sample.actual_b) << "," << static_cast<int>(sample.actual_a) << "]";
        stream << ",\"passed\":" << (sample.passed ? "true" : "false");
        stream << ",\"failure_reason\":\"" << json_escape(sample.failure_reason) << "\"}";
        if (index + 1u < result.observed_marker_samples.size()) {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "    ]\n";
    stream << "  },\n";
    stream << "  \"mapping\": {\n";
    stream << "    \"format\": " << result.mapped_format << ",\n";
    stream << "    \"row_stride_bytes\": " << result.mapped_row_stride_bytes << "\n";
    stream << "  },\n";
    stream << "  \"checks\": {\n";
    stream << "    \"dimensions\": \"" << bool_check(result.dimensions_ok) << "\",\n";
    stream << "    \"top_left_red\": \"" << bool_check(result.top_left_red) << "\",\n";
    stream << "    \"top_right_green\": \"" << bool_check(result.top_right_green) << "\",\n";
    stream << "    \"bottom_left_blue\": \"" << bool_check(result.bottom_left_blue) << "\",\n";
    stream << "    \"bottom_right_white\": \"" << bool_check(result.bottom_right_white) << "\",\n";
    stream << "    \"orientation\": \"" << bool_check(result.orientation_ok) << "\",\n";
    stream << "    \"channel_order\": \"" << bool_check(result.channel_order_ok) << "\",\n";
    stream << "    \"alpha_patch\": \"" << bool_check(result.alpha_patch_ok) << "\",\n";
    stream << "    \"moving_marker\": \"" << bool_check(result.moving_marker_ok) << "\",\n";
    stream << "    \"distinct_frames\": \"" << bool_check(result.distinct_frames_ok) << "\"\n";
    stream << "  },\n";
    stream << "  \"samples\": [\n";
    for (std::size_t index = 0; index < result.samples.size(); index = index + 1u) {
        const nozzle_viewer::smoke_sample_result &sample = result.samples[index];
        stream << "    {\"name\":\"" << json_escape(sample.name) << "\",\"x\":" << sample.x << ",\"y\":" << sample.y;
        stream << ",\"expected_rgba\":[" << static_cast<int>(sample.expected_r) << "," << static_cast<int>(sample.expected_g) << "," << static_cast<int>(sample.expected_b) << "," << static_cast<int>(sample.expected_a) << "]";
        stream << ",\"actual_rgba\":[" << static_cast<int>(sample.actual_r) << "," << static_cast<int>(sample.actual_g) << "," << static_cast<int>(sample.actual_b) << "," << static_cast<int>(sample.actual_a) << "]";
        stream << ",\"passed\":" << (sample.passed ? "true" : "false");
        stream << ",\"failure_reason\":\"" << json_escape(sample.failure_reason) << "\"}";
        if (index + 1u < result.samples.size()) {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "  ],\n";
    stream << "  \"verdict\": \"" << (result.passed ? "PASS" : "FAIL") << "\",\n";
    stream << "  \"failure_reason\": \"" << json_escape(result.failure_reason) << "\"\n";
    stream << "}\n";
    return stream.str();
}

bool write_evidence(const std::string &path, const std::string &json) {
    if (path.empty()) {
        std::printf("%s", json.c_str());
        return true;
    }
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    stream << json;
    return static_cast<bool>(stream);
}


std::string make_sender_evidence_json(const smoke_options &options, const smoke_sender_result &result, const nozzle::sender_info &sender_info) {
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"schema_version\": \"0.1.0\",\n";
    stream << "  \"tool\": \"nozzle-viewer\",\n";
    stream << "  \"os\": \"" << os_name() << "\",\n";
    stream << "  \"backend\": \"" << backend_name(sender_info.backend) << "\",\n";
    stream << "  \"role\": \"viewer_sender\",\n";
    stream << "  \"sender_name\": \"" << json_escape(options.source_name) << "\",\n";
    stream << "  \"sender_application\": \"" << json_escape(sender_info.application_name) << "\",\n";
    stream << "  \"sender_id\": \"" << json_escape(sender_info.id) << "\",\n";
    stream << "  \"dimensions\": {\n";
    stream << "    \"width\": " << options.width << ",\n";
    stream << "    \"height\": " << options.height << ",\n";
    stream << "    \"observed_width\": " << result.observed_width << ",\n";
    stream << "    \"observed_height\": " << result.observed_height << "\n";
    stream << "  },\n";
    stream << "  \"frame\": {\n";
    stream << "    \"requested_count\": " << options.frame_count << ",\n";
    stream << "    \"published_count\": " << result.published_count << ",\n";
    stream << "    \"interval_ms\": " << options.interval_ms << "\n";
    stream << "  },\n";
    stream << "  \"mapping\": {\n";
    stream << "    \"format\": " << result.mapped_format << ",\n";
    stream << "    \"row_stride_bytes\": " << result.mapped_row_stride_bytes << "\n";
    stream << "  },\n";
    stream << "  \"pattern\": {\n";
    stream << "    \"top_left\": [255,0,0,255],\n";
    stream << "    \"top_right\": [0,255,0,255],\n";
    stream << "    \"bottom_left\": [0,0,255,255],\n";
    stream << "    \"bottom_right\": [255,255,255,255],\n";
    stream << "    \"alpha_patch\": [255,0,255,64],\n";
    stream << "    \"moving_marker\": [255,255,0,255],\n";
    stream << "    \"moving_marker_formula\": \"x=((frame_index*29)%(width-24))+12,y=144\"\n";
    stream << "  },\n";
    stream << "  \"verdict\": \"" << (result.passed ? "PASS" : "FAIL") << "\",\n";
    stream << "  \"failure_reason\": \"" << json_escape(result.failure_reason) << "\"\n";
    stream << "}\n";
    return stream.str();
}

bool row_pointer(std::uint8_t *base, std::ptrdiff_t stride, std::uint32_t row, std::uint8_t *&result) {
    if (base == nullptr || stride == 0) {
        return false;
    }
    if (row == 0u) {
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

bool write_pixel(nozzle::mapped_pixels &pixels, std::uint32_t x, std::uint32_t y, std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha) {
    if (pixels.data == nullptr || pixels.width <= x || pixels.height <= y) {
        return false;
    }
    std::uint8_t *row = nullptr;
    if (!row_pointer(static_cast<std::uint8_t *>(pixels.data), pixels.row_stride_bytes, y, row)) {
        return false;
    }
    std::uint8_t *destination = row + (static_cast<std::size_t>(x) * 4u);
    if (pixels.format == nozzle::texture_format::rgba8_unorm || pixels.format == nozzle::texture_format::rgba8_srgb || pixels.cpu_layout.order == nozzle::channel_order::rgba) {
        destination[0] = red;
        destination[1] = green;
        destination[2] = blue;
        destination[3] = alpha;
        return true;
    }
    if (pixels.format == nozzle::texture_format::bgra8_unorm || pixels.format == nozzle::texture_format::bgra8_srgb || pixels.cpu_layout.order == nozzle::channel_order::bgra) {
        destination[0] = blue;
        destination[1] = green;
        destination[2] = red;
        destination[3] = alpha;
        return true;
    }
    return false;
}

bool fill_rect(nozzle::mapped_pixels &pixels, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha) {
    if (pixels.width <= x || pixels.height <= y) {
        return true;
    }
    const std::uint32_t clamped_width = std::min<std::uint32_t>(width, pixels.width - x);
    const std::uint32_t clamped_height = std::min<std::uint32_t>(height, pixels.height - y);
    const std::uint32_t end_x = x + clamped_width;
    const std::uint32_t end_y = y + clamped_height;
    for (std::uint32_t row = y; row < end_y; row = row + 1u) {
        for (std::uint32_t column = x; column < end_x; column = column + 1u) {
            if (!write_pixel(pixels, column, row, red, green, blue, alpha)) {
                return false;
            }
        }
    }
    return true;
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

bool validate_writable_pixels_layout(const nozzle::mapped_pixels &pixels) {
    if (pixels.data == nullptr || pixels.width == 0u || pixels.height == 0u) {
        return false;
    }
    std::uint64_t absolute_stride = 0u;
    if (!safe_abs_stride(pixels.row_stride_bytes, absolute_stride)) {
        return false;
    }
    return pixels.width * 4u <= absolute_stride;
}

bool draw_sender_pattern(nozzle::mapped_pixels &pixels, std::uint64_t frame_index) {
    if (!validate_writable_pixels_layout(pixels)) {
        return false;
    }
    if (!fill_rect(pixels, 0u, 0u, pixels.width, pixels.height, 5u, 5u, 5u, 255u)) {
        return false;
    }
    const std::uint32_t corner_width = std::max<std::uint32_t>(32u, pixels.width / 3u);
    const std::uint32_t corner_height = std::max<std::uint32_t>(32u, pixels.height / 3u);
    if (!fill_rect(pixels, 0u, 0u, corner_width, corner_height, 255u, 0u, 0u, 255u)) {
        return false;
    }
    if (!fill_rect(pixels, pixels.width - corner_width, 0u, corner_width, corner_height, 0u, 255u, 0u, 255u)) {
        return false;
    }
    if (!fill_rect(pixels, 0u, pixels.height - corner_height, corner_width, corner_height, 0u, 0u, 255u, 255u)) {
        return false;
    }
    if (!fill_rect(pixels, pixels.width - corner_width, pixels.height - corner_height, corner_width, corner_height, 255u, 255u, 255u, 255u)) {
        return false;
    }
    const std::uint32_t alpha_width = std::max<std::uint32_t>(24u, pixels.width / 7u);
    const std::uint32_t alpha_height = std::max<std::uint32_t>(16u, pixels.height / 8u);
    const std::uint32_t alpha_x = (pixels.width / 2u) - (alpha_width / 2u);
    const std::uint32_t alpha_y = ((pixels.height / 2u) - (pixels.height / 16u)) - (alpha_height / 2u);
    if (!fill_rect(pixels, alpha_x, alpha_y, alpha_width, alpha_height, 255u, 0u, 255u, 64u)) {
        return false;
    }
    const std::uint32_t marker_width = 24u;
    const std::uint32_t marker_height = 32u;
    const std::uint32_t marker_y = 144u;
    const std::uint32_t travel_width = pixels.width <= marker_width ? 1u : pixels.width - marker_width;
    const std::uint32_t marker_x = static_cast<std::uint32_t>((frame_index * 29u) % travel_width) + 12u;
    return fill_rect(pixels, marker_x, marker_y, marker_width, marker_height, 255u, 255u, 0u, 255u);
}

int run_smoke_sender(const smoke_options &options) {
    smoke_sender_result result{};
    std::string validation_failure{};
    if (!nozzle_viewer::validate_smoke_dimensions(options.width, options.height, true, true, validation_failure)) {
        result.failure_reason = validation_failure;
        nozzle::sender_info empty_info{};
        const std::string json = make_sender_evidence_json(options, result, empty_info);
        if (!write_evidence(options.evidence_path, json)) {
            std::fprintf(stderr, "failed to write evidence: %s\n", options.evidence_path.c_str());
        }
        return 1;
    }

    nozzle::sender_desc sender_desc{};
    sender_desc.name = options.source_name;
    sender_desc.application_name = "nozzle-viewer";
    sender_desc.ring_buffer_size = 3u;

    auto sender_result = nozzle::sender::create(sender_desc);
    if (!sender_result) {
        result.failure_reason = "sender_create_failed:" + sender_result.error().message;
        nozzle::sender_info empty_info{};
        const std::string json = make_sender_evidence_json(options, result, empty_info);
        write_evidence(options.evidence_path, json);
        std::fprintf(stderr, "nozzle-viewer smoke sender FAIL: %s\n", result.failure_reason.c_str());
        return 1;
    }

    nozzle::sender sender = std::move(sender_result.value());
    nozzle::sender_info sender_info = sender.info();
    nozzle::texture_desc texture_desc{};
    texture_desc.width = options.width;
    texture_desc.height = options.height;
    texture_desc.format = nozzle::texture_format::bgra8_unorm;
    texture_desc.semantic_format = nozzle::texture_format::rgba8_unorm;
    texture_desc.usage = nozzle::texture_usage::shader_read | nozzle::texture_usage::shared;

    for (std::uint64_t frame_index = 0u; frame_index < options.frame_count; frame_index = frame_index + 1u) {
        auto frame_result = sender.acquire_writable_frame(texture_desc);
        if (!frame_result) {
            result.failure_reason = "acquire_writable_frame_failed:" + frame_result.error().message;
            break;
        }
        nozzle::writable_frame frame = std::move(frame_result.value());
        auto mapping_result = nozzle::lock_writable_pixels_mapping_with_origin(frame, nozzle::texture_origin::top_left);
        if (!mapping_result) {
            result.failure_reason = "lock_writable_pixels_failed:" + mapping_result.error().message;
            sender.discard_frame(frame);
            break;
        }
        nozzle::pixel_mapping mapping = std::move(mapping_result.value());
        nozzle::mapped_pixels pixels = mapping.pixels();
        result.observed_width = pixels.width;
        result.observed_height = pixels.height;
        result.mapped_format = static_cast<int>(pixels.format);
        result.mapped_row_stride_bytes = static_cast<std::int64_t>(pixels.row_stride_bytes);
        if (!draw_sender_pattern(pixels, frame_index)) {
            mapping.unlock();
            sender.discard_frame(frame);
            result.failure_reason = "draw_sender_pattern_failed";
            break;
        }
        auto unlock_result = mapping.unlock_checked();
        if (!unlock_result) {
            sender.discard_frame(frame);
            result.failure_reason = "unlock_writable_pixels_failed:" + unlock_result.error().message;
            break;
        }
        auto commit_result = sender.commit_frame(frame);
        if (!commit_result) {
            result.failure_reason = "commit_frame_failed:" + commit_result.error().message;
            break;
        }
        result.published_count = result.published_count + 1u;
        std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));
    }

    result.passed = result.published_count == options.frame_count;
    if (result.passed) {
        result.failure_reason.clear();
    } else if (result.failure_reason.empty()) {
        result.failure_reason = "published_count_mismatch";
    }
    const std::string json = make_sender_evidence_json(options, result, sender_info);
    if (!write_evidence(options.evidence_path, json)) {
        std::fprintf(stderr, "failed to write evidence: %s\n", options.evidence_path.c_str());
        return 1;
    }
    if (result.passed) {
        std::printf("nozzle-viewer smoke sender PASS %ux%u frames=%llu source=%s\n", options.width, options.height, static_cast<unsigned long long>(result.published_count), options.source_name.c_str());
        return 0;
    }
    std::fprintf(stderr, "nozzle-viewer smoke sender FAIL: %s\n", result.failure_reason.c_str());
    return 1;
}

int run_smoke_receiver(const smoke_options &options) {
    smoke_result result{};
    result.timeout_ms = options.timeout_ms;

    std::string validation_failure{};
    if (!nozzle_viewer::validate_smoke_dimensions(options.width, options.height, options.expect_alpha_patch, options.expect_moving_marker, validation_failure)) {
        result.failure_reason = validation_failure;
        nozzle::connected_sender_info sender_info{};
        const std::string json = make_evidence_json(options, result, sender_info);
        if (!write_evidence(options.evidence_path, json)) {
            std::fprintf(stderr, "failed to write evidence: %s\n", options.evidence_path.c_str());
            return 1;
        }
        std::fprintf(stderr, "nozzle-viewer smoke receiver FAIL: %s\n", result.failure_reason.c_str());
        return 1;
    }

    nozzle::receiver_desc receiver_desc{};
    receiver_desc.name = options.source_name;
    receiver_desc.application_name = "nozzle-viewer smoke receiver";
    receiver_desc.receive_mode_val = nozzle::receive_mode::sequential_best_effort;

    std::unique_ptr<nozzle::receiver> receiver{};
    nozzle::connected_sender_info sender_info{};
    std::string last_create_error{};
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
        if (options.timeout_ms <= elapsed_ms) {
            result.failure_reason = receiver ? "timeout_waiting_for_frame" : "receiver_create_timeout:" + last_create_error;
            break;
        }

        if (!receiver) {
            auto receiver_result = nozzle::receiver::create(receiver_desc);
            if (!receiver_result) {
                last_create_error = receiver_result.error().message;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            receiver = std::make_unique<nozzle::receiver>(std::move(receiver_result.value()));
        }

        nozzle::acquire_desc acquire_desc{};
        acquire_desc.timeout_ms = 100u;
        auto frame_result = receiver->acquire_frame(acquire_desc);
        if (!frame_result) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        nozzle::frame frame = std::move(frame_result.value());
        const nozzle::frame_info info = frame.info();
        result.observed_width = info.width;
        result.observed_height = info.height;
        result.observed_frame_index = info.frame_index;
        result.observed_frame_count = result.observed_frame_count + 1u;
        if (result.observed_frame_indices.empty() || result.observed_frame_indices.back() != info.frame_index) {
            result.observed_frame_indices.push_back(info.frame_index);
            result.distinct_frame_count = static_cast<std::uint64_t>(result.observed_frame_indices.size());
        }
        result.dimensions_ok = info.width == options.width && info.height == options.height;
        sender_info = receiver->connected_info();

        if (!result.dimensions_ok) {
            result.failure_reason = "dimension_mismatch";
            break;
        }

        auto mapping_result = nozzle::lock_frame_pixels_mapping_with_origin(frame, nozzle::texture_origin::top_left);
        if (!mapping_result) {
            result.failure_reason = "lock_frame_pixels_failed:" + mapping_result.error().message;
            break;
        }
        nozzle::pixel_mapping mapping = std::move(mapping_result.value());
        const nozzle::mapped_pixels mapped_pixels = mapping.pixels();
        result.mapped_format = static_cast<int>(mapped_pixels.format);
        result.mapped_row_stride_bytes = static_cast<std::int64_t>(mapped_pixels.row_stride_bytes);
        nozzle_viewer::preview_image preview{};
        std::string conversion_error{};
        if (!nozzle_viewer::convert_to_rgba8_preview(mapped_pixels, preview, &conversion_error)) {
            mapping.unlock();
            result.failure_reason = "preview_conversion_failed:" + conversion_error;
            break;
        }
        mapping.unlock();

        result.distinct_frames_ok = options.min_frames <= result.distinct_frame_count;
        nozzle_viewer::smoke_oracle_options oracle_options{};
        oracle_options.expect_alpha_patch = options.expect_alpha_patch;
        oracle_options.expect_moving_marker = options.expect_moving_marker;
        oracle_options.min_frames = options.min_frames;
        nozzle_viewer::smoke_oracle_state oracle_state{};
        oracle_state.observed_marker_x = result.observed_marker_x;
        oracle_state.observed_marker_samples = result.observed_marker_samples;
        verify_smoke_pattern(preview, oracle_options, result.observed_frame_index, oracle_state);
        result.samples = oracle_state.samples;
        result.observed_marker_x = oracle_state.observed_marker_x;
        result.observed_marker_samples = oracle_state.observed_marker_samples;
        result.top_left_red = oracle_state.top_left_red;
        result.top_right_green = oracle_state.top_right_green;
        result.bottom_left_blue = oracle_state.bottom_left_blue;
        result.bottom_right_white = oracle_state.bottom_right_white;
        result.orientation_ok = oracle_state.orientation_ok;
        result.channel_order_ok = oracle_state.channel_order_ok;
        result.alpha_patch_ok = oracle_state.alpha_patch_ok;
        result.moving_marker_ok = oracle_state.moving_marker_ok;
        result.passed = result.dimensions_ok && result.orientation_ok && result.channel_order_ok && result.alpha_patch_ok && result.moving_marker_ok && result.distinct_frames_ok;
        if (result.passed) {
            result.failure_reason.clear();
            break;
        }
        result.failure_reason = !result.distinct_frames_ok ? "minimum_distinct_frame_count_not_reached" : "quadrant_semantics_failed";
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!result.passed && result.failure_reason.empty()) {
        result.failure_reason = "unknown_failure";
    }

    const std::string json = make_evidence_json(options, result, sender_info);
    if (!write_evidence(options.evidence_path, json)) {
        std::fprintf(stderr, "failed to write evidence: %s\n", options.evidence_path.c_str());
        return 1;
    }
    if (result.passed) {
        std::printf("nozzle-viewer smoke receiver PASS %ux%u frame=%llu source=%s\n", result.observed_width, result.observed_height, static_cast<unsigned long long>(result.observed_frame_index), options.source_name.c_str());
        return 0;
    }
    std::fprintf(stderr, "nozzle-viewer smoke receiver FAIL: %s\n", result.failure_reason.c_str());
    return 1;
}

} // namespace

int main(int argc, char **argv) {
    smoke_options options{};
    bool wants_smoke_receiver = false;
    bool wants_smoke_sender = false;
    bool wants_help = false;
    for (int index = 1; index < argc; index = index + 1) {
        if (std::strcmp(argv[index], "--smoke-receiver") == 0) {
            wants_smoke_receiver = true;
        } else if (std::strcmp(argv[index], "--smoke-sender") == 0) {
            wants_smoke_sender = true;
        } else if (std::strcmp(argv[index], "--help") == 0) {
            wants_help = true;
        }
    }
    if (wants_help) {
        print_usage();
        return 0;
    }
    if (!wants_smoke_receiver && !wants_smoke_sender) {
        nozzle_viewer::gui app;
        if (!app.init()) {
            std::fprintf(stderr, "failed to initialize nozzle-viewer\n");
            return 1;
        }
        app.run();
        return 0;
    }

    if (!parse_smoke_options(argc, argv, options)) {
        return 2;
    }
    if (options.sender_enabled) {
        return run_smoke_sender(options);
    }
    return run_smoke_receiver(options);
}
