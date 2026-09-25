#include "recorder.h"

#include <chrono>
#include <cstring>
#include <iostream>

namespace {

void putU32(std::ostream& s, uint32_t v) {
    char b[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                 static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)};
    s.write(b, 4);
}

void putU16(std::ostream& s, uint16_t v) {
    char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF)};
    s.write(b, 2);
}

void putTag(std::ostream& s, const char* tag) {
    s.write(tag, 4);
}

void putZeros(std::ostream& s, int count) {
    static const char zeros[64] = {0};
    s.write(zeros, count);
}

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

Recorder::~Recorder() {
    close();
}

bool Recorder::open(int w, int h) {
    if (targetPath.empty()) return false;

    out.open(targetPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "Recorder: cannot open " << targetPath << std::endl;
        return false;
    }

    width = w;
    height = h;

    // RIFF header (sizes patched at close)
    putTag(out, "RIFF");
    putU32(out, 0);
    putTag(out, "AVI ");

    // hdrl LIST (content is fixed size, sizes known upfront)
    putTag(out, "LIST");
    putU32(out, 4 + 64 + (4 + 64 + 48));  // 'hdrl' + avih chunk + strl LIST
    putTag(out, "hdrl");

    // avih — MainAVIHeader, patched at close
    putTag(out, "avih");
    putU32(out, 56);
    avihDataPos = static_cast<std::streamoff>(out.tellp());
    putZeros(out, 56);

    // strl LIST
    putTag(out, "LIST");
    putU32(out, 4 + 64 + 48);  // 'strl' + strh chunk + strf chunk
    putTag(out, "strl");

    putTag(out, "strh");
    putU32(out, 56);
    strhDataPos = static_cast<std::streamoff>(out.tellp());
    putZeros(out, 56);

    putTag(out, "strf");
    putU32(out, 40);
    strfDataPos = static_cast<std::streamoff>(out.tellp());
    putZeros(out, 40);

    // movi LIST (size patched at close)
    putTag(out, "LIST");
    moviSizePos = static_cast<std::streamoff>(out.tellp());
    putU32(out, 0);
    moviFourccPos = static_cast<std::streamoff>(out.tellp());
    putTag(out, "movi");

    opened = static_cast<bool>(out);
    return opened;
}

bool Recorder::addFrame(const uint8_t* jpeg, size_t length, int w, int h) {
    if (!jpeg || length == 0) return false;

    if (!opened) {
        if (!open(w, h)) return false;
        firstMs = nowMs();
        lastMs = firstMs;
    }
    if (w != width || h != height) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::cerr << "Recorder: resolution change "
                      << width << "x" << height << " -> " << w << "x" << h
                      << ", frames with the new size are skipped" << std::endl;
        }
        return false;
    }

    lastMs = nowMs();

    uint64_t chunkPos = static_cast<uint64_t>(static_cast<std::streamoff>(out.tellp()));
    uint32_t offset = static_cast<uint32_t>(chunkPos - moviFourccPos);

    putTag(out, "00dc");
    putU32(out, static_cast<uint32_t>(length));
    out.write(reinterpret_cast<const char*>(jpeg), static_cast<std::streamsize>(length));
    if (length & 1) {
        out.put('\0');
    }

    if (!out) {
        std::cerr << "Recorder: write failed on " << targetPath << std::endl;
        close();
        return false;
    }

    index.emplace_back(offset, static_cast<uint32_t>(length));
    ++frames;
    totalBytes += length;
    if (length > maxChunk) maxChunk = static_cast<uint32_t>(length);

    // Flush regularly so a crash still leaves a usable (partial) file.
    if ((frames % 30) == 0) out.flush();
    return true;
}

