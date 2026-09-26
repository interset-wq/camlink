# CamLink

Stream your Android phone camera to a PC over USB — low-latency, no Wi-Fi
required. The phone pushes JPEG frames over an ADB-reversed TCP connection;
the PC client decodes and renders them in real time with an ImGui interface,
and can record the stream, grab snapshots, or feed a virtual camera.

```
Android app (Camera2)  --JPEG-->  TCP 127.0.0.1:5555  -->  PC client (SDL2 + OpenGL)
        ^                                  ^
        └──── adb reverse tcp:5555 ────────┘   (the phone dials the PC)
```

## Features

- **Live preview** — letterboxed ImGui UI with menu bar, status bar, floating
  toolbar (REC / Snapshot / Fullscreen) and toast notifications
- **Recording** — native MJPEG `.avi` with no dependencies; `.mp4`/`.mkv`/
  `.mov` targets are re-encoded through ffmpeg at exit
- **Snapshots** — one keypress saves the current frame (with OSD) as PNG
- **Burned-in OSD** — timestamp, resolution and fps overlaid on both the
  preview and the recording (toggle with `--no-osd` or `O`)
- **App icon** — embedded in the executable for Explorer/taskbar/Alt-Tab
- **Virtual camera** — push RGB frames to a v4l2loopback device (Linux)
- **Virtual-cam / OBS** — on Windows, add the CamLink window in OBS

### Shortcuts

| Key | Action |
|-----|--------|
| `R` | Start/stop recording |
| `S` | Take a snapshot |
| `F` | Toggle fullscreen |
| `O` | Toggle the burned-in OSD |
| `Esc` | Leave fullscreen |
| Double-click | Toggle fullscreen |

(Shortcuts are ignored while a menu/text field has keyboard focus.)

## Quick start

### 1. Build the Android app

```bash
cd android
./gradlew installDebug     # SDK/NDK expected at D:\androidSDK (see AGENTS.md)
```

Release APK: `./gradlew assembleRelease`. If `android/app/keystore.properties`
(gitignored) exists, the APK is signed with that keystore; otherwise the
release APK is unsigned and cannot be installed.

### 2. Build the PC client (Windows / MinGW)

```bash
pwsh -File pc/setup_deps.ps1       # once: fetches SDL2 into pc/third_party
cd pc
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### 3. Run

1. Connect the phone over USB (`adb devices` must list it)
2. Start `pc\build\camlink.exe` — it sets up `adb reverse` itself
   (use `--no-adb` to manage it manually)
3. Launch the CamLink app on the phone — the preview appears

### PC client options

```
--port <n>           TCP listen port (default: 5555)
--no-adb             Do not set up adb reverse automatically
--adb <path>         Path to the adb executable
--virtual-cam <dev>  Push frames to a virtual camera device (Linux, /dev/video2)
--dump-frame <file>  Save the first decoded frame as PPM
--record <file>      Record the stream (.avi native MJPEG, .mp4 via ffmpeg)
--no-osd             Disable the burned-in overlay
--duration <sec>     Exit automatically after N seconds
--help               Show help
```

## Testing

```bash
pwsh -File pc/test/smoke_test.ps1   # no phone needed: synthetic frames,
                                    # recording, OSD assertions, UI self-test
pwsh -File pc/test/e2e_test.ps1     # phone connected: installs APK, checks FPS
camlink --selftest-ui               # pure UI-logic checks (paths, toasts)
```

## Protocol

Each frame on the wire is `[4-byte big-endian payload length][JPEG bytes]`.
The phone initiates the connection via `adb reverse tcp:5555 tcp:5555`, so
the PC only listens.

## Project layout

```
android/   Android app (Kotlin, Camera2 -> JPEG -> TCP)
pc/        PC client (C++17, SDL2 + OpenGL + Dear ImGui)
docs/      Extra documentation
temp/      Test artifacts and scratch files (gitignored)
```

Vendored third-party code lives in `pc/third_party/` (stb headers, Dear ImGui,
SDL2 — the last one is downloaded by `setup_deps.ps1`).

## Licenses of vendored code

- [Dear ImGui](https://github.com/ocornut/imgui) — MIT
- [stb](https://github.com/nothings/stb) — MIT
- [SDL2](https://www.libsdl.org/) — zlib license
