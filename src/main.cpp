#include <gui/gui.hpp>
#include <gui/preview_conversion.hpp>
#include <app/smoke_oracle.hpp>

#include <nozzle/pixel_access.hpp>
#include <nozzle/receiver.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct smoke_options {
    bool enabled{false};
    bool expect_alpha_patch{false};
    bool expect_moving_marker{false};
    std::string source_name{"juce_nozzle_app_smoke"};
    std::uint32_t width{320};
    std::uint32_t height{240};
    std::uint64_t min_frames{1};
    std::uint64_t timeout_ms{10000};
    std::string evidence_path{};
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
            options.enabled = true;
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
            return false;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", arg);
            return false;
        }
    }
    return true;
}

void print_usage() {
    std::printf("Usage: nozzle-viewer [--help]\n");
    std::printf("       nozzle-viewer --smoke-receiver --source NAME --width N --height N --min-frames N --timeout-ms N --evidence PATH [--expect-alpha-patch] [--expect-moving-marker]\n");
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
    bool wants_help = false;
    for (int index = 1; index < argc; index = index + 1) {
        if (std::strcmp(argv[index], "--smoke-receiver") == 0) {
            wants_smoke_receiver = true;
        } else if (std::strcmp(argv[index], "--help") == 0) {
            wants_help = true;
        }
    }
    if (wants_help) {
        print_usage();
        return 0;
    }
    if (!wants_smoke_receiver) {
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
    return run_smoke_receiver(options);
}
