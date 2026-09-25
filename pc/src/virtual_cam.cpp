#include "virtual_cam.h"

#include <iostream>

#ifdef __linux__
    #include <fcntl.h>
    #include <unistd.h>
    #include <sys/ioctl.h>
    #include <linux/videodev2.h>
#endif

VirtualCam::~VirtualCam() {
    close();
}

bool VirtualCam::supported() {
#ifdef __linux__
    return true;
#else
    return false;
#endif
}

bool VirtualCam::isOpen() const {
    return opened;
}

bool VirtualCam::open(const std::string& devicePath, int width, int height) {
#ifdef __linux__
    close();

    deviceFd = ::open(devicePath.c_str(), O_WRONLY);
    if (deviceFd < 0) {
        std::cerr << "VirtualCam: cannot open " << devicePath
                  << " (is v4l2loopback loaded?)" << std::endl;
        return false;
    }

    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    fmt.fmt.pix.bytesperline = width * 3;
    fmt.fmt.pix.sizeimage = width * height * 3;

    if (ioctl(deviceFd, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << "VirtualCam: VIDIOC_S_FMT failed on " << devicePath << std::endl;
        close();
        return false;
    }

    frameWidth = width;
    frameHeight = height;
    opened = true;
    std::cout << "VirtualCam: streaming " << width << "x" << height
              << " to " << devicePath << std::endl;
    return true;
#else
    (void)devicePath;
    (void)width;
    (void)height;
    if (!warned) {
        warned = true;
        std::cerr << "VirtualCam: not supported on this platform; "
                  << "add the CamLink window as a source in OBS "
                  << "and start OBS Virtual Camera instead." << std::endl;
    }
    return false;
#endif
}

void VirtualCam::close() {
#ifdef __linux__
    if (deviceFd >= 0) {
        ::close(deviceFd);
        deviceFd = -1;
    }
#endif
    opened = false;
    frameWidth = 0;
    frameHeight = 0;
}

bool VirtualCam::pushFrame(const uint8_t* rgb, int width, int height) {
    if (!opened || !rgb) return false;

    if (width != frameWidth || height != frameHeight) {
        if (!warned) {
            warned = true;
            std::cerr << "VirtualCam: frame size " << width << "x" << height
                      << " does not match device format "
                      << frameWidth << "x" << frameHeight
                      << ", dropping frames" << std::endl;
        }
        return false;
    }

#ifdef __linux__
    size_t total = static_cast<size_t>(width) * height * 3;
    const uint8_t* p = rgb;
    size_t remaining = total;
    while (remaining > 0) {
        ssize_t n = ::write(deviceFd, p, remaining);
        if (n <= 0) return false;
        p += n;
        remaining -= static_cast<size_t>(n);
    }
    return true;
#else
    return false;
#endif
}
