# CamLink — USB Camera Streaming

Stream Android phone camera to PC over USB (ADB + TCP).

User-facing documentation lives in `README.md` (keep both in sync when the
build steps, CLI flags, or features change).

## Architecture

- **Android app** (Kotlin): Camera2 API → JPEG encode → TCP push
- **PC client** (C++): TCP receive → JPEG decode → ImGui preview (SDL2
  window + OpenGL texture) + recording/snapshot + virtual camera
- **Connection**: ADB reverse (`adb reverse tcp:5555 tcp:5555`) — the phone dials
  `127.0.0.1:5555`, and adb routes it to the PC listener. `adb forward` is the
  wrong direction for this design (it would need the PC to dial the phone).

## Protocol

```
[4 bytes: payload length (big-endian uint32)][N bytes: JPEG data]
```

## Build

### Android (Gradle + NDK)

```bash
cd android
./gradlew assembleDebug        # debug APK
./gradlew assembleRelease      # release APK (signed when android/app/keystore.properties exists)
./gradlew installDebug         # install to connected device
```

SDK/NDK path: `D:\androidSDK`

### PC Client (CMake + MinGW)

```bash
pwsh -File pc/setup_deps.ps1   # fetches SDL2 into pc/third_party/SDL2 (once)
cd pc
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Dependencies:

- `pc/third_party/stb_image.h` — JPEG decoder, vendored (tracked in git)
- `pc/third_party/stb_image_write.h` — JPEG encoder, vendored (burned-in OSD
  requires re-encoding frames for recording)
- `pc/third_party/stb_easy_font.h` — bitmap font for the OSD overlay, vendored
- `pc/third_party/imgui/` — Dear ImGui (core + SDL2/OpenGL3 backends),
  vendored/trimmed (tracked in git); UI code in `src/ui*.cpp`
- `pc/res/`, `pc/tools/gen_icon.ps1` — app icon (`.ico` + `.rc.in`), embedded
  via windres on Windows
- `pc/third_party/SDL2/` — SDL2 MinGW devel package, downloaded by
  `setup_deps.ps1` (gitignored)
- MinGW g++ + CMake (tested: g++ 15.2, CMake 4.4); on Windows also needs
  `ws2_32` + `opengl32` (linked automatically)
- ffmpeg (optional) — only needed for `--record *.mp4`; native `--record
  *.avi` needs nothing. Install: `winget install Gyan.FFmpeg`

## Development

### Run end-to-end

1. Connect Android phone via USB, ensure `adb devices` shows it
2. Start PC client: `pc\build\camlink.exe` (sets up `adb reverse` itself;
   use `--no-adb` to skip, `--help` for all options)
3. Install and launch Android app — it connects to `127.0.0.1:5555`
4. Camera feed appears in PC window

Useful flags: `--port <n>`, `--duration <sec>` (auto-exit, good for tests),
`--dump-frame <file.ppm>` (save first decoded frame), `--virtual-cam <dev>`,
`--record <file.avi|file.mp4>` (record the stream; .avi is native MJPEG,
.mp4 re-encodes via ffmpeg with libx264), `--no-osd` (disable the burned-in
time/resolution/fps/REC overlay on preview and recordings).

### ADB reverse (manual fallback)

```bash
adb reverse tcp:5555 tcp:5555
```

### Smoke test (no phone needed)

```bash
pwsh -File pc/test/smoke_test.ps1   # selftest-ui + feeds 100 synthetic JPEG
                                    # frames, asserts recording + OSD overlay
```

### End-to-end test (phone connected)

```bash
pwsh -File pc/test/e2e_test.ps1     # installs APK, taps Start, checks frame count
```

### Virtual camera setup

- **Linux**: `sudo modprobe v4l2loopback`, then
  `camlink --virtual-cam /dev/video2` (writes RGB24 frames via V4L2)
- **Windows**: not implemented natively — add the CamLink window as a source in
  OBS and start OBS Virtual Camera

## Key files

- `android/app/src/main/java/com/intersetwq/camlink/` — Android app source
- `pc/src/main.cpp` — CLI args, frame slot, record/snapshot/toast actions, loop
- `pc/src/tcp_receiver.cpp` — TCP server, protocol framing
- `pc/src/decoder.cpp` — JPEG → RGB via stb_image
- `pc/src/display.cpp` — SDL2 window, OpenGL texture, ImGui backends, input
  → `AppEvent` (shortcuts, double-click fullscreen)
- `pc/src/ui.cpp` — ImGui frame: menu bar, status bar, toolbar, toasts, theme
- `pc/src/ui_logic.cpp` — pure UI logic (record/snapshot paths, toasts) +
  `--selftest-ui` checks
- `pc/src/adb_helper.cpp` — locate adb, manage `adb reverse`
- `pc/src/osd.cpp` — burned-in OSD overlay (time, resolution, fps, REC)
- `pc/src/virtual_cam.cpp` — v4l2loopback output (Linux) / OBS hint (Windows)
- `pc/src/icon_data.h`, `pc/res/`, `pc/tools/gen_icon.ps1` — window/app icon
- `pc/setup_deps.ps1` — downloads SDL2 into `third_party/`
- `pc/third_party/stb_image.h` — JPEG decoder (single header)
- `pc/third_party/imgui/` — Dear ImGui (vendored)
