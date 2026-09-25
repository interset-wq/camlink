#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace ui {

// Everything the UI needs to render one frame. Filled by main each loop.
struct FrameState {
    // Video (GL texture used as ImTextureID)
    void* texture = nullptr;
    int videoW = 0;
    int videoH = 0;
    bool hasFrame = false;

    // Stream stats
    double fps = 0.0;
    uint64_t shownFrames = 0;
    uint64_t decodeErrors = 0;
    bool connected = false;
    bool stalled = false;

    // Recording
    bool recording = false;
    uint64_t recFrames = 0;
    int recSeconds = 0;
    std::string recTarget;

    bool fullscreen = false;
    bool osdEnabled = true;

    // Toast notification (empty text = none). shownAtMs is a steady_clock
    // millisecond stamp; visibility is decided inside draw().
    std::string toastText;
    int64_t toastShownAtMs = 0;

    // Actions — invoked when the user clicks a widget/shortcut target.
    std::function<void()> onRecordToggle;
    std::function<void()> onSnapshot;
    std::function<void()> onFullscreen;
    std::function<void()> onOsdToggle;
    std::function<void()> onQuit;
};

// Applies the CamLink theme. Call once after the ImGui context exists,
// before the first frame.
void initStyle();

// Builds the full UI for one ImGui frame. Must run inside ImGui::NewFrame().
void draw(const FrameState& state);

}  // namespace ui
