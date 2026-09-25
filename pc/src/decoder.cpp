#include "decoder.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

DecodedFrame Decoder::decodeJpeg(const std::vector<uint8_t>& jpegData) {
    DecodedFrame frame;

    int width, height, channels;
    unsigned char* data = stbi_load_from_memory(
        jpegData.data(),
        (int)jpegData.size(),
        &width,
        &height,
        &channels,
        3  // Force RGB
    );

    if (!data) {
        return frame;
    }

    frame.width = width;
    frame.height = height;
    frame.rgb.assign(data, data + (width * height * 3));
    stbi_image_free(data);

    return frame;
}
