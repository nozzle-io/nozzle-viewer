#include <app/source_registry.hpp>
#include <app/viewer_state.hpp>

#include <cassert>
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

} // namespace

int main() {
    test_sender_snapshots_are_sorted_and_deduplicated();
    test_source_registry_tracks_generation_and_lookup();
    test_viewer_state_keeps_a_valid_focus();
    test_backend_and_format_names_are_stable();
    return 0;
}
