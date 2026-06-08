#include <gui/gui.hpp>
#include <gui/preview_conversion.hpp>

#include <nozzle/pixel_access.hpp>
#include <nozzle/receiver.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct smoke_options {
    bool enabled{false};
    std::string source_name{"juce_nozzle_app_smoke"};
    std::uint32_t width{320};
    std::uint32_t height{240};
    std::uint64_t timeout_ms{10000};
    std::string evidence_path{};
};

struct sample_result {
    std::string name{};
    std::uint32_t x{0};
    std::uint32_t y{0};
    std::uint8_t expected_r{0};
    std::uint8_t expected_g{0};
    std::uint8_t expected_b{0};
    std::uint8_t actual_r{0};
    std::uint8_t actual_g{0};
    std::uint8_t actual_b{0};
    bool passed{false};
};

struct smoke_result {
    bool passed{false};
    std::string failure_reason{};
    std::uint32_t observed_width{0};
    std::uint32_t observed_height{0};
    std::uint64_t observed_frame_index{0};
    std::uint64_t observed_frame_count{0};
    std::uint64_t timeout_ms{0};
    bool dimensions_ok{false};
    bool top_left_red{false};
    bool top_right_green{false};
    bool bottom_left_blue{false};
    bool bottom_right_white{false};
    bool orientation_ok{false};
    bool channel_order_ok{false};
    std::vector<sample_result> samples{};
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
        } else if (std::strcmp(arg, "--help") == 0) {
            std::printf("Usage: nozzle-viewer [--smoke-receiver --source NAME --width N --height N --timeout-ms N --evidence PATH]\n");
            return false;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", arg);
            return false;
        }
    }
    return true;
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

bool is_color(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t expected_r, std::uint8_t expected_g, std::uint8_t expected_b) {
    return r == expected_r && g == expected_g && b == expected_b;
}

sample_result sample_pixel(
    const nozzle_viewer::preview_image &image,
    const std::string &name,
    std::uint32_t x,
    std::uint32_t y,
    std::uint8_t expected_r,
    std::uint8_t expected_g,
    std::uint8_t expected_b) {
    sample_result result{};
    result.name = name;
    result.x = x;
    result.y = y;
    result.expected_r = expected_r;
    result.expected_g = expected_g;
    result.expected_b = expected_b;
    const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4u;
    result.actual_r = image.pixels[offset + 0u];
    result.actual_g = image.pixels[offset + 1u];
    result.actual_b = image.pixels[offset + 2u];
    result.passed = is_color(result.actual_r, result.actual_g, result.actual_b, expected_r, expected_g, expected_b);
    return result;
}

