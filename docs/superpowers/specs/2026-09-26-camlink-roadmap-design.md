# CamLink Roadmap Master Design

- Date: 2026-09-26
- Status: design sections approved in chat; pending spec review
- Scope: overall roadmap and architecture (总纲). Each phase gets its own
  implementation plan before coding starts (via writing-plans skill).
- Related: `README.md`, `AGENTS.md` (must be synced when phases land)

---

## 1. Product Vision and Scope

**CamLink turns a phone into a set of PC peripherals** over the existing
USB/adb-reverse TCP link. The monitoring camera is the core; other features
are extensions of the same transport.

| Tier | Feature | Status |
|---|---|---|
| Core | Monitoring video: rear / front / screen sources, orientation compensation, gyro virtual PTZ | rear camera done; rest is P1–P2 |
| Core | Speaker: PC system audio → phone loudspeaker (real need: Dell laptop with Ubuntu has no speaker driver) | researched; P3 |
| Convenience | Win11 Media Foundation virtual camera (QR scanning, livestreaming — PC built-in cams are too low-res) | researched; P4 |
| Placeholder | Microphone, reverse display (PC → phone video), multi-phone, Wi-Fi transport | out of scope; extension seams only (§2.5) |

### 1.1 Scope assumptions (approved in chat)

- Single phone, USB/adb-reverse link only. No Wi-Fi, no cloud, no multi-device.
- Monitoring sessions ≈ 2 h, foreground service, screen-off allowed;
  **no** battery management, no multi-device scheduling.
- **Single active video source** — front/back/screen are mutually exclusive,
  switched stop→start like a phone camera app. No simultaneous dual-camera.
- PC platforms: Linux + Windows for audio; virtual camera is Win11-only
  (Linux virtual camera via v4l2loopback already exists).
- Existing behavior (preview, recording, OSD, shortcuts, snapshots) is
  unchanged; guarded by the existing smoke/e2e/selftest suites.
- Speaker transport design (from earlier approved discussion): system loopback
  capture, PCM passthrough (no codec), low latency target one-way < 150 ms,
  runs simultaneously with camera streaming.

---

## 2. Architecture and Extensibility Seams

Three seams make future features "add a class, not change the core".

### 2.1 Android video source abstraction

```
VideoSource (interface: start / stop / frame callback)
 ├─ RearCameraSource    ← existing CameraManager.kt renamed/refactored (P0)
 ├─ FrontCameraSource   ← P1
 ├─ ScreenSource        ← P1, MediaProjection → RGBA → JPEG
 └─ (future: AudioSource → microphone)
SourceManager           ← owns exactly ONE active source; handles switch,
                           permissions, lifecycle
```

- Transform layer lives on the phone: orientation compensation and PTZ crop
  run before JPEG encode. Rear/front use Camera2 `Scaler CropRegion`
  (hardware ISP crop, saves bandwidth); screen source crops in software.
- Screen source encodes to JPEG to keep the wire format uniform (no H.264
  decode path on PC). 1080p RGBA→JPEG performance is a P1 risk (§3 P1).

### 2.2 Protocol: typed frame header (P0)

```
[ type:1 byte ][ length:4 bytes big-endian ][ payload ]
types: 0x01 VIDEO_JPEG (payload = raw JPEG, as today)
       0x02 CONTROL    (low-frequency events: source switch, orientation)
       0x03 AUDIO_PCM   (P3)
       reserved for future (mic uplink, reverse display, ...)
```

- Android `StreamService` gains a reader coroutine (today it only writes) —
  this creates the PC→phone control channel that P3 audio reuses directly.
- Atomic change: both framing sites + smoke test updated in one commit; no
  third-party compatibility concerns (only our two clients).

### 2.3 PC pipeline with registered sinks

```
FrameSlot → Decode → Transform → OSD → Sinks { Preview, Record, Snapshot,
                                               VirtualCam (P4), v4l2 (exists) }
```

- `main.cpp` (565 lines) shrinks to orchestration; each output becomes a
  sink class. New output (virtual camera, audio meter, ...) = new sink file,
  no edits to the core loop.
- Transform = PC-side frame operations (pass-through at first; reserved for
  future PC-side scaling/mirroring). Rotation and PTZ crop are done on the
  phone (§2.1), not here.

### 2.4 Speaker skeleton (P3 detail deferred to sub-plan)

- Reuses the single multiplexed connection: `AUDIO_PCM` chunks (10 ms,
  48 kHz stereo 16-bit ≈ 1.5 Mbps — USB bandwidth is ample) +
  `AUDIO_START/STOP` control carrying sample-rate/channels (native rate
  passthrough, zero resampling; AudioTrack plays 44.1/48 k natively).
