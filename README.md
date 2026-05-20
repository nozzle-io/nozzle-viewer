# nozzle-viewer

A lightweight desktop viewer for discovering and previewing active [nozzle](https://github.com/nozzle-io/nozzle) texture-sharing sources.

## Features

- Enumerates currently available nozzle senders.
- Connects receivers to discovered sources and shows live CPU-readback previews when the source backend is compatible with the host backend.
- Switches between a multi-source grid view and a focused single-source view.
- Builds on macOS, Windows, and Linux using CMake, GLFW, and Dear ImGui.

## Build

```sh
git clone --recursive https://github.com/nozzle-io/nozzle-viewer.git
cd nozzle-viewer
cmake -S . -B build -DNOZZLE_VIEWER_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

On Linux, install the standard OpenGL/X11/Wayland development packages required by GLFW and the DMA-BUF/GBM packages required by nozzle.

## Usage

```sh
./build/nozzle-viewer
```

Use **Refresh** to rescan senders. **Auto refresh** keeps the sender list current. Grid mode displays all visible sources; single mode focuses one selected source.

Preview requires the sender backend to match the receiver backend used by nozzle on the current platform. Unsupported or incompatible sources remain listed with their connection/acquire error so discovery remains useful even without a live frame.
