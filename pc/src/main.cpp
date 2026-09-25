#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "adb_helper.h"
#include "cmd.h"
#include "decoder.h"
#include "display.h"
#include "osd.h"
#include "recorder.h"
#include "tcp_receiver.h"
#include "virtual_cam.h"

namespace fs = std::filesystem;

namespace {

struct Options {
    int port = 5555;
    bool useAdb = true;
    std::string adbPath;
    int durationSec = 0;
    std::string dumpPath;
    std::string virtualCamPath;
    std::string recordPath;
    bool osdEnabled = true;
    bool showHelp = false;
};

struct FrameSlot {
    std::mutex mutex;
    std::vector<uint8_t> jpeg;
    bool fresh = false;
    uint64_t received = 0;
};

void printUsage() {
    std::cout <<
        "CamLink PC client\n"
        "Usage: camlink [options]\n"
        "\n"
        "Options:\n"
        "  --port <n>           TCP listen port (default: 5555)\n"
        "  --no-adb             Do not set up adb reverse automatically\n"
        "  --adb <path>         Path to the adb executable\n"
        "  --virtual-cam <dev>  Push frames to a virtual camera device\n"
        "                       (Linux, e.g. /dev/video2)\n"
        "  --dump-frame <file>  Save the first decoded frame as PPM\n"
        "  --record <file>      Record the stream to a video file:\n"
        "                         .avi  native MJPEG (no dependencies)\n"
        "                         .mp4  re-encoded via ffmpeg (if installed)\n"
        "  --no-osd            Disable the burned-in overlay (time, resolution,\n"
        "                       fps, REC) on preview and recordings\n"
        "  --duration <sec>     Exit automatically after N seconds\n"
        "  --help               Show this help\n";
}

bool parseArgs(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << std::endl;
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--port") {
            const char* v = value("--port");
            if (!v) return false;
            opt.port = std::atoi(v);
        } else if (arg == "--no-adb") {
            opt.useAdb = false;
        } else if (arg == "--adb") {
            const char* v = value("--adb");
            if (!v) return false;
            opt.adbPath = v;
        } else if (arg == "--virtual-cam") {
            const char* v = value("--virtual-cam");
            if (!v) return false;
            opt.virtualCamPath = v;
        } else if (arg == "--dump-frame") {
            const char* v = value("--dump-frame");
            if (!v) return false;
            opt.dumpPath = v;
        } else if (arg == "--record") {
            const char* v = value("--record");
            if (!v) return false;
            opt.recordPath = v;
        } else if (arg == "--no-osd") {
            opt.osdEnabled = false;
        } else if (arg == "--duration") {
            const char* v = value("--duration");
            if (!v) return false;
            opt.durationSec = std::atoi(v);
        } else if (arg == "--help" || arg == "-h") {
            opt.showHelp = true;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            return false;
        }
    }
    if (opt.port <= 0 || opt.port > 65535) {
        std::cerr << "Invalid port: " << opt.port << std::endl;
        return false;
    }
    return true;
}

bool writePpm(const std::string& path, const DecodedFrame& frame) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "Cannot write " << path << std::endl;
        return false;
    }
    out << "P6\n" << frame.width << " " << frame.height << "\n255\n";
    out.write(reinterpret_cast<const char*>(frame.rgb.data()),
              static_cast<std::streamsize>(frame.rgb.size()));
    return static_cast<bool>(out);
}

bool needsTranscode(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    for (auto& c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return ext == ".mp4" || ext == ".mkv" || ext == ".mov";
}

std::string replaceExtension(const std::string& path, const std::string& ext) {
    fs::path p(path);
    return p.replace_extension(ext).string();
}

std::string findExecutableInPath(const std::string& fileName) {
    const char* overridePath = std::getenv("CAMLINK_FFMPEG");
    if (overridePath && *overridePath && fs::is_regular_file(overridePath)) {
        return overridePath;
    }

    std::string pathEnv;
    const char* p = std::getenv("Path");
    if (!p) p = std::getenv("PATH");
    if (p) pathEnv = p;
    if (pathEnv.empty()) return "";

#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::stringstream ss(pathEnv);
    std::string entry;
    while (std::getline(ss, entry, sep)) {
        if (entry.empty()) continue;
        fs::path candidate = fs::path(entry) / fileName;
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) return candidate.string();
    }
    return "";
}