- PC capture via **miniaudio** (vendored single header): Linux
  Pulse/PipeWire monitor + Windows WASAPI loopback behind one API.
  (Supersedes the earlier "raw WASAPI" idea — Linux is the primary target
  machine for this feature.)
- Latency budget: 10 ms chunks, small buffers, target one-way < 150 ms
  (lip-sync when replacing broken laptop speakers while watching video).

### 2.5 Future feature → seam map

| Future feature | Plugs into |
|---|---|
| Microphone (phone → PC) | new `AudioSource` on Android + new protocol type |
| Reverse display (PC → phone video) | mirror of the sink concept on Android + new type |
| Multi-phone | `TcpReceiver` multi-client + device manager |
| Wi-Fi transport | transport seam below the protocol (framing unchanged) |

---

## 3. Phased Roadmap

Each phase is independently deliverable and verifiable. A phase starts only
after its own implementation plan is written (writing-plans skill) — except
P0, which may start after this spec is approved.

**Dependencies:** P0 blocks everything; P1 → P2 (PTZ must work across all
sources). P3 and P4 depend only on P0, but run in the approved schedule
order (after P2) unless the user re-prioritizes.

### P0 — Foundation refactor (behavior-preserving)

- Android: `CameraManager` → `RearCameraSource` + `SourceManager`;
  `StreamService` reader coroutine.
- Protocol: typed header + `CONTROL` skeleton (no real messages yet);
  smoke test sender updated.
- PC: pipeline + sink registration; existing logic moved verbatim.
- **Acceptance:** smoke + e2e + `--selftest-ui` all green; preview/record/OSD
  visually identical (baseline recorded before refactor).
- **Guard:** capture test baseline first, compare item-by-item after.

### P1 — Front camera + screen source

- `FrontCameraSource` (switch logic mostly shared with rear),
  `ScreenSource` (MediaProjection consent flow → VirtualDisplay →
  ImageReader → RGBA→JPEG).
- Phone UI: source switch control driving `SourceManager`; all three sources
  flow through the same pipeline (preview/record/stream/snapshot work for
  free).
- **Acceptance:** after each switch, each source streams ≥ 2 min without
  interruption; switch recovers streaming < 2 s; screen source ≥ 10 fps at
  1080p — if the perf test fails, downscale resolution and note it here.
- **Risk / spike:** MediaProjection behavior with screen off; 2 h sessions
  may keep the screen on as fallback — spike result sets the level.

### P2 — Orientation compensation + gyro virtual PTZ

- Orientation: sensor attitude → send upright frames (portrait/landscape
  rotation without breaking the stream).
- Virtual PTZ: accelerometer/gyro → crop-window parameters → Camera2
  `Scaler CropRegion` (rear/front) / software crop (screen). Optional PC-side
  indicator showing the current crop window.
- Smoothing: dead-zone + filter to avoid jitter; target follow latency
  < 200 ms.
- **Acceptance:** rotating the phone keeps the picture upright; tilting the
  phone moves the crop window smoothly.
- **Risk / spike:** CropRegion interaction with zoom/AF — 1-day spike first.

### P3 — Speaker (core)

- PC capture: miniaudio loopback (Linux + Windows), audio thread →
  `AUDIO_PCM` frames; phone `AudioTrack` playback.
- Control: `AUDIO_START/STOP` with native format passthrough; PC UI toggle +
  phone playback status.
- **Acceptance:** works simultaneously with camera streaming; measured
  one-way latency < 150 ms (loopback recording measurement); verified on
  both Linux and Windows.
- **Risk / spike:** miniaudio loopback under PipeWire (Ubuntu) — 1-day spike
  first; fallback is per-backend capture behind the same interface.

### P4 — Win11 MF virtual camera (convenience)

- Approach A (approved): host registration inside `camlink.exe` +
  `CamLinkCamSource.dll` (COM `IMFMediaSource`/`IMFActivate`, loaded by the
  Frame Server) + named shared-memory latest-frame slot (NV12).
- Toolchain: MinGW preferred (verified: `mfidl.h`, `mfobjects.h`,
  `libmfplat.a` etc. present; only `mfvirtualcamera.h` missing → hand-written
  minimal declaration inheriting MinGW's `IMFAttributes` + runtime
  `GetProcAddress` of `mfsensorgroup.dll`). MSVC (VS18 installed) is the
  fallback if the spike fails; MSVC switch is a last resort (cost: SDL2 VC
  package, CMake/docs churn).
- Lifecycle (approved): camera exists while `camlink.exe` window runs
  (DroidCam model); `--vcam install/uninstall` CLI + UI menu entry.
- Integrated as a P0-style sink (RGB→NV12 + shared-memory write); feeds
  **pre-OSD** frames by default (a timestamp overlay is unwanted in
  meetings/QR scanning; can become a setting if ever needed).
- **Acceptance:** Windows Camera app, browser `getUserMedia`, and Zoom all
  list "CamLink Camera" with smooth video; install/uninstall clean.
