#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>

struct GLFWwindow;
struct ImVec2;

namespace nozzle_viewer {

enum class preview_format {
    rgba8,
    bgra8,
};

class render_backend {
public:
    virtual ~render_backend() = default;

    virtual bool init(GLFWwindow *window) = 0;
    virtual void shutdown() = 0;
    virtual void begin_frame() = 0;
    virtual void end_frame() = 0;

    virtual void *create_preview_texture(std::uint32_t width, std::uint32_t height) = 0;
    virtual void destroy_preview_texture(void *texture) = 0;
    virtual bool update_preview_texture(
        void *texture,
        const void *pixels,
        std::uint32_t width,
        std::uint32_t height,
        std::ptrdiff_t row_stride_bytes,
        preview_format format) = 0;

    virtual const char *get_system_font_path() = 0;
};

std::unique_ptr<render_backend> create_render_backend();

} // namespace nozzle_viewer