// Re-encodes the native MJPEG AVI to H.264 mp4 with ffmpeg (if available).
bool convertToMp4(const std::string& aviPath, const std::string& mp4Path) {
    std::string ffmpeg =
#ifdef _WIN32
        findExecutableInPath("ffmpeg.exe");
#else
        findExecutableInPath("ffmpeg");
#endif
    if (ffmpeg.empty()) {
        std::cerr << "ffmpeg not found; keeping " << aviPath << std::endl;
        return false;
    }

    std::string cmd = shell::quote(ffmpeg) +
                      " -y -i " + shell::quote(aviPath) +
                      " -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p"
                      " -movflags +faststart " + shell::quote(mp4Path) + " 2>&1";

    std::string out;
    int status = shell::run(cmd, &out);
    if (status != 0) {
        std::cerr << "ffmpeg failed (exit " << status << "): " << out << std::endl;
        return false;
    }

    std::error_code ec;
    fs::remove(aviPath, ec);
    std::cout << "Recorded video: " << mp4Path << std::endl;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) {
        printUsage();
        return 2;
    }
    if (opt.showHelp) {
        printUsage();
        return 0;
    }

    std::string adbPath;
    bool reverseAdded = false;
    if (opt.useAdb) {
        adbPath = adb::findExecutable(opt.adbPath);
        if (adbPath.empty()) {
            std::cerr << "adb not found; run manually: adb reverse tcp:"
                      << opt.port << " tcp:" << opt.port << std::endl;
        } else {
            std::string devices;
            if (adb::listDevices(devices, adbPath) && devices.empty()) {
                std::cerr << "No Android device connected (adb devices is empty)"
                          << std::endl;
            }
            // The phone dials 127.0.0.1:<port>; adb reverse routes that to
            // this PC's listener. Note: `adb forward` is the wrong direction.
            reverseAdded = adb::addReverse(opt.port, opt.port, adbPath);
            if (reverseAdded) {
                std::cout << "adb reverse tcp:" << opt.port
                          << " tcp:" << opt.port << " established" << std::endl;
            } else {
                std::cerr << "Could not set up adb reverse; run manually: adb reverse tcp:"
                          << opt.port << " tcp:" << opt.port << std::endl;
            }
        }
    }

    Display display;
    if (!display.init(1280, 720, "CamLink")) {
        if (reverseAdded) adb::removeReverse(opt.port, adbPath);
        return 1;
    }

    FrameSlot slot;
    TcpReceiver receiver;
    receiver.setFrameCallback([&slot](const std::vector<uint8_t>& jpeg) {
        std::lock_guard<std::mutex> lock(slot.mutex);
        slot.jpeg = jpeg;  // keep only the newest frame (drop stale ones)
        slot.fresh = true;
        ++slot.received;
    });

    if (!receiver.start(opt.port)) {
        display.destroy();
        if (reverseAdded) adb::removeReverse(opt.port, adbPath);
        return 1;
    }

    VirtualCam virtualCam;
    bool dumpWritten = opt.dumpPath.empty();

    Recorder recorder;
    if (!opt.recordPath.empty()) {
        // Native MJPEG AVI first; mp4/mkv/mov targets are transcoded at exit.
        std::string aviPath = needsTranscode(opt.recordPath)
                                  ? replaceExtension(opt.recordPath, ".avi")
                                  : opt.recordPath;
        recorder.setPath(aviPath);
    }

    uint64_t shownFrames = 0;
    uint64_t decodeFailures = 0;
    int fpsFrames = 0;
    double fps = 0.0;
    auto fpsMark = std::chrono::steady_clock::now();
    auto started = std::chrono::steady_clock::now();
    auto lastFrameAt = started;

    display.setTitle("CamLink | waiting for phone...");

    bool quit = false;
    while (!quit) {
        if (display.shouldClose()) quit = true;

        if (opt.durationSec > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - started).count();
            if (elapsed >= opt.durationSec) quit = true;
        }

        std::vector<uint8_t> jpeg;
        bool haveFrame = false;
        {
            std::lock_guard<std::mutex> lock(slot.mutex);
            if (slot.fresh) {
                jpeg.swap(slot.jpeg);
                slot.fresh = false;
                haveFrame = true;
            }
        }

        if (haveFrame) {
            DecodedFrame frame = Decoder::decodeJpeg(jpeg);
            if (frame.rgb.empty()) {
                ++decodeFailures;
            } else {
                if (opt.osdEnabled) {
                    char ts[32];
                    std::time_t t = std::time(nullptr);
                    std::tm tmv{};
#ifdef _WIN32
                    localtime_s(&tmv, &t);
#else
                    localtime_r(&t, &tmv);
#endif
                    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);
                    std::string line2 =
                        std::to_string(frame.width) + "x" +
                        std::to_string(frame.height) + " " +
                        std::to_string(static_cast<int>(fps + 0.5)) + " fps";
                    if (recorder.active()) {
                        line2 += " REC " + std::to_string(recorder.frameCount());
                    }
                    osd::draw(frame.rgb.data(), frame.width, frame.height,
                              ts, line2);
                }

                display.update(frame.rgb.data(), frame.width, frame.height);
                if (!dumpWritten) {
                    if (writePpm(opt.dumpPath, frame)) {
                        std::cout << "First frame dumped to " << opt.dumpPath
                                  << " (" << frame.width << "x" << frame.height
                                  << ")" << std::endl;
                        dumpWritten = true;
                    } else {
                        dumpWritten = true;
                    }
                }
                if (!opt.virtualCamPath.empty()) {
                    if (!virtualCam.isOpen()) {
                        virtualCam.open(opt.virtualCamPath,
                                        frame.width, frame.height);
                    }
                    if (virtualCam.isOpen()) {
                        virtualCam.pushFrame(frame.rgb.data(),
                                             frame.width, frame.height);
                    }
                }
                if (recorder.active() || !opt.recordPath.empty()) {
                    if (opt.osdEnabled) {
                        // Overlay is burned into the RGB frame; re-encode it
                        // so the recording contains the OSD too.
                        std::vector<uint8_t> encoded;
                        auto append = [](void* user, void* data, int size) {
                            auto* out = static_cast<std::vector<uint8_t>*>(user);
                            const auto* p = static_cast<const uint8_t*>(data);
                            out->insert(out->end(), p, p + size);
                        };
                        if (stbi_write_jpg_to_func(append, &encoded,
                                                   frame.width, frame.height, 3,
                                                   frame.rgb.data(), 90) &&
                            !encoded.empty()) {
                            recorder.addFrame(encoded.data(), encoded.size(),
                                              frame.width, frame.height);
                        }
                    } else {
                        recorder.addFrame(jpeg.data(), jpeg.size(),
                                          frame.width, frame.height);
                    }
                }
                ++shownFrames;
                ++fpsFrames;
                lastFrameAt = std::chrono::steady_clock::now();
            }
        }

        auto now = std::chrono::steady_clock::now();
        auto fpsElapsedMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - fpsMark)
                .count();
        if (fpsElapsedMs >= 500) {
            fps = fpsFrames * 1000.0 / fpsElapsedMs;
            fpsFrames = 0;
            fpsMark = now;

            std::string title;
            if (shownFrames == 0) {
                title = "CamLink | waiting for phone...";
            } else {
                auto idleSec = std::chrono::duration_cast<std::chrono::seconds>(
                                   now - lastFrameAt).count();
                title = "CamLink | " + std::to_string(shownFrames) + " frames | " +
                        std::to_string(static_cast<int>(fps + 0.5)) + " fps" +
                        (recorder.active()
                             ? " | REC " + std::to_string(recorder.frameCount())
                             : "") +
                        (idleSec >= 3 ? " | stalled" : "") +
                        (decodeFailures ? " | errors " + std::to_string(decodeFailures)
                                        : "");
            }
            display.setTitle(title);
        }

        if (!haveFrame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    receiver.stop();
    virtualCam.close();

    uint64_t recordedFrames = recorder.frameCount();
    std::string recordedAvi = recorder.path();
    recorder.close();

    if (needsTranscode(opt.recordPath) && recordedFrames > 0) {
        if (!convertToMp4(recordedAvi, opt.recordPath)) {
            std::cout << "Native MJPEG recording kept at: " << recordedAvi << std::endl;
        }
    } else if (recordedFrames > 0) {
        std::cout << "Recorded video: " << recordedAvi << std::endl;
    }

    display.destroy();
    if (reverseAdded) adb::removeReverse(opt.port, adbPath);

    std::cout << "Exit. Frames shown: " << shownFrames
              << ", decode errors: " << decodeFailures << std::endl;
    return 0;
}
