#include <app/source_registry.hpp>
#include <app/viewer_state.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace nozzle_viewer;

TEST_CASE("sender snapshots are sorted and deduplicated") {
    std::vector<nozzle::sender_info> senders{
        {"zeta", "app", "2", nozzle::backend_type::metal},
        {"alpha", "app", "1", nozzle::backend_type::d3d11},
        {"alpha", "app", "1", nozzle::backend_type::d3d11},
    };

    auto entries = to_source_entries(senders);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == "alpha");
    CHECK(entries[1].name == "zeta");
}

TEST_CASE("source_registry tracks generation and lookup") {
    source_registry registry;
    registry.set_sources({{"camera", "app", "abc", nozzle::backend_type::opengl}});

    CHECK(registry.generation() == 1);
    REQUIRE(registry.find_by_id_or_name("abc") != nullptr);
    REQUIRE(registry.find_by_id_or_name("camera") != nullptr);
    CHECK(registry.find_by_id_or_name("missing") == nullptr);
}

TEST_CASE("viewer_state keeps a valid focus") {
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

TEST_CASE("backend and format names are stable") {
    CHECK(std::string(backend_name(nozzle::backend_type::dma_buf)) == "DMA-BUF");
    CHECK(std::string(format_name(nozzle::texture_format::rgba8_unorm)) == "rgba8_unorm");
    CHECK(std::string(format_name(nozzle::texture_format::unknown)) == "unknown");
}
