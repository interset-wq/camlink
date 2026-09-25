#pragma once

#include <string>

namespace adb {

// Resolves the adb executable path.
// Order: explicit hint > CAMLINK_ADB env > ADB env > PATH > known SDK locations.
std::string findExecutable(const std::string& hint = "");

// Lists serials of devices in state "device" (excludes offline/unauthorized).
// Returns false when adb itself cannot be run.
bool listDevices(std::string& serials, const std::string& adbPath = "");

// Sets up `adb reverse tcp:<devicePort> tcp:<hostPort>` so the phone can
// connect to 127.0.0.1:<devicePort> and reach this PC's listener.
bool addReverse(int devicePort, int hostPort, const std::string& adbPath = "");

// Removes the reverse mapping (best effort).
void removeReverse(int devicePort, const std::string& adbPath = "");

}  // namespace adb
