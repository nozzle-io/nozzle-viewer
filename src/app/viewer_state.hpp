#pragma once

#include <app/source_registry.hpp>

#include <string>

namespace nozzle_viewer {

enum class display_mode {
    grid,
    single,
};

class viewer_state {
public:
    display_mode mode() const noexcept;
    bool auto_refresh() const noexcept;
    const std::string &focused_key() const noexcept;

    void set_mode(display_mode mode) noexcept;
    void toggle_mode() noexcept;
    void set_auto_refresh(bool enabled) noexcept;
    void focus_source(const source_entry &source);
    void clear_focus();
    void reconcile_focus(const source_registry &registry);

private:
    display_mode mode_{display_mode::grid};
    bool auto_refresh_{true};
    std::string focused_key_{};
};

} // namespace nozzle_viewer
