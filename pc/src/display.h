#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Application-level input forwarded from the SDL event loop.
// Key values are AppKey; raw SDL types stay inside display.cpp.
struct AppEvent {
    enum class Type { KeyDown, MouseDoubleClick };
    enum class Key { Record, Snapshot, Fullscreen, Escape, OsdToggle, Unknown };

    Type type = Type::KeyDown;
    Key key = Key::Unknown;
    int x = 0;
    int y = 0;
};

class Display {
public:
    Display();
    ~Display();

    bool init(int width, int height, const std::string& title = "CamLink");
    void destroy();

    // Upload an RGB24 frame to the GPU texture (recreated on size change).
    void uploadFrame(const uint8_t* rgbData, int width, int height);

    // Called every loop iteration: polls SDL events (feeding ImGui), then
    // builds the UI via `uiBuild` inside a fresh ImGui frame and presents.
    bool shouldClose();
    void present(const std::function<void()>& uiBuild);

    // Drains app-level events collected during shouldClose().
    bool pollAppEvent(AppEvent& out);

    bool wantCaptureKeyboard() const;
    bool wantCaptureMouse() const;

    void toggleFullscreen();
    bool isFullscreen() const;

    // Window/taskbar icon from raw RGBA32 pixels.
    void setIcon(const uint8_t* rgba, int width, int height);
    void setTitle(const std::string& title);

    // ImTextureID of the uploaded camera frame (GL texture name).
    void* videoTexture() const;

private:
    void* window = nullptr;        // SDL_Window*
    void* glContext = nullptr;     // SDL_GLContext
    unsigned int glTexture = 0;
    int currentWidth = 0;
    int currentHeight = 0;
    bool closeRequested = false;
    bool fullscreen = false;
    bool imguiReady = false;
    std::vector<AppEvent> pendingEvents;
};
