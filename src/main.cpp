#include <gui/gui.hpp>

#include <cstdio>

int main() {
    nozzle_viewer::gui app;
    if (!app.init()) {
        std::fprintf(stderr, "failed to initialize nozzle-viewer\n");
        return 1;
    }
    app.run();
    return 0;
}