- **Spike (gate):** register the virtual camera, appear in Camera app,
  receive one synthetic frame — **before** any phase-level coding.
- Reference implementations to consult (not vendor): BestCam (same
  architecture, open source), Wincam (MIT), Microsoft's Windows-Camera
  VirtualCamera sample.

### P4+ — Placeholders

Microphone, reverse display, multi-phone, Wi-Fi: each gets its own
brainstorm → spec → plan cycle. Nothing in this master doc commits to them.

---

## 4. Risks and Verification Strategy

| Category | Strategy |
|---|---|
| Regression guard | Record baseline (smoke + e2e + selftest) before P0; every phase ends by running all three; `--duration` mode for scripted smoke |
| Big unknowns first | Each phase's uncertainty gets a spike (P1 MediaProjection/screen-off, P2 CropRegion, P3 miniaudio/PipeWire, P4 MF registration); a spike result that invalidates phase design upgrades back to brainstorming |
| Protocol evolution | From P0 on: protocol changes = both ends + tests in one atomic commit; format documented in AGENTS.md/README in the same commit |
| Performance | Frame-rate assertions stay in test scripts (e2e style); speaker latency measured by loopback recording; screen-source fps measured in P1 |
| Documentation | Every phase syncs AGENTS.md + README + docs/; this master doc is updated when phases change shape |

---

## 5. Decision Log

| Decision | Rationale |
|---|---|
| Camera completion before speaker | user priority: core monitoring feature first |
| Speaker is core, not deferred | real need: Ubuntu has no driver for the laptop's speakers |
| Single active video source | phone-like UX; hardware reality |
| Protocol type header in P0 | cheapest now; foundation for speaker + all future types |
| miniaudio for capture (not raw WASAPI) | feature's primary target machine is Ubuntu; one API for both OSes |
| Virtual camera: Win11 MF only | user scope choice; Win10/DirectShow explicitly out |
| Virtual camera lifecycle = app window open | user scope choice (DroidCam model); no tray/autostart in first version |
| Virtual camera approach A (self-built, MinGW, spike-gated) | smallest standing cost; single toolchain; MSVC as fallback; OSS ports as reference only |
| No plugin frameworks / no multi-process PC architecture | YAGNI at this scale; simple class seams suffice |

---

## Appendix A — Virtual camera research facts (2026-09-26)

- `MFCreateVirtualCamera`: min client **Windows build 22000 (Win11)**, header
  `mfvirtualcamera.h`, library `mfsensorgroup.lib`; no Win10 equivalent
  (Win10 would require a DirectShow filter — out of scope).
- Architecture: host process registers/starts the camera; the media-source
  DLL is loaded by the system **Frame Server** service (Session 0) → frames
  cross a process boundary via **named shared memory** (BestCam) or named
  pipe (Wincam). DLL must have minimal dependencies (no SDL2); MinGW runtime
  statically linked into it.
- Registration lifetime: per-user session semantics (per Wincam); exact
  persistence behavior to be confirmed by the P4 spike. Admin requirement is
  claimed by some projects (BestCam) but not by MS docs — spike confirms.
- Dev machine: Windows 11 build 26200, Windows SDK 10.0.26100 (has
  `mfvirtualcamera.h` + `mfsensorgroup.lib`), VS18 Community (MSVC 14.51),
  MinGW g++ 15.2 (MinGW-Builds, headers under
  `mingw64/x86_64-w64-mingw32/include`).
- MinGW header/lib status: ✅ `mfidl.h`, `mfobjects.h`, `mfapi.h`,
  `dshow.h`, `audioclient.h`, `mmdeviceapi.h`; ✅ `libmfplat.a`,
  `libmfuuid.a`, `libstrmiids.a`, `libmf.a`; ❌ `mfvirtualcamera.h`,
  ❌ `streams.h` (DirectShow base classes — not needed for approach A).

## Appendix B — Speaker research facts (2026-09-26)

- Windows loopback: WASAPI `AUDCLNT_STREAMFLAGS_LOOPBACK`; Linux: Pulse/PipeWire
  monitor source. miniaudio covers both (to be spike-verified on PipeWire).
- PCM 48 kHz stereo 16-bit ≈ 1.5 Mbps — negligible over USB; no codec needed
  on either end (WASAPI/Pulse give PCM → AudioTrack consumes PCM directly).
- Android playback: `AudioTrack` streaming mode, native 44.1/48 kHz;
  format announced via `AUDIO_START`, so no resampler in v1.
- Latency target < 150 ms one-way (lip-sync for watching PC video while the
  phone is the only speaker); chunk size 10 ms as starting point.
- Runs over the same multiplexed socket as video (single adb reverse port);
  reconnect re-sends `AUDIO_START`.
