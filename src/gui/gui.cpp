#include <gui/gui.hpp>

#include <nozzle/pixel_access.hpp>

#include <imgui.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nozzle_viewer {

namespace {

constexpr float k_window_default_w = 1120.0f;
constexpr float k_window_default_h = 720.0f;
constexpr auto k_refresh_interval = std::chrono::milliseconds(1000);

std::string source_key(const source_entry &source) {
    return source.id.empty() ? source.name : source.id;
}

preview_format preview_format_from_nozzle(nozzle::texture_format format, bool *supported) {
    *supported = true;
    switch (format) {
    case nozzle::texture_format::rgba8_unorm:
    case nozzle::texture_format::rgba8_srgb:
        return preview_format::rgba8;
    case nozzle::texture_format::bgra8_unorm:
    case nozzle::texture_format::bgra8_srgb:
        return preview_format::bgra8;
    default:
        *supported = false;
        return preview_format::rgba8;
    }
}

} // namespace

gui::gui() = default;

gui::~gui() {
    shutdown();
}

bool gui::init() {
    if (!glfwInit()) {
        return false;
    }

    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    const int win_w = mode ? std::min(static_cast<int>(k_window_default_w), mode->width * 3 / 4) : static_cast<int>(k_window_default_w);
    const int win_h = mode ? std::min(static_cast<int>(k_window_default_h), mode->height * 3 / 4) : static_cast<int>(k_window_default_h);

#if !defined(__linux__)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#endif
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window_ = glfwCreateWindow(win_w, win_h, "nozzle-viewer", nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        return false;
    }

    if (mode) {
        int mon_x = 0;
        int mon_y = 0;
        glfwGetMonitorPos(monitor, &mon_x, &mon_y);
        glfwSetWindowPos(window_, mon_x + (mode->width - win_w) / 2, mon_y + (mode->height - win_h) / 2);
    }

    backend_ = create_render_backend();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImFontConfig font_cfg;
    font_cfg.OversampleH = 2;
    font_cfg.OversampleV = 1;
    const char *font_path = backend_->get_system_font_path();
    if (font_path && font_path[0] != '\0') {
        io.Fonts->AddFontFromFileTTF(font_path, 16.0f, &font_cfg, io.Fonts->GetGlyphRangesJapanese());
    } else {
        io.Fonts->AddFontDefault();
    }

    ImGui::StyleColorsDark();

    if (!backend_->init(window_)) {
        ImGui::DestroyContext();
        glfwDestroyWindow(window_);
        glfwTerminate();
        window_ = nullptr;
        return false;
    }

    refresh_sources();
    return true;
}

void gui::run() {
    running_ = true;
    while (running_ && window_ && !glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        int fb_w = 0;
        int fb_h = 0;
        glfwGetFramebufferSize(window_, &fb_w, &fb_h);
        if (fb_w <= 0 || fb_h <= 0) {
            continue;
        }

        update_sessions();
        backend_->begin_frame();
        draw_ui();
        backend_->end_frame();
    }
}

void gui::shutdown() {
    running_ = false;
    if (backend_) {
        for (auto &pair : sessions_) {
            destroy_session_texture(pair.second);
        }
        sessions_.clear();
    }

    if (window_) {
        backend_->shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window_);
        glfwTerminate();
        window_ = nullptr;
    }
}

void gui::refresh_sources() {
    registry_.refresh();
    state_.reconcile_focus(registry_);
    reconcile_sessions();
    last_refresh_ = std::chrono::steady_clock::now();
}

void gui::update_sessions() {
    const auto now = std::chrono::steady_clock::now();
    if (state_.auto_refresh() && now - last_refresh_ >= k_refresh_interval) {
        refresh_sources();
    }

    for (auto &pair : sessions_) {
        auto &session = pair.second;
        if (!session.connected) {
            connect_session(session);
        }
        if (session.connected) {
            acquire_preview(session);
        }
    }
}

