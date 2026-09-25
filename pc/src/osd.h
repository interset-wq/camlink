#pragma once

#include <cstdint>
#include <string>

namespace osd {

// Burns a semi-transparent info overlay (timestamp, resolution, fps, REC)
// into an RGB24 buffer, bottom-right corner. Scales with frame height.
void draw(uint8_t* rgb, int width, int height,
          const std::string& line1, const std::string& line2);

}  // namespace osd
