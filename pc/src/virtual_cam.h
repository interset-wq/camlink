#pragma once

#include <cstdint>
#include <string>

class VirtualCam {
public:
    VirtualCam() = default;
    ~VirtualCam();

    VirtualCam(const VirtualCam&) = delete;
    VirtualCam& operator=(const VirtualCam&) = delete;

    bool open(const std::string& devicePath, int width, int height);
    void close();
    bool isOpen() const;

    bool pushFrame(const uint8_t* rgb, int width, int height);

    static bool supported();

private:
#ifdef __linux__
    int deviceFd = -1;
#endif
    int frameWidth = 0;
    int frameHeight = 0;
    bool opened = false;
    bool warned = false;
};
