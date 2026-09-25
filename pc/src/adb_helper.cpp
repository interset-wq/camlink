#include "adb_helper.h"

#include "cmd.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef _WIN32
    #include <windows.h>
#endif

namespace fs = std::filesystem;

namespace adb {
namespace {

using shell::quote;
using shell::run;

bool looksLikeError(const std::string& output) {
    std::string lower = output;
    for (auto& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return lower.find("error") != std::string::npos ||
           lower.find("failed") != std::string::npos;
}

bool isExecutable(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec) && !ec;
}

std::string fromEnv(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string();
}

std::vector<fs::path> searchDirs() {
    std::vector<fs::path> dirs;

    for (const char* name : {"ANDROID_HOME", "ANDROID_SDK_ROOT"}) {
        std::string root = fromEnv(name);
        if (!root.empty()) dirs.push_back(fs::path(root) / "platform-tools");
    }

#ifdef _WIN32
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData) {
        dirs.push_back(fs::path(localAppData) / "Android" / "Sdk" / "platform-tools");
    }
#endif

    // Common manual installs (extend here if adb lives elsewhere).
    for (const char* candidate : {"D:/androidSDK/platform-tools",
                                  "C:/androidSDK/platform-tools",
                                  "C:/Android/sdk/platform-tools"}) {
        dirs.push_back(fs::path(candidate));
    }

    std::string pathEnv = fromEnv(
#ifdef _WIN32
        "Path"
#else
        "PATH"
#endif
    );
    if (pathEnv.empty()) pathEnv = fromEnv("PATH");

    std::stringstream ss(pathEnv);
    std::string entry;
    char sep =
#ifdef _WIN32
        ';'
#else
        ':'
#endif
        ;
    while (std::getline(ss, entry, sep)) {
        if (!entry.empty()) dirs.push_back(fs::path(entry));
    }

    return dirs;
}

}  // namespace

std::string findExecutable(const std::string& hint) {
#ifdef _WIN32
    const char* exeName = "adb.exe";
#else
    const char* exeName = "adb";
#endif

    if (!hint.empty()) {
        return isExecutable(hint) ? hint : std::string();
    }

    for (const char* name : {"CAMLINK_ADB", "ADB"}) {
        std::string fromEnvVar = fromEnv(name);
        if (isExecutable(fromEnvVar)) return fromEnvVar;
    }

    for (const auto& dir : searchDirs()) {
        fs::path candidate = dir / exeName;
        if (isExecutable(candidate)) return candidate.string();
    }

    return "";
}

bool listDevices(std::string& serials, const std::string& adbPath) {
    std::string adb = adbPath.empty() ? findExecutable() : adbPath;
    if (adb.empty()) return false;

    std::string out;
    int status = run(quote(adb) + " devices 2>&1", &out);
    if (status != 0) return false;

    serials.clear();
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty() || line == "List of devices attached" ||
            line.find("*") == 0) {
            continue;
        }
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string state = line.substr(tab + 1);
        if (!state.empty() && state.back() == '\r') state.pop_back();
        if (state == "device") serials += line.substr(0, tab) + "\n";
    }
    return true;
}

bool addReverse(int devicePort, int hostPort, const std::string& adbPath) {
    std::string adb = adbPath.empty() ? findExecutable() : adbPath;
    if (adb.empty()) return false;

    std::ostringstream spec;
    spec << "tcp:" << devicePort;

    // Clear any stale mapping first (best effort, ignores failures).
    run(quote(adb) + " reverse --remove " + spec.str() + " 2>&1");

    std::ostringstream cmd;
    cmd << quote(adb) << " reverse " << spec.str() << " tcp:" << hostPort << " 2>&1";

    std::string out;
    int status = run(cmd.str(), &out);
    if (status != 0 || looksLikeError(out)) {
        std::cerr << "adb reverse failed: " << out << std::endl;
        return false;
    }
    return true;
}

void removeReverse(int devicePort, const std::string& adbPath) {
    std::string adb = adbPath.empty() ? findExecutable() : adbPath;
    if (adb.empty()) return;
    run(quote(adb) + " reverse --remove tcp:" + std::to_string(devicePort) + " 2>&1");
}

}  // namespace adb
