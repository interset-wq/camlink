#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace ui_logic {

// Timestamp usable in file names: YYYYMMDD_HHMMSS (local time).
std::string nowTimestamp();

// True for containers that must be transcoded from MJPEG AVI (.mp4/.mkv/.mov).
bool needsTranscode(const std::string& path);

// Replace a path's extension (ext includes the dot, e.g. ".avi").
std::string replaceExtension(const std::string& path, const std::string& ext);

// Chooses the AVI path for a recording that starts now.
//   - First recording honors the --record CLI target (as an .avi
//     intermediate when the final target needs transcoding, e.g. .mp4).
//   - Later recordings get a timestamped camlink_<ts>.avi in the working dir.
// Returns {aviPath, finalPath}; finalPath is non-empty only when the CLI
// target still needs transcoding (aviPath -> finalPath at exit).
std::pair<std::string, std::string> nextRecordPath(const std::string& cliTarget,
                                                   bool& cliConsumed,
                                                   const std::string& timestamp);

// Snapshot file name for a capture taken at `timestamp`.
std::string snapshotPath(const std::string& timestamp);

// Toast notifications auto-hide after ttlMs.
bool toastVisible(int64_t shownAtMs, int64_t nowMs, int64_t ttlMs);

// In-process checks for the pure UI logic above. Prints
// "SELFTEST-UI: PASS"/"SELFTEST-UI: FAIL ..." and returns success.
bool runSelfTest();

}  // namespace ui_logic
