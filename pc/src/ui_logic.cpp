#include "ui_logic.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <system_error>

#include "stb_image_write.h"  // declarations only; implementation in main.cpp

namespace fs = std::filesystem;

namespace ui_logic {

std::string nowTimestamp() {
    char buf[32];
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tmv);
    return buf;
}

bool needsTranscode(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".mp4" || ext == ".mkv" || ext == ".mov";
}

std::string replaceExtension(const std::string& path, const std::string& ext) {
    return fs::path(path).replace_extension(ext).string();
}

std::pair<std::string, std::string> nextRecordPath(const std::string& cliTarget,
                                                   bool& cliConsumed,
                                                   const std::string& timestamp) {
    if (!cliTarget.empty() && !cliConsumed) {
        cliConsumed = true;
        if (needsTranscode(cliTarget)) {
            return {replaceExtension(cliTarget, ".avi"), cliTarget};
        }
        return {cliTarget, ""};
    }
    return {"camlink_" + timestamp + ".avi", ""};
}

std::string snapshotPath(const std::string& timestamp) {
    return "camlink_snapshot_" + timestamp + ".png";
}

bool toastVisible(int64_t shownAtMs, int64_t nowMs, int64_t ttlMs) {
    return nowMs - shownAtMs < ttlMs;
}

bool runSelfTest() {
    int failures = 0;
    auto check = [&failures](bool ok, const char* what) {
        if (!ok) {
            std::printf("SELFTEST-UI: FAIL (%s)\n", what);
            ++failures;
        }
    };

    {
        bool used = false;
        auto p = nextRecordPath("", used, "T1");
        check(p.first == "camlink_T1.avi" && p.second.empty(),
              "auto path is timestamped avi");
        check(!used, "auto path does not consume cli target");
    }
    {
        bool used = false;
        auto p = nextRecordPath("out.mp4", used, "T1");
        check(p.first == "out.avi" && p.second == "out.mp4",
              "mp4 target uses avi intermediate");
        check(used, "cli target consumed");
        auto q = nextRecordPath("out.mp4", used, "T2");
        check(q.first == "camlink_T2.avi" && q.second.empty(),
              "second recording is timestamped");
    }
    {
        bool used = false;
        auto p = nextRecordPath("clip.avi", used, "T1");
        check(p.first == "clip.avi" && p.second.empty(),
              "avi cli target used directly");
    }
    check(snapshotPath("T1") == "camlink_snapshot_T1.png", "snapshot path");
    check(toastVisible(1000, 3999, 3000), "toast visible before ttl");
    check(!toastVisible(1000, 4000, 3000), "toast hidden after ttl");

    // The PNG writer snapshots rely on must work.
    std::error_code ec;
    fs::create_directories("temp", ec);
    const unsigned char px[3] = {10, 20, 30};
    int wrote = stbi_write_png("temp/selftest_ui.png", 1, 1, 3, px, 3);
    check(wrote == 1, "snapshot png write");
    if (wrote == 1) {
        auto size = fs::file_size("temp/selftest_ui.png", ec);
        check(!ec && size > 0, "snapshot png non-empty");
    }

    if (failures == 0) {
        std::printf("SELFTEST-UI: PASS\n");
        return true;
    }
    return false;
}

}  // namespace ui_logic