void Recorder::close() {
    if (!opened) return;
    opened = false;

    int64_t elapsed = lastMs - firstMs;
    uint32_t usecPerFrame = 33333;
    uint32_t rate = 30000;  // dwRate with dwScale=1000 -> fps = rate/1000
    if (frames > 1 && elapsed > 0) {
        usecPerFrame = static_cast<uint32_t>(elapsed * 1000 / static_cast<int64_t>(frames));
        rate = static_cast<uint32_t>(
            (static_cast<double>(frames) * 1000.0 * 1000.0) / elapsed + 0.5);
        if (rate == 0) rate = 1000;
    }

    // idx1 index
    std::streamoff idxPos = static_cast<std::streamoff>(out.tellp());
    putTag(out, "idx1");
    putU32(out, static_cast<uint32_t>(index.size() * 16));
    for (const auto& e : index) {
        putTag(out, "00dc");
        putU32(out, 0x10);  // AVIF_KEYFRAME
        putU32(out, e.first);
        putU32(out, e.second);
    }
    std::streamoff endPos = static_cast<std::streamoff>(out.tellp());

    // Patch RIFF size
    out.seekp(0);
    putTag(out, "RIFF");
    putU32(out, static_cast<uint32_t>(endPos - 8));

    // MainAVIHeader
    out.seekp(avihDataPos);
    putU32(out, usecPerFrame);
    putU32(out, static_cast<uint32_t>(
                    (static_cast<double>(totalBytes) * 1000.0) /
                    static_cast<double>(elapsed > 0 ? elapsed : 1)));
    putU32(out, 0);        // dwPaddingGranularity
    putU32(out, 0x10);     // dwFlags: AVIF_HASINDEX
    putU32(out, static_cast<uint32_t>(frames));
    putU32(out, 0);        // dwInitialFrames
    putU32(out, 1);        // dwStreams
    putU32(out, maxChunk); // dwSuggestedBufferSize
    putU32(out, static_cast<uint32_t>(width));
    putU32(out, static_cast<uint32_t>(height));
    putU32(out, 0);
    putU32(out, 0);
    putU32(out, 0);
    putU32(out, 0);

    // AVIStreamHeader
    out.seekp(strhDataPos);
    putTag(out, "vids");
    putTag(out, "MJPG");
    putU32(out, 0);   // dwFlags
    putU16(out, 0);   // wPriority
    putU16(out, 0);   // wLanguage
    putU32(out, 0);   // dwInitialFrames
    putU32(out, 1000);// dwScale
    putU32(out, rate);// dwRate (fps*1000)
    putU32(out, 0);   // dwStart
    putU32(out, static_cast<uint32_t>(frames));
    putU32(out, maxChunk);
    putU32(out, 0xFFFFFFFF);  // dwQuality (default)
    putU32(out, 0);           // dwSampleSize
    putU16(out, 0);                          // rcFrame.left
    putU16(out, 0);                          // rcFrame.top
    putU16(out, static_cast<uint16_t>(width));   // rcFrame.right
    putU16(out, static_cast<uint16_t>(height));  // rcFrame.bottom

    // BITMAPINFOHEADER
    out.seekp(strfDataPos);
    putU32(out, 40);
    putU32(out, static_cast<uint32_t>(width));
    putU32(out, static_cast<uint32_t>(height));
    putU16(out, 1);      // biPlanes
    putU16(out, 24);     // biBitCount
    putTag(out, "MJPG"); // biCompression
    putU32(out, static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 3);
    putU32(out, 0);
    putU32(out, 0);
    putU32(out, 0);
    putU32(out, 0);

    // movi LIST size (from the 'movi' fourcc through end of movi data)
    out.seekp(moviSizePos);
    putU32(out, static_cast<uint32_t>(idxPos - moviFourccPos));

    out.flush();
    out.close();

    std::cout << "Recorder: wrote " << frames << " frames (" << totalBytes
              << " bytes) to " << targetPath << std::endl;

    // Reset so a subsequent recording starts from a clean slate.
    index.clear();
    frames = 0;
    totalBytes = 0;
    maxChunk = 0;
    firstMs = lastMs = 0;
}
