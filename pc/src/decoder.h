#pragma once

#include <cstdint>
#include <vector>

struct DecodedFrame {
    std::vector<uint8_t> rgb;
    int width = 0;
    int height = 0;
};

class Decoder {
public:
    static DecodedFrame decodeJpeg(const std::vector<uint8_t>& jpegData);
};