void gui::reconcile_sessions() {
    for (const auto &source : registry_.sources()) {
        const auto key = source_key(source);
        auto it = sessions_.find(key);
        if (it == sessions_.end()) {
            receiver_session session{};
            session.source = source;
            sessions_.emplace(key, std::move(session));
        } else {
            it->second.source = source;
        }
    }

    for (auto it = sessions_.begin(); it != sessions_.end();) {
        const auto *source = registry_.find_by_id_or_name(it->first);
        if (!source) {
            destroy_session_texture(it->second);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

void gui::connect_session(receiver_session &session) {
    nozzle::receiver_desc desc{};
    desc.name = session.source.name;
    desc.application_name = "nozzle-viewer";

    auto result = nozzle::receiver::create(desc);
    if (!result.ok()) {
        session.connected = false;
        session.error = result.error().message;
        session.status = "receiver unavailable";
        return;
    }

    session.receiver = std::make_unique<nozzle::receiver>(std::move(result.value()));
    session.connected = true;
    session.connected_info = session.receiver->connected_info();
    session.status = "connected";
    session.error.clear();
}

void gui::acquire_preview(receiver_session &session) {
    if (!session.receiver || !session.receiver->valid()) {
        session.connected = false;
        session.status = "disconnected";
        return;
    }

    nozzle::acquire_desc desc{};
    desc.timeout_ms = 0;
    auto frame_result = session.receiver->acquire_frame(desc);
    if (!frame_result.ok()) {
        if (frame_result.error().code == nozzle::ErrorCode::Timeout) {
            session.status = "waiting for frame";
        } else {
            session.error = frame_result.error().message;
            session.status = "acquire failed";
            if (frame_result.error().code == nozzle::ErrorCode::SenderClosed ||
                frame_result.error().code == nozzle::ErrorCode::SenderNotFound) {
                session.connected = false;
                session.receiver.reset();
            }
        }
        return;
    }

    auto &frame = frame_result.value();
    auto info = frame.info();
    bool supported = false;
    const auto preview_fmt = preview_format_from_nozzle(info.format, &supported);
    if (!supported) {
        session.status = "unsupported preview format";
        session.error = format_name(info.format);
        session.connected_info = session.receiver->connected_info();
        session.last_frame_index = info.frame_index;
        return;
    }

    auto pixels_result = nozzle::lock_frame_pixels_with_origin(frame, nozzle::texture_origin::top_left);
    if (!pixels_result.ok()) {
        session.status = "pixel readback failed";
        session.error = pixels_result.error().message;
        session.connected_info = session.receiver->connected_info();
        session.last_frame_index = info.frame_index;
        return;
    }

    const auto &pixels = pixels_result.value();
    if (!session.preview_texture || session.preview_width != pixels.width || session.preview_height != pixels.height) {
        destroy_session_texture(session);
        session.preview_texture = backend_->create_preview_texture(pixels.width, pixels.height);
        session.preview_width = pixels.width;
        session.preview_height = pixels.height;
    }

    if (session.preview_texture) {
        if (backend_->update_preview_texture(session.preview_texture, pixels.data, pixels.width, pixels.height,
                pixels.row_stride_bytes, preview_fmt)) {
            session.status = "live";
            session.error.clear();
        } else {
            session.status = "texture upload failed";
        }
    } else {
        session.status = "texture allocation failed";
    }

    nozzle::unlock_frame_pixels(frame);
    session.connected_info = session.receiver->connected_info();
    session.last_frame_index = info.frame_index;
}

void gui::draw_ui() {
    int fb_w = 0;
    int fb_h = 0;
    glfwGetFramebufferSize(window_, &fb_w, &fb_h);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(fb_w), static_cast<float>(fb_h)));
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::Begin("nozzle-viewer", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    draw_toolbar();
    ImGui::Separator();

    if (registry_.sources().empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted("No nozzle sources found.");
        ImGui::TextDisabled("Start a nozzle sender, then press Refresh or enable Auto refresh.");
        ImGui::End();
        return;
    }

    const float content_w = ImGui::GetContentRegionAvail().x;
    const float content_h = ImGui::GetContentRegionAvail().y;

    if (state_.mode() == display_mode::single) {
        const auto *focused = registry_.find_by_id_or_name(state_.focused_key());
        if (focused) {
            auto it = sessions_.find(source_key(*focused));
            if (it != sessions_.end()) {
                draw_source_tile(it->second, content_w, content_h);
            }
        }
    } else {
        const int columns = std::max(1, static_cast<int>(content_w / 360.0f));
        const float tile_w = (content_w - ImGui::GetStyle().ItemSpacing.x * static_cast<float>(columns - 1)) / static_cast<float>(columns);
        const float tile_h = std::max(220.0f, tile_w * 0.62f);
        ImGui::Columns(columns, nullptr, false);
        for (const auto &source : registry_.sources()) {
            auto it = sessions_.find(source_key(source));
            if (it != sessions_.end()) {
                draw_source_tile(it->second, tile_w, tile_h);
            } else {
                draw_empty_tile(source, tile_w, tile_h, "not tracked");
            }
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
    }

    ImGui::End();
}

void gui::draw_toolbar() {
    if (ImGui::Button("Refresh")) {
        refresh_sources();
    }
    ImGui::SameLine();
    bool auto_refresh = state_.auto_refresh();
    if (ImGui::Checkbox("Auto refresh", &auto_refresh)) {
        state_.set_auto_refresh(auto_refresh);
    }
    ImGui::SameLine();
    if (ImGui::Button(state_.mode() == display_mode::grid ? "Single view" : "Grid view")) {
        state_.toggle_mode();
        state_.reconcile_focus(registry_);
    }
    ImGui::SameLine();
    ImGui::Text("Sources: %d", static_cast<int>(registry_.sources().size()));
}

void gui::draw_source_tile(receiver_session &session, float width, float height) {
    ImGui::PushID(source_key(session.source).c_str());
    ImGui::BeginChild("tile", ImVec2(width, height), true);

    ImGui::TextUnformatted(session.source.name.c_str());
    if (!session.source.application_name.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", session.source.application_name.c_str());
    }
    ImGui::TextDisabled("Backend: %s", backend_name(session.source.backend));

    if (ImGui::Button("Focus")) {
        state_.focus_source(session.source);
    }
    if (state_.mode() == display_mode::single) {
        ImGui::SameLine();
        if (ImGui::Button("Back to grid")) {
            state_.set_mode(display_mode::grid);
        }
    }

    const float meta_h = 96.0f;
    const float image_w = ImGui::GetContentRegionAvail().x;
    const float image_h = std::max(80.0f, ImGui::GetContentRegionAvail().y - meta_h);
    if (session.preview_texture && session.preview_width > 0 && session.preview_height > 0) {
        const float aspect = static_cast<float>(session.preview_width) / static_cast<float>(session.preview_height);
        float draw_w = image_w;
        float draw_h = draw_w / aspect;
        if (draw_h > image_h) {
            draw_h = image_h;
            draw_w = draw_h * aspect;
        }
        ImGui::Image(reinterpret_cast<ImTextureID>(session.preview_texture), ImVec2(draw_w, draw_h));
    } else {
        ImGui::Dummy(ImVec2(image_w, image_h));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - image_h * 0.55f);
        ImGui::TextDisabled("No preview");
    }

    ImGui::Text("Status: %s", session.status.c_str());
    if (!session.error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", session.error.c_str());
    }
    if (session.connected_info.width > 0 && session.connected_info.height > 0) {
        ImGui::TextDisabled("%ux%u  %s  frame=%llu  fps=%.1f",
            session.connected_info.width,
            session.connected_info.height,
            format_name(session.connected_info.format),
            static_cast<unsigned long long>(session.connected_info.frame_counter),
            session.connected_info.estimated_fps);
    }

    ImGui::EndChild();
    ImGui::PopID();
}

void gui::draw_empty_tile(const source_entry &source, float width, float height, const char *message) {
    ImGui::BeginChild(source_key(source).c_str(), ImVec2(width, height), true);
    ImGui::TextUnformatted(source.name.c_str());
    ImGui::TextDisabled("Backend: %s", backend_name(source.backend));
    ImGui::TextDisabled("%s", message);
    ImGui::EndChild();
}

void gui::destroy_session_texture(receiver_session &session) {
    if (session.preview_texture && backend_) {
        backend_->destroy_preview_texture(session.preview_texture);
    }
    session.preview_texture = nullptr;
    session.preview_width = 0;
    session.preview_height = 0;
}

} // namespace nozzle_viewer
