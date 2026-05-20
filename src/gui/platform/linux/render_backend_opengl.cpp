#include <gui/render_backend.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

namespace nozzle_viewer {

class opengl_render_backend : public render_backend {
public:
    bool init(GLFWwindow *window) override {
        window_ = window;
        glfwMakeContextCurrent(window_);
        glfwSwapInterval(1);
        ImGui_ImplGlfw_InitForOpenGL(window_, true);
        ImGui_ImplOpenGL3_Init("#version 330 core");
        return true;
    }

    void shutdown() override {
        if (window_) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
        }
    }

    void begin_frame() override {
        ImGui_ImplGlfw_NewFrame();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
    }

    void end_frame() override {
        ImGui::Render();
        int fb_w = 0;
        int fb_h = 0;
        glfwGetFramebufferSize(window_, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window_);
    }

    void *create_preview_texture(std::uint32_t width, std::uint32_t height) override {
        GLuint tex_id = 0;
        glGenTextures(1, &tex_id);
        glBindTexture(GL_TEXTURE_2D, tex_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        return reinterpret_cast<void *>(static_cast<uintptr_t>(tex_id));
    }

    void destroy_preview_texture(void *texture) override {
        if (!texture) {
            return;
        }
        GLuint tex_id = static_cast<GLuint>(reinterpret_cast<uintptr_t>(texture));
        glDeleteTextures(1, &tex_id);
    }

    bool update_preview_texture(void *texture, const void *pixels, std::uint32_t width, std::uint32_t height,
        std::ptrdiff_t row_stride_bytes, preview_format format) override {
        if (!texture || !pixels || width == 0 || height == 0) {
            return false;
        }
        GLuint tex_id = static_cast<GLuint>(reinterpret_cast<uintptr_t>(texture));
        glBindTexture(GL_TEXTURE_2D, tex_id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (format == preview_format::rgba8 && row_stride_bytes == static_cast<std::ptrdiff_t>(width * 4)) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        } else {
            scratch_.resize(static_cast<std::size_t>(width) * height * 4);
            const auto *src_base = static_cast<const std::uint8_t *>(pixels);
            for (std::uint32_t y = 0; y < height; ++y) {
                const auto *src = src_base + static_cast<std::ptrdiff_t>(y) * row_stride_bytes;
                auto *dst = scratch_.data() + static_cast<std::size_t>(y) * width * 4;
                if (format == preview_format::bgra8) {
                    for (std::uint32_t x = 0; x < width; ++x) {
                        dst[x * 4 + 0] = src[x * 4 + 2];
                        dst[x * 4 + 1] = src[x * 4 + 1];
                        dst[x * 4 + 2] = src[x * 4 + 0];
                        dst[x * 4 + 3] = src[x * 4 + 3];
                    }
                } else {
                    std::memcpy(dst, src, static_cast<std::size_t>(width) * 4);
                }
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_RGBA, GL_UNSIGNED_BYTE, scratch_.data());
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        return true;
    }

    const char *get_system_font_path() override {
        static std::string cached;
        if (!cached.empty()) {
            return cached.c_str();
        }
        const char *candidates[] = {
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        };
        for (const char *path : candidates) {
            if (access(path, F_OK) == 0) {
                cached = path;
                return cached.c_str();
            }
        }
        return "";
    }

private:
    GLFWwindow *window_{nullptr};
    std::vector<std::uint8_t> scratch_{};
};

std::unique_ptr<render_backend> create_render_backend() {
    return std::make_unique<opengl_render_backend>();
}

} // namespace nozzle_viewer
