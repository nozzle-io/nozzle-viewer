#include <app/source_registry.hpp>
#include <app/viewer_state.hpp>
#include <gui/preview_conversion.hpp>

#include <nozzle/format_resolve.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace nozzle_viewer;

namespace {

void test_sender_snapshots_are_sorted_and_deduplicated() {
    std::vector<nozzle::sender_info> senders{
        {"zeta", "app", "2", nozzle::backend_type::metal},
        {"alpha", "app", "1", nozzle::backend_type::d3d11},
        {"alpha", "app", "1", nozzle::backend_type::d3d11},
    };

    auto entries = to_source_entries(senders);
    assert(entries.size() == 2);
    assert(entries[0].name == "alpha");
    assert(entries[1].name == "zeta");
}

void test_source_registry_tracks_generation_and_lookup() {
    source_registry registry;
    registry.set_sources({{"camera", "app", "abc", nozzle::backend_type::opengl}});

    assert(registry.generation() == 1);
    assert(registry.find_by_id_or_name("abc") != nullptr);
    assert(registry.find_by_id_or_name("camera") != nullptr);
    assert(registry.find_by_id_or_name("missing") == nullptr);
}

void test_viewer_state_keeps_a_valid_focus() {
    source_registry registry;
    registry.set_sources({{"one", "app", "id-one", nozzle::backend_type::metal}});

    viewer_state state;
    state.reconcile_focus(registry);
    assert(state.focused_key() == "id-one");

    state.set_mode(display_mode::single);
    registry.set_sources({});
    state.reconcile_focus(registry);
    assert(state.focused_key().empty());
    assert(state.mode() == display_mode::grid);
}

void test_backend_and_format_names_are_stable() {
    assert(std::string(backend_name(nozzle::backend_type::dma_buf)) == "DMA-BUF");
    assert(std::string(format_name(nozzle::texture_format::rgba8_unorm)) == "rgba8_unorm");
    assert(std::string(format_name(nozzle::texture_format::unknown)) == "unknown");
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
    assert(false);
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
    assert(convert_to_rgba8_preview(pixels, preview, &error));
    assert(error.empty());
    return preview;
}

void assert_pixel(const preview_image &image, std::array<std::uint8_t, 4> expected) {
    assert(image.width == 1);
    assert(image.height == 1);
    assert(image.row_stride_bytes == 4);
    assert(image.pixels.size() == 4);
    for (std::size_t index = 0; index < expected.size(); index = index + 1) {
        assert(image.pixels[index] == expected[index]);
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
        assert(bytes_per_pixel != 0);
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
        assert(is_known_preview_format(format));
        assert(convert_to_rgba8_preview(pixels, preview, &error));
        assert(error.empty());
        assert(preview.pixels.size() == 4);
    }

    assert(!is_known_preview_format(nozzle::texture_format::unknown));
}

void test_preview_converter_preserves_bgra_order() {
    const std::vector<std::uint8_t> source{10, 20, 30, 40};
    assert_pixel(convert_one_pixel(nozzle::texture_format::bgra8_unorm, source), {30, 20, 10, 40});
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
    assert(convert_to_rgba8_preview(pixels, preview, &error));
    assert(error.empty());
    const std::vector<std::uint8_t> expected{
        1, 0, 0, 255, 2, 0, 0, 255,
        3, 0, 0, 255, 4, 0, 0, 255,
    };
    assert(preview.width == 2);
    assert(preview.height == 2);
    assert(preview.row_stride_bytes == 8);
    assert(preview.pixels == expected);
}

void test_preview_converter_converts_unorm16() {
    std::vector<std::uint8_t> source;
    append_value<std::uint16_t>(source, 0);
    append_value<std::uint16_t>(source, 32768);
    append_value<std::uint16_t>(source, 65535);
    append_value<std::uint16_t>(source, 65535);
    assert_pixel(convert_one_pixel(nozzle::texture_format::rgba16_unorm, source), {0, 128, 255, 255});
}

void test_preview_converter_converts_float_formats() {
    std::vector<std::uint8_t> half_source;
    append_value<std::uint16_t>(half_source, half_bits(0.0f));
    append_value<std::uint16_t>(half_source, half_bits(0.5f));
    append_value<std::uint16_t>(half_source, half_bits(1.0f));
    append_value<std::uint16_t>(half_source, half_bits(1.0f));
    assert_pixel(convert_one_pixel(nozzle::texture_format::rgba16_float, half_source), {0, 128, 255, 255});

    std::vector<std::uint8_t> float_source;
    append_value<float>(float_source, -1.0f);
    append_value<float>(float_source, 0.5f);
    append_value<float>(float_source, 2.0f);
    append_value<float>(float_source, 1.0f);
    assert_pixel(convert_one_pixel(nozzle::texture_format::rgba32_float, float_source), {0, 128, 255, 255});

    std::vector<std::uint8_t> nan_source;
    append_value<float>(nan_source, std::numeric_limits<float>::quiet_NaN());
    assert_pixel(convert_one_pixel(nozzle::texture_format::r32_float, nan_source), {255, 0, 255, 255});

    std::vector<std::uint8_t> alpha_nan_source;
    append_value<float>(alpha_nan_source, 0.25f);
    append_value<float>(alpha_nan_source, 0.5f);
    append_value<float>(alpha_nan_source, 0.75f);
    append_value<float>(alpha_nan_source, std::numeric_limits<float>::quiet_NaN());
    assert_pixel(convert_one_pixel(nozzle::texture_format::rgba32_float, alpha_nan_source), {64, 128, 191, 255});

    std::vector<std::uint8_t> inf_source;
    append_value<float>(inf_source, -std::numeric_limits<float>::infinity());
    append_value<float>(inf_source, 0.5f);
    append_value<float>(inf_source, std::numeric_limits<float>::infinity());
    append_value<float>(inf_source, 1.0f);
    assert_pixel(convert_one_pixel(nozzle::texture_format::rgba32_float, inf_source), {0, 128, 255, 255});
}

void test_preview_converter_converts_uint_and_depth_formats() {
    std::vector<std::uint8_t> uint_source;
    append_value<std::uint32_t>(uint_source, 0x12345678u);
    append_value<std::uint32_t>(uint_source, 0x00000001u);
    append_value<std::uint32_t>(uint_source, 0x000000ffu);
    append_value<std::uint32_t>(uint_source, 0x00000102u);
    assert_pixel(convert_one_pixel(nozzle::texture_format::rgba32_uint, uint_source), {0x78, 0x01, 0xff, 0x02});

    std::vector<std::uint8_t> depth_source;
    append_value<float>(depth_source, 0.5f);
    assert_pixel(convert_one_pixel(nozzle::texture_format::depth32_float, depth_source), {128, 128, 128, 255});
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
    assert(!convert_to_rgba8_preview(pixels, preview, &error));
    assert(error == "unsupported preview format");
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
    return 0;
}
