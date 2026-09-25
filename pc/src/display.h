#pragma once

#include <cstdint>
#include <string>

class Display {
public:
    Display();
    ~Display();

    bool init(int width, int height, const std::string& title = "CamLink");
    void update(const uint8_t* rgbData, int width, int height);
    void destroy();
    bool shouldClose();
    void setTitle(const std::string& title);

private:
    void redraw();

    void* window = nullptr;
    void* renderer = nullptr;
    void* texture = nullptr;
    int currentWidth = 0;
    int currentHeight = 0;
};
