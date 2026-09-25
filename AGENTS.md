# CamLink — USB Camera Streaming

Stream Android phone camera to PC over USB (ADB + TCP).

## Architecture

- **Android app** (Kotlin): Camera2 API → JPEG encode → TCP push
- **PC client** (C++): TCP receive → JPEG decode → SDL2 display + virtual camera
- **Connection**: ADB port forwarding (`adb forward tcp:5555 tcp:5555`)

## Protocol

```
[4 bytes: payload length (big-endian uint32)][N bytes: JPEG data]
```

## Build

### Android (Gradle + NDK)

```bash
cd android
./gradlew assembleDebug        # debug APK
./gradlew installDebug         # install to connected device
```

SDK/NDK path: `D:\androidSDK`

### PC Client (CMake + MinGW)

```bash
cd pc
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
```

Dependencies: SDL2, stb_image.h (single header, in `third_party/`)

## Development

### Run end-to-end

1. Connect Android phone via USB, ensure `adb devices` shows it
2. Start PC client: `./build/camlink`
3. Install and launch Android app — it auto-connects via ADB forward
4. Camera feed appears in PC window + available as virtual camera

### ADB forward (manual fallback)

```bash
adb forward tcp:5555 tcp:5555
```

### Virtual camera setup

- **Linux**: `sudo modprobe v4l2loopback` then write frames to `/dev/videoX`
- **Windows**: Use OBS Virtual Camera or DirectShow filter

## Key files

- `android/app/src/main/java/com/camlink/` — Android app source
- `pc/src/` — PC client source
- `pc/third_party/stb_image.h` — JPEG decoder (single header)
