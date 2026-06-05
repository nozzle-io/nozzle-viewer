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

## Preview format mapping

`nozzle-viewer` converts every known nozzle texture format to an RGBA8 preview
image before uploading it to the UI backend. Known formats should not show
`unsupported preview format`; that status is reserved for unknown/corrupt format
values.

- `rgba8_*` is copied directly; `bgra8_*` swaps red/blue into RGBA preview order.
- `r*`, `rg*`, and `rgb*` formats expand missing color channels to `0` and alpha
  to `255`.
- `*_unorm` formats are normalized to `[0, 1]` and rounded to 8-bit preview
  values.
- Float and depth formats clamp finite values to `[0, 1]`; depth is displayed as
  grayscale. Color-channel NaN maps to magenta, positive infinity maps to `255`,
  and negative infinity maps to `0`.
- `*_uint` formats are visualized with the low 8 bits of each component. This is
  an explicit debug-view choice so small IDs/bit patterns remain visible instead
  of becoming almost black under full-range 32-bit normalization.

## Latest build

The `latest` GitHub release is a replaceable development snapshot from `main`. It publishes macOS, Windows x64, and Linux x64 zip artifacts after macOS, Windows, and Linux CI all pass. Version tags (`v*`) publish immutable platform zip artifacts.
