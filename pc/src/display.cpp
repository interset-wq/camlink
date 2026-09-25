#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "display.h"

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

    window = SDL_CreateWindow(title.c_str(),
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              width, height,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        return false;
    }

    renderer = SDL_CreateRenderer(static_cast<SDL_Window*>(window), -1,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(static_cast<SDL_Window*>(window), -1, 0);
    }
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        destroy();
        return false;
    }

    return true;
}

void Display::update(const uint8_t* rgbData, int width, int height) {
    if (!renderer || !rgbData || width <= 0 || height <= 0) return;

    if (!texture || currentWidth != width || currentHeight != height) {
        if (texture) {
            SDL_DestroyTexture(static_cast<SDL_Texture*>(texture));
            texture = nullptr;
        }
        texture = SDL_CreateTexture(static_cast<SDL_Renderer*>(renderer),
                                    SDL_PIXELFORMAT_RGB24,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    width, height);
        if (!texture) {
            std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << std::endl;
            return;
        }
        currentWidth = width;
        currentHeight = height;
    }

    if (SDL_UpdateTexture(static_cast<SDL_Texture*>(texture), nullptr,
                          rgbData, width * 3) != 0) {
        std::cerr << "SDL_UpdateTexture failed: " << SDL_GetError() << std::endl;
        return;
    }

    redraw();
}

void Display::redraw() {
    if (!renderer) return;

    SDL_SetRenderDrawColor(static_cast<SDL_Renderer*>(renderer), 0, 0, 0, 255);
    SDL_RenderClear(static_cast<SDL_Renderer*>(renderer));
    if (texture) {
        SDL_RenderCopy(static_cast<SDL_Renderer*>(renderer),
                       static_cast<SDL_Texture*>(texture), nullptr, nullptr);
    }
    SDL_RenderPresent(static_cast<SDL_Renderer*>(renderer));
}

bool Display::shouldClose() {
    if (!window) return true;

    SDL_Event event;
    bool close = false;
    bool needRedraw = false;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                close = true;
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    event.window.event == SDL_WINDOWEVENT_RESTORED ||
                    event.window.event == SDL_WINDOWEVENT_EXPOSED) {
                    needRedraw = true;
                }
                break;
            default:
                break;
        }
    }

    if (needRedraw) redraw();
    return close;
}

void Display::setTitle(const std::string& title) {
    if (window) SDL_SetWindowTitle(static_cast<SDL_Window*>(window), title.c_str());
}

void Display::destroy() {
    if (texture) {
        SDL_DestroyTexture(static_cast<SDL_Texture*>(texture));
        texture = nullptr;
    }
    if (renderer) {
        SDL_DestroyRenderer(static_cast<SDL_Renderer*>(renderer));
        renderer = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(window));
        window = nullptr;
    }
    currentWidth = 0;
    currentHeight = 0;

    if (SDL_WasInit(0)) SDL_Quit();
}