void verify_quadrants(const nozzle_viewer::preview_image &image, smoke_result &result) {
    const std::uint32_t left_x = image.width / 8u;
    const std::uint32_t right_x = image.width - 1u - image.width / 8u;
    const std::uint32_t top_y = image.height / 8u;
    const std::uint32_t bottom_y = image.height - 1u - image.height / 8u;

    result.samples.push_back(sample_pixel(image, "top_left_red", left_x, top_y, 255u, 0u, 0u));
    result.samples.push_back(sample_pixel(image, "top_right_green", right_x, top_y, 0u, 255u, 0u));
    result.samples.push_back(sample_pixel(image, "bottom_left_blue", left_x, bottom_y, 0u, 0u, 255u));
    result.samples.push_back(sample_pixel(image, "bottom_right_white", right_x, bottom_y, 255u, 255u, 255u));

    result.top_left_red = result.samples[0].passed;
    result.top_right_green = result.samples[1].passed;
    result.bottom_left_blue = result.samples[2].passed;
    result.bottom_right_white = result.samples[3].passed;
    result.orientation_ok = result.top_left_red && result.top_right_green && result.bottom_left_blue && result.bottom_right_white;
    result.channel_order_ok = result.top_left_red && result.bottom_left_blue;
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
    stream << "    \"timeout_ms\": " << result.timeout_ms << "\n";
    stream << "  },\n";
    stream << "  \"checks\": {\n";
    stream << "    \"dimensions\": \"" << bool_check(result.dimensions_ok) << "\",\n";
    stream << "    \"top_left_red\": \"" << bool_check(result.top_left_red) << "\",\n";
    stream << "    \"top_right_green\": \"" << bool_check(result.top_right_green) << "\",\n";
    stream << "    \"bottom_left_blue\": \"" << bool_check(result.bottom_left_blue) << "\",\n";
    stream << "    \"bottom_right_white\": \"" << bool_check(result.bottom_right_white) << "\",\n";
    stream << "    \"orientation\": \"" << bool_check(result.orientation_ok) << "\",\n";
    stream << "    \"channel_order\": \"" << bool_check(result.channel_order_ok) << "\"\n";
    stream << "  },\n";
    stream << "  \"samples\": [\n";
    for (std::size_t index = 0; index < result.samples.size(); index = index + 1u) {
        const sample_result &sample = result.samples[index];
        stream << "    {\"name\":\"" << json_escape(sample.name) << "\",\"x\":" << sample.x << ",\"y\":" << sample.y;
        stream << ",\"expected_rgb\":[" << static_cast<int>(sample.expected_r) << "," << static_cast<int>(sample.expected_g) << "," << static_cast<int>(sample.expected_b) << "]";
        stream << ",\"actual_rgb\":[" << static_cast<int>(sample.actual_r) << "," << static_cast<int>(sample.actual_g) << "," << static_cast<int>(sample.actual_b) << "]";
        stream << ",\"passed\":" << (sample.passed ? "true" : "false") << "}";
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

    nozzle::receiver_desc receiver_desc{};
    receiver_desc.name = options.source_name;
    receiver_desc.application_name = "nozzle-viewer smoke receiver";
    receiver_desc.receive_mode_val = nozzle::receive_mode::sequential_best_effort;

    auto receiver_result = nozzle::receiver::create(receiver_desc);
    if (!receiver_result) {
        result.failure_reason = "receiver_create_failed:" + receiver_result.error().message;
        const nozzle::connected_sender_info empty_info{};
        write_evidence(options.evidence_path, make_evidence_json(options, result, empty_info));
        std::fprintf(stderr, "%s\n", result.failure_reason.c_str());
        return 1;
    }

    nozzle::receiver receiver = std::move(receiver_result.value());
    nozzle::connected_sender_info sender_info{};
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
        if (options.timeout_ms <= elapsed_ms) {
            result.failure_reason = "timeout_waiting_for_frame";
            break;
        }

        nozzle::acquire_desc acquire_desc{};
        acquire_desc.timeout_ms = 100u;
        auto frame_result = receiver.acquire_frame(acquire_desc);
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
        result.dimensions_ok = info.width == options.width && info.height == options.height;
        sender_info = receiver.connected_info();

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
        nozzle_viewer::preview_image preview{};
        std::string conversion_error{};
        if (!nozzle_viewer::convert_to_rgba8_preview(mapping.pixels(), preview, &conversion_error)) {
            mapping.unlock();
            result.failure_reason = "preview_conversion_failed:" + conversion_error;
            break;
        }
        mapping.unlock();

        verify_quadrants(preview, result);
        result.passed = result.dimensions_ok && result.orientation_ok && result.channel_order_ok;
        if (!result.passed) {
            result.failure_reason = "quadrant_semantics_failed";
            break;
        }
        break;
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
    if (!parse_smoke_options(argc, argv, options)) {
        return 2;
    }
    if (options.enabled) {
        return run_smoke_receiver(options);
    }

    nozzle_viewer::gui app;
    if (!app.init()) {
        std::fprintf(stderr, "failed to initialize nozzle-viewer\n");
        return 1;
    }
    app.run();
    return 0;
}
