#include "osd.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_easy_font.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace osd {

namespace {

// Must match stb_easy_font's interleaved vertex layout: x,y,z float + RGBA.
struct Vertex {
    float x, y, z;
    unsigned char color[4];
};
static_assert(sizeof(Vertex) == 16, "stb_easy_font vertex layout mismatch");

// blend=true: darken toward black (semi-transparent box); false: solid white.
void fillRect(uint8_t* rgb, int W, int H,
              double x0, double y0, double x1, double y1, bool blend) {
    int ix0 = std::max(0, static_cast<int>(std::floor(x0)));
    int iy0 = std::max(0, static_cast<int>(std::floor(y0)));
    int ix1 = std::min(W, static_cast<int>(std::ceil(x1)));
    int iy1 = std::min(H, static_cast<int>(std::ceil(y1)));
    for (int y = iy0; y < iy1; ++y) {
        uint8_t* p = rgb + (static_cast<size_t>(y) * W + ix0) * 3;
        for (int x = ix0; x < ix1; ++x) {
            if (blend) {
                p[0] = static_cast<uint8_t>(p[0] * 2 / 5);
                p[1] = static_cast<uint8_t>(p[1] * 2 / 5);
                p[2] = static_cast<uint8_t>(p[2] * 2 / 5);
            } else {
                p[0] = p[1] = p[2] = 255;
            }
            p += 3;
        }
    }
}

void drawText(uint8_t* rgb, int W, int H, const char* text,
              double ox, double oy, double scale) {
    std::vector<char> mutableText(text, text + std::strlen(text) + 1);
    size_t vbytes = (mutableText.size() * 300 + 63) & ~static_cast<size_t>(63);
    std::vector<unsigned char> vbuf(vbytes);

    int quads = stb_easy_font_print(0.f, 0.f, mutableText.data(), nullptr,
                                    vbuf.data(), static_cast<int>(vbuf.size()));
    const Vertex* v = reinterpret_cast<const Vertex*>(vbuf.data());
    for (int i = 0; i < quads; ++i) {
        const Vertex* q = v + static_cast<size_t>(i) * 4;
        double x0 = q[0].x, x1 = q[0].x;
        double y0 = q[0].y, y1 = q[0].y;
        for (int k = 1; k < 4; ++k) {
            x0 = std::min(x0, static_cast<double>(q[k].x));
            x1 = std::max(x1, static_cast<double>(q[k].x));
            y0 = std::min(y0, static_cast<double>(q[k].y));
            y1 = std::max(y1, static_cast<double>(q[k].y));
        }
        fillRect(rgb, W, H,
                 ox + x0 * scale, oy + y0 * scale,
                 ox + x1 * scale, oy + y1 * scale,
                 /*blend=*/false);
    }
}

}  // namespace

void draw(uint8_t* rgb, int width, int height,
          const std::string& line1, const std::string& line2) {
    if (!rgb || width <= 0 || height <= 0) return;
    if (line1.empty() && line2.empty()) return;

    // ~12px font: scale up on larger frames so text stays readable.
    int scale = height / 360;
    if (scale < 1) scale = 1;

    std::string s1 = line1;
    std::string s2 = line2;
    int w1 = s1.empty() ? 0 : stb_easy_font_width(s1.data());
    int h1 = s1.empty() ? 0 : stb_easy_font_height(s1.data());
    int w2 = s2.empty() ? 0 : stb_easy_font_width(s2.data());
    int h2 = s2.empty() ? 0 : stb_easy_font_height(s2.data());

    const int gap = 2;
    const int pad = 4;
    int textW = std::max(w1, w2);
    int textH = h1 + (s2.empty() ? 0 : gap + h2);
    int boxW = textW * scale + 2 * pad;
    int boxH = textH * scale + 2 * pad;

    int x0 = width - 8 - boxW;
    int y0 = height - 8 - boxH;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;

    fillRect(rgb, width, height, x0, y0, x0 + boxW, y0 + boxH, /*blend=*/true);
    if (!s1.empty()) drawText(rgb, width, height, s1.c_str(), x0 + pad, y0 + pad, scale);
    if (!s2.empty()) {
        drawText(rgb, width, height, s2.c_str(),
                 x0 + pad, y0 + pad + (h1 + gap) * scale, scale);
    }
}

}  // namespace osd
