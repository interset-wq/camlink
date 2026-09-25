#define SDL_MAIN_HANDLED
#include <SDL.h>
#ifdef _WIN32
#include <SDL_opengl.h>
#else
#include <GL/gl.h>
#endif

#include "display.h"

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

#include <iostream>

Display::Display() {}

Display::~Display() {
    destroy();
}

bool Display::init(int width, int height, const std::string& title) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_SetMainReady();

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    window = SDL_CreateWindow(title.c_str(),
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              width, height,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                  SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        return false;
    }

    glContext = SDL_GL_CreateContext(static_cast<SDL_Window*>(window));
    if (!glContext) {
        std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError()
                  << std::endl;
        destroy();
        return false;
    }
    SDL_GL_SetSwapInterval(1);  // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // nothing here is worth persisting; avoids
                               // dropping imgui.ini into the CWD

    ImGui_ImplSDL2_InitForOpenGL(static_cast<SDL_Window*>(window), glContext);
    ImGui_ImplOpenGL3_Init(nullptr);
    imguiReady = true;

    return true;
}

bool Display::shouldClose() {
    if (!window) return true;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (imguiReady) ImGui_ImplSDL2_ProcessEvent(&event);

        switch (event.type) {
            case SDL_QUIT:
                closeRequested = true;
                break;
            case SDL_KEYDOWN: {
                if (event.key.repeat) break;
                AppEvent a;
                a.type = AppEvent::Type::KeyDown;
                // Map physical scancodes: keysym.sym depends on the active
                // keyboard layout and may arrive as an unmapped value.
                switch (event.key.keysym.scancode) {
                    case SDL_SCANCODE_R: a.key = AppEvent::Key::Record; break;
                    case SDL_SCANCODE_S: a.key = AppEvent::Key::Snapshot; break;
                    case SDL_SCANCODE_F: a.key = AppEvent::Key::Fullscreen; break;
                    case SDL_SCANCODE_O: a.key = AppEvent::Key::OsdToggle; break;
                    case SDL_SCANCODE_ESCAPE: a.key = AppEvent::Key::Escape; break;
                    default: a.key = AppEvent::Key::Unknown; break;
                }
                if (a.key != AppEvent::Key::Unknown) pendingEvents.push_back(a);
                break;
            }
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT &&
                    event.button.clicks >= 2) {
                    AppEvent a;
                    a.type = AppEvent::Type::MouseDoubleClick;
                    a.x = event.button.x;
                    a.y = event.button.y;
                    pendingEvents.push_back(a);
                }
                break;
            default:
                break;
        }
    }
    return closeRequested;
}

void Display::present(const std::function<void()>& uiBuild) {
    if (!window || !imguiReady) return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (uiBuild) uiBuild();

    ImGui::Render();

    int drawableW = 0, drawableH = 0;
    SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(window), &drawableW,
                           &drawableH);
    glViewport(0, 0, drawableW, drawableH);
    glClearColor(0.055f, 0.06f, 0.075f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(window));
}

void Display::uploadFrame(const uint8_t* rgbData, int width, int height) {
    if (!window || !rgbData || width <= 0 || height <= 0) return;

    if (!glTexture || width != currentWidth || height != currentHeight) {
        if (glTexture) glDeleteTextures(1, &glTexture);
        glGenTextures(1, &glTexture);
        glBindTexture(GL_TEXTURE_2D, glTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        currentWidth = width;
        currentHeight = height;
    }

    glBindTexture(GL_TEXTURE_2D, glTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, rgbData);
}

bool Display::pollAppEvent(AppEvent& out) {
    if (pendingEvents.empty()) return false;
    out = pendingEvents.front();
    pendingEvents.erase(pendingEvents.begin());
    return true;
}

bool Display::wantCaptureKeyboard() const {
    return imguiReady && ImGui::GetIO().WantCaptureKeyboard;
}

bool Display::wantCaptureMouse() const {
    return imguiReady && ImGui::GetIO().WantCaptureMouse;
}

void Display::toggleFullscreen() {
    if (!window) return;
    fullscreen = !fullscreen;
    SDL_SetWindowFullscreen(static_cast<SDL_Window*>(window),
                            fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

bool Display::isFullscreen() const { return fullscreen; }

void Display::setIcon(const uint8_t* rgba, int width, int height) {
    if (!window || !rgba || width <= 0 || height <= 0) return;
    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        const_cast<uint8_t*>(rgba), width, height, 32, width * 4,
        0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u);
    if (surface) {
        SDL_SetWindowIcon(static_cast<SDL_Window*>(window), surface);
        SDL_FreeSurface(surface);
    } else {
        std::cerr << "SDL_CreateRGBSurfaceFrom failed: " << SDL_GetError()
                  << std::endl;
    }
}

void Display::setTitle(const std::string& title) {
    if (window) SDL_SetWindowTitle(static_cast<SDL_Window*>(window),
                                   title.c_str());
}

void* Display::videoTexture() const {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(glTexture));
}

void Display::destroy() {
    if (imguiReady) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        imguiReady = false;
    }
    if (glTexture) {
        glDeleteTextures(1, &glTexture);
        glTexture = 0;
    }
    if (glContext) {
        SDL_GL_DeleteContext(glContext);
        glContext = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(window));
        window = nullptr;
    }
    currentWidth = 0;
    currentHeight = 0;
    pendingEvents.clear();

    if (SDL_WasInit(0)) SDL_Quit();
}
