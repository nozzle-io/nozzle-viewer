#pragma once

#include <app/source_registry.hpp>
#include <app/viewer_state.hpp>
#include <gui/preview_conversion.hpp>
#include <gui/render_backend.hpp>

#include <nozzle/receiver.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

namespace nozzle_viewer {

struct receiver_session {
    source_entry source{};
    std::unique_ptr<nozzle::receiver> receiver{};
    void *preview_texture{nullptr};
    preview_image preview_pixels{};
    std::uint32_t preview_width{0};
    std::uint32_t preview_height{0};
    nozzle::connected_sender_info connected_info{};
    std::string status{"not connected"};
    std::string error{};
    std::uint64_t last_frame_index{0};
    bool connected{false};
};

class gui {
public:
    gui();
    ~gui();

    gui(const gui &) = delete;
    gui &operator=(const gui &) = delete;

    bool init();
    void run();
    void shutdown();

private:
    void refresh_sources();
    void update_sessions();
    void reconcile_sessions();
    void connect_session(receiver_session &session);
    void acquire_preview(receiver_session &session);
    void draw_ui();
    void draw_toolbar();
    void draw_source_tile(receiver_session &session, float width, float height);
    void draw_empty_tile(const source_entry &source, float width, float height, const char *message);
    void destroy_session_texture(receiver_session &session);

    GLFWwindow *window_{nullptr};
    std::unique_ptr<render_backend> backend_{};
    source_registry registry_{};
    viewer_state state_{};
    std::unordered_map<std::string, receiver_session> sessions_{};
    std::chrono::steady_clock::time_point last_refresh_{};
    bool running_{false};
};

} // namespace nozzle_viewer
