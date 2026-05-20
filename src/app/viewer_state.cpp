#include <app/viewer_state.hpp>

namespace nozzle_viewer {

display_mode viewer_state::mode() const noexcept {
    return mode_;
}

bool viewer_state::auto_refresh() const noexcept {
    return auto_refresh_;
}

const std::string &viewer_state::focused_key() const noexcept {
    return focused_key_;
}

void viewer_state::set_mode(display_mode mode) noexcept {
    mode_ = mode;
}

void viewer_state::toggle_mode() noexcept {
    mode_ = mode_ == display_mode::grid ? display_mode::single : display_mode::grid;
}

void viewer_state::set_auto_refresh(bool enabled) noexcept {
    auto_refresh_ = enabled;
}

void viewer_state::focus_source(const source_entry &source) {
    focused_key_ = source.id.empty() ? source.name : source.id;
    mode_ = display_mode::single;
}

void viewer_state::clear_focus() {
    focused_key_.clear();
    mode_ = display_mode::grid;
}

void viewer_state::reconcile_focus(const source_registry &registry) {
    if (focused_key_.empty()) {
        if (!registry.sources().empty()) {
            focused_key_ = registry.sources().front().id.empty()
                ? registry.sources().front().name
                : registry.sources().front().id;
        }
        return;
    }

    if (!registry.find_by_id_or_name(focused_key_)) {
        focused_key_.clear();
        if (!registry.sources().empty()) {
            focused_key_ = registry.sources().front().id.empty()
                ? registry.sources().front().name
                : registry.sources().front().id;
        } else {
            mode_ = display_mode::grid;
        }
    }
}

} // namespace nozzle_viewer
