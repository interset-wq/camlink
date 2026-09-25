#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// Records raw JPEG frames as an MJPEG AVI file.
//
// The file is written incrementally (frames are flushed as they arrive) and
// the RIFF/AVI headers plus the idx1 index are patched in at close(), so
// memory usage stays constant regardless of recording length.
class Recorder {
public:
    Recorder() = default;
    ~Recorder();

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Starts a recording; dimensions come from the first frame.
    bool addFrame(const uint8_t* jpeg, size_t length, int width, int height);

    // Finalizes headers/index and closes the file. Safe to call twice.
    void close();

    bool active() const { return opened; }
    uint64_t frameCount() const { return frames; }
    const std::string& path() const { return targetPath; }

    // Target path (must be set before the first addFrame).
    void setPath(const std::string& path) { targetPath = path; }

private:
    bool open(int width, int height);

    std::string targetPath;
    std::ofstream out;
    std::vector<std::pair<uint32_t, uint32_t>> index;  // (offset, size)
    bool opened = false;
    uint64_t frames = 0;
    uint64_t totalBytes = 0;
    uint32_t maxChunk = 0;
    int width = 0;
    int height = 0;
    int64_t firstMs = 0;
    int64_t lastMs = 0;
    std::streamoff avihDataPos = 0;
    std::streamoff strhDataPos = 0;
    std::streamoff strfDataPos = 0;
    std::streamoff moviSizePos = 0;
    std::streamoff moviFourccPos = 0;
};
