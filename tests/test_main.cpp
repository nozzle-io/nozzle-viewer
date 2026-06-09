#include <app/source_registry.hpp>
#include <app/viewer_state.hpp>
#include <gui/preview_conversion.hpp>
#include <app/smoke_oracle.hpp>

#include <nozzle/format_resolve.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace nozzle_viewer;

namespace {

[[noreturn]] void fail_check(const char *condition, const char *file, int line) {
    std::fprintf(stderr, "check failed: %s:%d: %s\n", file, line, condition);
    std::exit(1);
}

void check(bool condition, const char *condition_text, const char *file, int line) {
    if (!condition) {
        fail_check(condition_text, file, line);
    }
}

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

void test_sender_snapshots_are_sorted_and_deduplicated() {
    std::vector<nozzle::sender_info> senders{
        {"zeta", "app", "2", nozzle::backend_type::metal},
        {"alpha", "app", "1", nozzle::backend_type::d3d11},
        {"alpha", "app", "1", nozzle::backend_type::d3d11},
    };

    auto entries = to_source_entries(senders);
    CHECK(entries.size() == 2);
    CHECK(entries[0].name == "alpha");
    CHECK(entries[1].name == "zeta");
}

void test_source_registry_tracks_generation_and_lookup() {
    source_registry registry;
    registry.set_sources({{"camera", "app", "abc", nozzle::backend_type::opengl}});

    CHECK(registry.generation() == 1);
    CHECK(registry.find_by_id_or_name("abc") != nullptr);
    CHECK(registry.find_by_id_or_name("camera") != nullptr);
    CHECK(registry.find_by_id_or_name("missing") == nullptr);
}

void test_viewer_state_keeps_a_valid_focus() {
    source_registry registry;
    registry.set_sources({{"one", "app", "id-one", nozzle::backend_type::metal}});

    viewer_state state;
    state.reconcile_focus(registry);
    CHECK(state.focused_key() == "id-one");

    state.set_mode(display_mode::single);
    registry.set_sources({});
    state.reconcile_focus(registry);
    CHECK(state.focused_key().empty());
    CHECK(state.mode() == display_mode::grid);
}

void test_backend_and_format_names_are_stable() {
    CHECK(std::string(backend_name(nozzle::backend_type::dma_buf)) == "DMA-BUF");
    CHECK(std::string(format_name(nozzle::texture_format::rgba8_unorm)) == "rgba8_unorm");
    CHECK(std::string(format_name(nozzle::texture_format::unknown)) == "unknown");
}

template <typename T>
void append_value(std::vector<std::uint8_t> &bytes, T value) {
    const auto old_size = bytes.size();
    bytes.resize(old_size + sizeof(T));
    std::memcpy(bytes.data() + old_size, &value, sizeof(T));
}

std::uint16_t half_bits(float value) {
    if (value == 0.0f) {
        return std::signbit(value) ? 0x8000u : 0x0000u;
    }
    if (value == 0.5f) {
        return 0x3800u;
    }
    if (value == 1.0f) {
        return 0x3c00u;
    }
    if (value == 2.0f) {
        return 0x4000u;
    }
    CHECK(false);
    return 0;
}

preview_image convert_one_pixel(nozzle::texture_format format, const std::vector<std::uint8_t> &source_bytes) {
    nozzle::mapped_pixels pixels{};
    pixels.data = const_cast<std::uint8_t *>(source_bytes.data());
    pixels.row_stride_bytes = static_cast<std::ptrdiff_t>(source_bytes.size());
    pixels.width = 1;
    pixels.height = 1;
    pixels.format = format;
    pixels.origin = nozzle::texture_origin::top_left;
    pixels.cpu_layout = nozzle::resolve_cpu_layout(format);

    preview_image preview;
    std::string error;
    CHECK(convert_to_rgba8_preview(pixels, preview, &error));
    CHECK(error.empty());
    return preview;
}

void check_pixel(const preview_image &image, std::array<std::uint8_t, 4> expected) {
    CHECK(image.width == 1);
    CHECK(image.height == 1);
    CHECK(image.row_stride_bytes == 4);
    CHECK(image.pixels.size() == 4);
    for (std::size_t index = 0; index < expected.size(); index = index + 1) {
        CHECK(image.pixels[index] == expected[index]);
    }
}

void test_preview_converter_accepts_every_known_format() {
    const std::vector<nozzle::texture_format> formats{
        nozzle::texture_format::r8_unorm,
        nozzle::texture_format::rg8_unorm,
        nozzle::texture_format::rgb8_unorm,
        nozzle::texture_format::rgba8_unorm,
        nozzle::texture_format::bgra8_unorm,
        nozzle::texture_format::rgba8_srgb,
        nozzle::texture_format::bgra8_srgb,
        nozzle::texture_format::r16_unorm,
        nozzle::texture_format::rg16_unorm,
        nozzle::texture_format::rgb16_unorm,
        nozzle::texture_format::rgba16_unorm,
        nozzle::texture_format::r16_float,
        nozzle::texture_format::rg16_float,
        nozzle::texture_format::rgb16_float,
        nozzle::texture_format::rgba16_float,
        nozzle::texture_format::r32_float,
        nozzle::texture_format::rg32_float,
        nozzle::texture_format::rgb32_float,
        nozzle::texture_format::rgba32_float,
        nozzle::texture_format::r32_uint,
        nozzle::texture_format::rgba32_uint,
        nozzle::texture_format::rgb32_uint,
        nozzle::texture_format::depth32_float,
    };

    for (auto format : formats) {
        const auto bytes_per_pixel = nozzle::resolve_bytes_per_pixel(format);
        CHECK(bytes_per_pixel != 0);
        std::vector<std::uint8_t> bytes(bytes_per_pixel, 0);
        nozzle::mapped_pixels pixels{};
        pixels.data = bytes.data();
        pixels.row_stride_bytes = bytes_per_pixel;
        pixels.width = 1;
        pixels.height = 1;
        pixels.format = format;
        pixels.origin = nozzle::texture_origin::top_left;
        pixels.cpu_layout = nozzle::resolve_cpu_layout(format);
        preview_image preview;
        std::string error;
        CHECK(is_known_preview_format(format));
        CHECK(convert_to_rgba8_preview(pixels, preview, &error));
        CHECK(error.empty());
        CHECK(preview.pixels.size() == 4);
    }

    CHECK(!is_known_preview_format(nozzle::texture_format::unknown));
}

void test_preview_converter_preserves_bgra_order() {
    const std::vector<std::uint8_t> source{10, 20, 30, 40};
    check_pixel(convert_one_pixel(nozzle::texture_format::bgra8_unorm, source), {30, 20, 10, 40});
}

void test_preview_converter_expands_padded_rows() {
    std::vector<std::uint8_t> source{
        1, 2, 99, 99,
        3, 4, 99, 99,
    };
    nozzle::mapped_pixels pixels{};
    pixels.data = source.data();
    pixels.row_stride_bytes = 4;
    pixels.width = 2;
    pixels.height = 2;
    pixels.format = nozzle::texture_format::r8_unorm;
    pixels.origin = nozzle::texture_origin::top_left;
    pixels.cpu_layout = nozzle::resolve_cpu_layout(pixels.format);

    preview_image preview;
    std::string error;
    CHECK(convert_to_rgba8_preview(pixels, preview, &error));
    CHECK(error.empty());
    const std::vector<std::uint8_t> expected{
        1, 0, 0, 255, 2, 0, 0, 255,
        3, 0, 0, 255, 4, 0, 0, 255,
    };
    CHECK(preview.width == 2);
    CHECK(preview.height == 2);
    CHECK(preview.row_stride_bytes == 8);
    CHECK(preview.pixels == expected);
}

void test_preview_converter_converts_unorm16() {
    std::vector<std::uint8_t> source;
    append_value<std::uint16_t>(source, 0);
    append_value<std::uint16_t>(source, 32768);
    append_value<std::uint16_t>(source, 65535);
    append_value<std::uint16_t>(source, 65535);
    check_pixel(convert_one_pixel(nozzle::texture_format::rgba16_unorm, source), {0, 128, 255, 255});
}

void test_preview_converter_converts_float_formats() {
    std::vector<std::uint8_t> half_source;
    append_value<std::uint16_t>(half_source, half_bits(0.0f));
    append_value<std::uint16_t>(half_source, half_bits(0.5f));
    append_value<std::uint16_t>(half_source, half_bits(1.0f));
    append_value<std::uint16_t>(half_source, half_bits(1.0f));
    check_pixel(convert_one_pixel(nozzle::texture_format::rgba16_float, half_source), {0, 128, 255, 255});

    std::vector<std::uint8_t> float_source;
    append_value<float>(float_source, -1.0f);
    append_value<float>(float_source, 0.5f);
    append_value<float>(float_source, 2.0f);
    append_value<float>(float_source, 1.0f);
    check_pixel(convert_one_pixel(nozzle::texture_format::rgba32_float, float_source), {0, 128, 255, 255});

    std::vector<std::uint8_t> nan_source;
    append_value<float>(nan_source, std::numeric_limits<float>::quiet_NaN());
    check_pixel(convert_one_pixel(nozzle::texture_format::r32_float, nan_source), {255, 0, 255, 255});

    std::vector<std::uint8_t> alpha_nan_source;
    append_value<float>(alpha_nan_source, 0.25f);
    append_value<float>(alpha_nan_source, 0.5f);
    append_value<float>(alpha_nan_source, 0.75f);
    append_value<float>(alpha_nan_source, std::numeric_limits<float>::quiet_NaN());
    check_pixel(convert_one_pixel(nozzle::texture_format::rgba32_float, alpha_nan_source), {64, 128, 191, 255});

    std::vector<std::uint8_t> inf_source;
    append_value<float>(inf_source, -std::numeric_limits<float>::infinity());
    append_value<float>(inf_source, 0.5f);
    append_value<float>(inf_source, std::numeric_limits<float>::infinity());
    append_value<float>(inf_source, 1.0f);
    check_pixel(convert_one_pixel(nozzle::texture_format::rgba32_float, inf_source), {0, 128, 255, 255});
}

void test_preview_converter_converts_uint_and_depth_formats() {
    std::vector<std::uint8_t> uint_source;
    append_value<std::uint32_t>(uint_source, 0x12345678u);
    append_value<std::uint32_t>(uint_source, 0x00000001u);
    append_value<std::uint32_t>(uint_source, 0x000000ffu);
    append_value<std::uint32_t>(uint_source, 0x00000102u);
    check_pixel(convert_one_pixel(nozzle::texture_format::rgba32_uint, uint_source), {0x78, 0x01, 0xff, 0x02});

    std::vector<std::uint8_t> depth_source;
    append_value<float>(depth_source, 0.5f);
    check_pixel(convert_one_pixel(nozzle::texture_format::depth32_float, depth_source), {128, 128, 128, 255});
}

void test_preview_converter_rejects_only_unknown_format() {
    std::vector<std::uint8_t> source{0, 0, 0, 0};
    nozzle::mapped_pixels pixels{};
    pixels.data = source.data();
    pixels.row_stride_bytes = 4;
    pixels.width = 1;
    pixels.height = 1;
    pixels.format = nozzle::texture_format::unknown;

    preview_image preview;
    std::string error;
    CHECK(!convert_to_rgba8_preview(pixels, preview, &error));
    CHECK(error == "unsupported preview format");
}

void test_preview_status_decision_matches_viewer_gate_regression() {
    CHECK(std::string(preview_status_after_conversion_failure(nozzle::texture_format::rgba16_float)) == "preview conversion failed");
    CHECK(std::string(preview_status_after_conversion_failure(nozzle::texture_format::rgba32_float)) == "preview conversion failed");
    CHECK(std::string(preview_status_after_conversion_failure(nozzle::texture_format::unknown)) == "unsupported preview format");
    CHECK(converted_preview_upload_format() == preview_format::rgba8);
}

void test_smoke_oracle_accepts_320x240_contract() {
    std::string failure_reason;
    CHECK(validate_smoke_dimensions(320, 240, true, true, failure_reason));
    CHECK(failure_reason.empty());
}

void test_smoke_oracle_rejects_too_small_marker_width() {
    std::string failure_reason;
    CHECK(!validate_smoke_dimensions(24, 240, false, true, failure_reason));
    CHECK(failure_reason.find("moving_marker_width_too_small") != std::string::npos);
}

void test_smoke_oracle_rejects_too_small_marker_height() {
    std::string failure_reason;
    CHECK(!validate_smoke_dimensions(320, 144, false, true, failure_reason));
    CHECK(failure_reason.find("moving_marker_height_too_small") != std::string::npos);
}

void test_smoke_oracle_reports_alpha_sample_out_of_range() {
    preview_image image{};
    image.width = 4;
    image.height = 4;
    image.row_stride_bytes = 16;
    image.pixels.assign(4u * 4u * 4u, 0u);

    const smoke_sample_result sample = sample_smoke_pixel(image, "center_magenta_alpha_patch", 4, 1, 255u, 0u, 255u, 64u);
    CHECK(!sample.passed);
    CHECK(sample.failure_reason.find("sample_out_of_bounds") != std::string::npos);
}

} // namespace

int main() {
    test_sender_snapshots_are_sorted_and_deduplicated();
    test_source_registry_tracks_generation_and_lookup();
    test_viewer_state_keeps_a_valid_focus();
    test_backend_and_format_names_are_stable();
    test_preview_converter_accepts_every_known_format();
    test_preview_converter_preserves_bgra_order();
    test_preview_converter_expands_padded_rows();
    test_preview_converter_converts_unorm16();
    test_preview_converter_converts_float_formats();
    test_preview_converter_converts_uint_and_depth_formats();
    test_preview_converter_rejects_only_unknown_format();
    test_preview_status_decision_matches_viewer_gate_regression();
    test_smoke_oracle_accepts_320x240_contract();
    test_smoke_oracle_rejects_too_small_marker_width();
    test_smoke_oracle_rejects_too_small_marker_height();
    test_smoke_oracle_reports_alpha_sample_out_of_range();
    return 0;
}
