# nozzle-viewer design

`nozzle-viewer` intentionally keeps policy and rendering separate:

- `source_registry` owns discovery snapshots from `nozzle::enumerate_senders()`.
- `viewer_state` owns display mode and focused-source selection.
- `gui` owns receiver lifecycle, CPU pixel readback, and ImGui presentation.
- `render_backend` owns only the platform ImGui renderer and CPU RGBA/BGRA texture upload.

The app does not require a sender for tests. CI validates model behavior and a full GUI executable build on macOS, Windows, and Linux.
