// Software compositor for the in-ring layout.
//
// The renderer's maths (art scale, arena origin, sprite anchoring, depth sort)
// is the one thing that cannot be checked without a GPU. This tool repeats those
// few lines with a software blitter and writes a PNG, so the placement can be
// inspected on any machine and compared with the original screenshots.
//
//   tools/preview/preview <assets dir> <out.png> [wrestler folder ...]

#include "../../android_project/app/src/main/cpp/data/game_data.h"
#include "../../android_project/app/src/main/cpp/data/asset_source.h"
#include "../../android_project/app/src/main/cpp/render/image.h"
#include "../../android_project/app/src/main/cpp/render/zip_reader.h"
#include "../../android_project/app/src/main/cpp/sim/match.h"
#include "../../android_project/app/src/main/cpp/game/menu_layout.h"
#include "../../android_project/app/src/main/cpp/game/road.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace wrestlefest;

namespace {

constexpr int kWidth = 960;
constexpr int kHeight = 640;
// Mirrors game_renderer.cpp (kArtScale and the calibrated arena origin).
constexpr float kArtScale = 0.72f;
constexpr float kOriginX = 480.0f - 742.0f * kArtScale;
constexpr float kOriginY = 560.0f - 430.0f * kArtScale;

std::uint32_t crc32Of(const std::uint8_t *data, std::size_t size) {
    static std::uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        built = true;
    }
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

std::uint32_t adler32Of(const std::uint8_t *data, std::size_t size) {
    std::uint32_t a = 1, b = 0;
    for (std::size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void put32(std::vector<std::uint8_t> *out, std::uint32_t v) {
    out->push_back(static_cast<std::uint8_t>(v >> 24));
    out->push_back(static_cast<std::uint8_t>(v >> 16));
    out->push_back(static_cast<std::uint8_t>(v >> 8));
    out->push_back(static_cast<std::uint8_t>(v));
}

void putChunk(std::vector<std::uint8_t> *out, const char *type, const std::vector<std::uint8_t> &data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = out->size();
    out->insert(out->end(), type, type + 4);
    out->insert(out->end(), data.begin(), data.end());
    put32(out, crc32Of(out->data() + start, out->size() - start));
}

// zlib stream with stored (uncompressed) deflate blocks - no compressor needed.
std::vector<std::uint8_t> zlibStored(const std::vector<std::uint8_t> &raw) {
    std::vector<std::uint8_t> out;
    out.push_back(0x78);
    out.push_back(0x01);
    std::size_t pos = 0;
    while (pos < raw.size()) {
        const std::size_t len = std::min<std::size_t>(65535, raw.size() - pos);
        const bool last = pos + len >= raw.size();
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(len & 0xff));
        out.push_back(static_cast<std::uint8_t>(len >> 8));
        out.push_back(static_cast<std::uint8_t>(~len & 0xff));
        out.push_back(static_cast<std::uint8_t>((~len >> 8) & 0xff));
        out.insert(out.end(), raw.begin() + pos, raw.begin() + pos + len);
        pos += len;
    }
    put32(&out, adler32Of(raw.data(), raw.size()));
    return out;
}

bool writePng(const std::string &path, const std::vector<std::uint8_t> &rgba) {
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(kHeight) * (kWidth * 4 + 1));
    for (int y = 0; y < kHeight; ++y) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), rgba.begin() + static_cast<std::size_t>(y) * kWidth * 4,
                   rgba.begin() + static_cast<std::size_t>(y + 1) * kWidth * 4);
    }
    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<std::uint8_t> ihdr;
    put32(&ihdr, kWidth);
    put32(&ihdr, kHeight);
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    putChunk(&png, "IHDR", ihdr);
    putChunk(&png, "IDAT", zlibStored(raw));
    putChunk(&png, "IEND", {});
    FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return false;
    std::fwrite(png.data(), 1, png.size(), file);
    std::fclose(file);
    return true;
}

bool readFile(const std::string &path, std::vector<std::uint8_t> *bytes) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    bytes->resize(static_cast<std::size_t>(size));
    const std::size_t read = std::fread(bytes->data(), 1, bytes->size(), file);
    std::fclose(file);
    bytes->resize(read);
    return true;
}

struct Canvas {
    std::vector<std::uint8_t> rgba = std::vector<std::uint8_t>(kWidth * kHeight * 4, 0);

    void fill(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
        for (std::size_t i = 0; i < rgba.size(); i += 4) {
            rgba[i] = r;
            rgba[i + 1] = g;
            rgba[i + 2] = b;
            rgba[i + 3] = a;
        }
    }

    void fillRect(float x, float y, float w, float h, const float color[4]) {
        const int x0 = std::max(0, static_cast<int>(std::floor(x)));
        const int y0 = std::max(0, static_cast<int>(std::floor(y)));
        const int x1 = std::min(kWidth, static_cast<int>(std::ceil(x + w)));
        const int y1 = std::min(kHeight, static_cast<int>(std::ceil(y + h)));
        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) {
                const float sa = color[3];
                std::uint8_t *dst = &rgba[(static_cast<std::size_t>(py) * kWidth + px) * 4];
                for (int i = 0; i < 3; ++i) {
                    dst[i] =
                        static_cast<std::uint8_t>(color[i] * 255.0f * sa + dst[i] * (1.0f - sa));
                }
                dst[3] = 255;
            }
        }
    }

    void blendPixel(int x, int y, const std::uint8_t *src) {
        if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
        const float sa = src[3] / 255.0f;
        if (sa <= 0.0f) return;
        std::uint8_t *dst = &rgba[(static_cast<std::size_t>(y) * kWidth + x) * 4];
        for (int i = 0; i < 3; ++i) {
            dst[i] = static_cast<std::uint8_t>(src[i] * sa + dst[i] * (1.0f - sa));
        }
        dst[3] = 255;
    }

    // Nearest-neighbour scaled blit, matching the batch's quad placement.
    // `tint` mirrors the colour the GL batch multiplies into the texel.
    void blit(const std::vector<std::uint8_t> &image, int iw, int ih, float x, float y, float w,
              float h, bool flip_x, float alpha = 1.0f,
              const float tint[4] = nullptr) {
        blitRegion(image, iw, ih, 0, 0, iw, ih, x, y, w, h, flip_x, alpha, tint);
    }

    // Sub-rectangle blit (font glyph cells, atlas regions).
    void blitRegion(const std::vector<std::uint8_t> &image, int iw, int ih, int rx, int ry, int rw,
                    int rh, float x, float y, float w, float h, bool flip_x, float alpha = 1.0f,
                    const float tint[4] = nullptr) {
        if (iw <= 0 || ih <= 0 || rw <= 0 || rh <= 0 || w <= 0.0f || h <= 0.0f) return;
        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const int x1 = static_cast<int>(std::ceil(x + w));
        const int y1 = static_cast<int>(std::ceil(y + h));
        for (int py = y0; py < y1; ++py) {
            const int sy = static_cast<int>((py + 0.5f - y) / h * rh) + ry;
            if (sy < ry || sy >= ry + rh) continue;
            for (int px = x0; px < x1; ++px) {
                int sx = static_cast<int>((px + 0.5f - x) / w * rw) + rx;
                if (sx < rx || sx >= rx + rw) continue;
                if (flip_x) sx = rx + (rx + rw - 1 - sx);
                if (sx < 0 || sx >= iw) continue;
                std::uint8_t pixel[4] = {image[(static_cast<std::size_t>(sy) * iw + sx) * 4 + 0],
                                         image[(static_cast<std::size_t>(sy) * iw + sx) * 4 + 1],
                                         image[(static_cast<std::size_t>(sy) * iw + sx) * 4 + 2],
                                         image[(static_cast<std::size_t>(sy) * iw + sx) * 4 + 3]};
                pixel[3] = static_cast<std::uint8_t>(pixel[3] * alpha);
                if (tint != nullptr) {
                    for (int c = 0; c < 3; ++c) {
                        pixel[c] = static_cast<std::uint8_t>(pixel[c] * tint[c]);
                    }
                    pixel[3] = static_cast<std::uint8_t>(pixel[3] * tint[3]);
                }
                blendPixel(px, py, pixel);
            }
        }
    }

    // Rotated blit about the image centre (the loading card tilts the logo).
    void blitRotated(const std::vector<std::uint8_t> &image, int iw, int ih, float cx, float cy,
                     float w, float h, float degrees, const float tint[4] = nullptr) {
        if (iw <= 0 || ih <= 0 || w <= 0.0f || h <= 0.0f) return;
        const float radians = degrees * 3.14159265358979f / 180.0f;
        const float c = std::cos(-radians);
        const float s = std::sin(-radians);
        const int x0 = static_cast<int>(std::floor(cx - (std::fabs(w * c) + std::fabs(h * s))));
        const int y0 = static_cast<int>(std::floor(cy - (std::fabs(w * s) + std::fabs(h * c))));
        const int x1 = static_cast<int>(std::ceil(cx + (std::fabs(w * c) + std::fabs(h * s))));
        const int y1 = static_cast<int>(std::ceil(cy + (std::fabs(w * s) + std::fabs(h * c))));
        for (int py = y0; py <= y1; ++py) {
            for (int px = x0; px <= x1; ++px) {
                // Inverse rotate the pixel into the sprite's own space.
                const float dx = px + 0.5f - cx;
                const float dy = py + 0.5f - cy;
                const float u = dx * c - dy * s;
                const float v = dx * s + dy * c;
                const int sx = static_cast<int>((u / w + 0.5f) * iw);
                const int sy = static_cast<int>((v / h + 0.5f) * ih);
                if (sx < 0 || sx >= iw || sy < 0 || sy >= ih) continue;
                std::uint8_t pixel[4] = {image[(static_cast<std::size_t>(sy) * iw + sx) * 4 + 0],
                                         image[(static_cast<std::size_t>(sy) * iw + sx) * 4 + 1],
                                         image[(static_cast<std::size_t>(sy) * iw + sx) * 4 + 2],
                                         image[(static_cast<std::size_t>(sy) * iw + sx) * 4 + 3]};
                if (tint != nullptr) {
                    for (int k = 0; k < 4; ++k) {
                        pixel[k] = static_cast<std::uint8_t>(pixel[k] * tint[k]);
                    }
                }
                blendPixel(px, py, pixel);
            }
        }
    }

};

// ---------------------------------------------------------------------------
// tyF bitmap font: the same glyph cells the GL batch draws, blitted on the CPU.
// ---------------------------------------------------------------------------
struct Font {
    std::string name;
    std::vector<std::uint8_t> atlas;
    int width = 0;
    int height = 0;
    FontMetrics metrics;
    struct Glyph {
        float w = 0.0f;
        float h = 0.0f;
        float pre = 0.0f;
        float post = 0.0f;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
    };
    Glyph glyphs[256]{};

    bool load(const std::string &assets, const std::string &face) {
        name = face;
        std::vector<std::uint8_t> tyf;
        std::vector<std::uint8_t> png;
        if (!readFile(assets + "/graphics/Fonts/" + face + ".tyF", &tyf)) return false;
        if (!readFile(assets + "/graphics/Fonts/" + face + ".png", &png)) return false;
        Image image;
        if (!decodePng(png.data(), png.size(), &image)) return false;
        atlas = image.rgba;
        width = image.width;
        height = image.height;
        SmlDocument doc;
        std::string error;
        if (!doc.parse(reinterpret_cast<const char *>(tyf.data()), tyf.size(), &error)) return false;
        const SmlArray *table = doc.array("CharTable");
        if (table == nullptr || table->records.size() < 256) return false;
        for (int i = 0; i < 256; ++i) {
            const SmlRecord &r = table->records[static_cast<std::size_t>(i)];
            Glyph &g = glyphs[i];
            g.w = static_cast<float>(r.number("w"));
            g.h = static_cast<float>(r.number("h"));
            g.pre = static_cast<float>(r.number("preSpace"));
            g.post = static_cast<float>(r.number("postSpace"));
            g.u0 = static_cast<float>(r.number("u0"));
            g.v0 = static_cast<float>(r.number("v0"));
            g.u1 = static_cast<float>(r.number("u1"));
            g.v1 = static_cast<float>(r.number("v1"));
        }
        measureInk();
        return true;
    }

    // Same ink measurement as BitmapFont::measureInk: the capitals of 'H'.
    void measureInk() {
        const Glyph &h = glyphs[static_cast<unsigned char>('H')];
        metrics = FontMetrics{};
        metrics.cell_height = h.h > 0.0f ? h.h : 24.0f;
        const int x0 = static_cast<int>(h.u0 * width);
        const int y0 = static_cast<int>(h.v0 * height);
        const int x1 = static_cast<int>(std::lround(h.u1 * width));
        const int y1 = static_cast<int>(std::lround(h.v1 * height));
        int top = -1;
        int bottom = -1;
        for (int y = y0; y < y1 && y < height; ++y) {
            for (int x = x0; x < x1 && x < width; ++x) {
                if (atlas[(static_cast<std::size_t>(y) * width + x) * 4 + 3] > 40) {
                    if (top < 0) top = y;
                    bottom = y;
                    break;
                }
            }
        }
        if (top < 0 || y1 <= y0) return;
        const float cell_px = static_cast<float>(y1 - y0);
        metrics.ink_top = (top - y0) * h.h / cell_px;
        metrics.cap_height = (bottom - top + 1) * h.h / cell_px;
        metrics.measured = true;
    }

    float measure(const std::string &text, float scale, float tracking = 0.0f) const {
        float width_sum = 0.0f;
        for (char c : text) {
            const Glyph &g = glyphs[static_cast<unsigned char>(c)];
            width_sum += (g.pre + g.w + g.post + tracking) * scale;
        }
        return width_sum;
    }

    void draw(Canvas &canvas, float x, float y, float scale, const std::string &text,
              const float color[4], float tracking = 0.0f, float bold_px = 0.0f) const {
        if (atlas.empty()) return;
        static const float kRing[8][2] = {{1.0f, 0.0f},  {-1.0f, 0.0f}, {0.0f, 1.0f},  {0.0f, -1.0f},
                                          {0.7f, 0.7f},  {-0.7f, 0.7f}, {0.7f, -0.7f}, {-0.7f, -0.7f}};
        float pen = x;
        for (char c : text) {
            const Glyph &g = glyphs[static_cast<unsigned char>(c)];
            pen += g.pre * scale;
            if (g.w > 0.0f && g.h > 0.0f) {
                const int rx = static_cast<int>(g.u0 * width);
                const int ry = static_cast<int>(g.v0 * height);
                const int rw = std::max(1, static_cast<int>(std::lround((g.u1 - g.u0) * width)));
                const int rh = std::max(1, static_cast<int>(std::lround((g.v1 - g.v0) * height)));
                if (bold_px > 0.0f) {
                    for (const auto &offset : kRing) {
                        canvas.blitRegion(atlas, width, height, rx, ry, rw, rh,
                                          pen + offset[0] * bold_px, y + offset[1] * bold_px,
                                          g.w * scale, g.h * scale, false, 1.0f, color);
                    }
                }
                canvas.blitRegion(atlas, width, height, rx, ry, rw, rh, pen, y, g.w * scale,
                                  g.h * scale, false, 1.0f, color);
            }
            pen += (g.w + g.post + tracking) * scale;
        }
    }

    void drawCentered(Canvas &canvas, float center_x, float y, float scale, const std::string &text,
                      const float color[4], float shadow, float shadow_alpha,
                      float tracking = 0.0f, float bold_px = menu::kBoldPx) const {
        const float x = center_x - measure(text, scale, tracking) * 0.5f;
        if (shadow > 0.0f) {
            const float shadow_color[4] = {0.0f, 0.0f, 0.0f, shadow_alpha * color[3]};
            draw(canvas, x + shadow, y + shadow, scale, text, shadow_color, tracking);
        }
        draw(canvas, x, y, scale, text, color, tracking, bold_px);
    }
};

// The menu screens repeat MenuScreens::draw* with the CPU canvas, through the
// shared geometry in game/menu_layout.h.
struct MenuPreview {
    const std::string &assets;
    Canvas &canvas;
    Font &font;  // Century Gothic, the menu face

    bool image(const std::string &path, Image *out) const {
        std::vector<std::uint8_t> bytes;
        if (!readFile(assets + "/" + path, &bytes)) return false;
        return decodePng(bytes.data(), bytes.size(), out);
    }

    void backdrop(const char *file) {
        Image bg;
        if (image(std::string("ui/Sprites/UI/") + file, &bg)) {
            canvas.blit(bg.rgba, bg.width, bg.height, 0.0f, 0.0f, 960.0f, 640.0f, false);
        }
    }

    void heading(const char *text, const float color[4]) {
        // The original prints the heading from the top of the glyph cell (y=18)
        // and lets the ink fall where the face puts it.
        font.drawCentered(canvas, 480.0f, menu::headingCellTop(),
                          menu::scaleForSize(font.metrics, menu::kHeadingSize), text, color,
                          menu::kHeadingShadow, 0.75f, menu::kHeadingTracking);
    }

    void caption(const std::string &text, float center_x, float ink_top, float size,
                 const float color[4], float tracking = menu::kLabelTracking) const {
        font.drawCentered(canvas, center_x,
                          menu::inkTopToCellTop(font.metrics, ink_top, size),
                          menu::scaleForSize(font.metrics, size), text, color,
                          menu::kLabelShadow, 0.75f, tracking);
    }

    void backButton(float alpha) {
        Image back;
        if (image("ui/Sprites/UI/back.png", &back)) {
            canvas.blit(back.rgba, back.width, back.height, menu::kBackX, menu::kBackY,
                        menu::kBackSize, menu::kBackSize, false, alpha);
        }
    }

    void dots(int page, int page_count) {
        if (page_count < 2) return;
        Image dot;
        Image selected;
        const bool has_dot = image("ui/Sprites/UI/selector/point.png", &dot);
        const bool has_selected = image("ui/Sprites/UI/selector/point_select.png", &selected);
        for (int i = 0; i < page_count; ++i) {
            const Image &art = i == page && has_selected ? selected : dot;
            if (!has_dot || art.rgba.empty()) continue;
            canvas.blit(art.rgba, art.width, art.height, menu::dotX(i, page_count), menu::kDotY,
                        menu::kDotSize, menu::kDotSize, false);
        }
    }

    void arrows(int page, int page_count) {
        Image next;
        Image previous;
        if (page > 0 && image("ui/Sprites/UI/selector/previous.png", &previous)) {
            canvas.blit(previous.rgba, previous.width, previous.height, 26.0f, menu::kArrowY,
                        menu::kArrowWidth, menu::kArrowHeight, false);
        }
        if (page + 1 < page_count && image("ui/Sprites/UI/selector/next.png", &next)) {
            canvas.blit(next.rgba, next.width, next.height, 960.0f - 68.0f, menu::kArrowY,
                        menu::kArrowWidth, menu::kArrowHeight, false);
        }
    }

    void loading() {
        // background_2.png is the whole card: the ghosted, tilted logo is in
        // the art, so the only thing drawn over it is the label.
        backdrop("background_2.png");
        const float size = menu::kLoadingLabelSize;
        const float scale = menu::scaleForSize(font.metrics, size);
        const std::string label = "Loading ...";
        const float width = font.measure(label, scale, menu::kLabelTracking);
        const float cell_top =
            menu::inkTopToCellTop(font.metrics, menu::kLoadingLabelInkTop, size);
        const float x = 960.0f - menu::kLoadingLabelRight - width;
        const float shadow[4] = {0.0f, 0.0f, 0.0f, 0.75f};
        font.draw(canvas, x + menu::kNameShadow, cell_top + menu::kNameShadow, scale, label, shadow,
                  menu::kLabelTracking);
        const float color[4] = {0.80f, 0.85f, 0.88f, 1.0f};
        font.draw(canvas, x, cell_top, scale, label, color, menu::kLabelTracking);
    }

    void title() {
        backdrop("background.png");
        Image logo;
        if (image("ui/Sprites/UI/title_menu/title.png", &logo)) {
            const float width = 790.0f * 0.85f;
            const float height = 204.0f * 0.85f;
            canvas.blit(logo.rgba, logo.width, logo.height, 480.0f - width * 0.5f, 120.0f, width,
                        height, false);
        }
        Image tap;
        if (image("ui/Sprites/UI/title_menu/taptostart_en.png", &tap)) {
            canvas.blit(tap.rgba, tap.width, tap.height, 480.0f - 187.0f, 439.0f, 374.0f, 47.0f,
                        false);
        }
    }

    void mainMenu(int selected) {
        backdrop("background.png");
        heading("MAIN MENU", std::array<float, 4>{0.78f, 0.13f, 0.10f, 1.0f}.data());
        for (int i = 0; i < menu::kMainEntryCount; ++i) {
            const float y = menu::mainRowY(i);
            Image strip;
            if (image("ui/Sprites/UI/main_menu/main_button_" + std::to_string(i) + ".png", &strip)) {
                canvas.blit(strip.rgba, strip.width, strip.height, menu::kMainStripX, y,
                            menu::kMainStripWidth, menu::kMainStripHeight, false);
            }
            const float color[4] = {1.0f, i == selected ? 0.55f : 1.0f,
                                    i == selected ? 0.0f : 1.0f, 1.0f};
            const float ink_top = y + menu::centreInBox(font.metrics, 0.0f,
                                                        menu::kMainStripHeight,
                                                        menu::kMainLabelSize);
            caption(menu::mainEntryLabel(i), 480.0f, ink_top, menu::kMainLabelSize, color);
        }
        Image wwe;
        if (image("ui/Sprites/UI/title_menu/wwe_logo_for_shop.png", &wwe)) {
            // Sits left of the centred SHOP caption, as in the game.
            const float shop_width = font.measure(menu::mainEntryLabel(2),
                                                  menu::scaleForSize(font.metrics,
                                                                     menu::kMainLabelSize),
                                                  menu::kLabelTracking);
            canvas.blit(wwe.rgba, wwe.width, wwe.height, menu::wweLogoX(shop_width),
                        menu::kWweLogoY, menu::kWweLogoWidth,
                        wwe.height * menu::kWweLogoScale, false);
        }
        backButton(0.5f);
    }

    void modes(int selected) {
        backdrop("background.png");
        heading("GAME MODES", std::array<float, 4>{0.78f, 0.13f, 0.10f, 1.0f}.data());
        for (int i = 0; i < menu::kModeCount; ++i) {
            const float width = menu::modeStripWidth(i);
            const float height = menu::modeStripHeight(i);
            Image strip;
            const std::string file = "mode_button_" + std::to_string(i) + ".png";
            if (image("ui/Sprites/UI/main_menu/" + file, &strip)) {
                canvas.blit(strip.rgba, strip.width, strip.height, menu::modeStripX(i),
                            menu::modeRowY(i), width, height, false);
            }
            const float color[4] = {1.0f, i == selected ? 0.55f : 1.0f,
                                    i == selected ? 0.0f : 1.0f, 1.0f};
            float size = menu::kModeLabelSize;
            const float room = width - 32.0f;
            const float natural = font.measure(menu::modeLabel(i),
                                               menu::scaleForSize(font.metrics, size),
                                               menu::kLabelTracking);
            if (natural > room && natural > 0.0f) size *= room / natural;
            caption(menu::modeLabel(i), 480.0f,
                    menu::modeRowY(i) + menu::centreInBox(font.metrics, 0.0f, height, size),
                    size, color);
        }
        backButton(1.0f);
    }

    void superstarPage(const GameData &data, int page, int selected, int page_count) {
        backdrop("background_2.png");
        heading("SELECT SUPERSTAR", std::array<float, 4>{1.0f, 0.55f, 0.0f, 1.0f}.data());
        const int first = page * menu::kGridPageSize;
        const int count = static_cast<int>(data.wrestlers().size());
        for (int slot = 0; slot < menu::kGridPageSize; ++slot) {
            const int index = first + slot;
            if (index >= count) break;
            const WrestlerProfile &profile = data.wrestlers()[static_cast<std::size_t>(index)];
            const int column = slot / menu::kGridRows;
            const int row = slot % menu::kGridRows;
            const float x = menu::superstarCardX(column);
            const float y = menu::superstarCardY(row);
            Image frame;
            if (image(std::string("ui/Sprites/UI/selector/") +
                          (slot == selected ? "superstar_select.png" : "superstar_frame.png"),
                      &frame)) {
                canvas.blit(frame.rgba, frame.width, frame.height, x, y, menu::kCardSize,
                            menu::kCardSize, false);
            }
            Image avatar;
            if (image("graphics/Sprites/Character/" + profile.folder + "/avatar.png", &avatar)) {
                const menu::CoverCrop crop = menu::avatarCrop(
                    static_cast<float>(avatar.width), static_cast<float>(avatar.height));
                canvas.blitRegion(avatar.rgba, avatar.width, avatar.height,
                                  static_cast<int>(crop.u0 * avatar.width + 0.5f),
                                  static_cast<int>(crop.v0 * avatar.height + 0.5f),
                                  static_cast<int>((crop.u1 - crop.u0) * avatar.width + 0.5f),
                                  static_cast<int>((crop.v1 - crop.v0) * avatar.height + 0.5f),
                                  x + menu::kAvatarWindowX, y + menu::kAvatarWindowY,
                                  menu::kAvatarWindowSize, menu::kAvatarWindowSize, false);
            }
            caption(profile.name_line1, x + menu::kCardSize * 0.5f, y + menu::kCardNameInkOffset,
                    menu::kCardNameSize, std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f}.data(),
                    menu::kCardNameTracking);
            if (!profile.name_line2.empty()) {
                caption(profile.name_line2, x + menu::kCardSize * 0.5f,
                        y + menu::kCardNameInkOffset + menu::kCardNameLineStep, menu::kCardNameSize,
                        std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f}.data(),
                        menu::kCardNameTracking);
            }
        }
        arrows(page, page_count);
        dots(page, page_count);
        backButton(1.0f);
    }

    // Road to WrestleMania ladder: mirrors MenuScreens::drawRoad.
    void road(const wrestlefest::road::Progress &progress, int cursor) {
        backdrop("background_2.png");
        heading("ROAD TO WRESTLEMANIA", std::array<float, 4>{1.0f, 0.55f, 0.0f, 1.0f}.data());
        const wrestlefest::road::Level *level = progress.current();
        const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        const float orange[4] = {1.0f, 0.55f, 0.0f, 1.0f};
        if (level == nullptr) {
            caption("THE ROAD IS COMPLETE", 480.0f, 300.0f, 40.0f, orange);
            backButton(1.0f);
            return;
        }
        caption(level->name, 480.0f, 90.0f, menu::kRoadLevelSize, white, menu::kHeadingTracking);
        const int index = std::clamp(cursor, 0, static_cast<int>(level->stages.size()) - 1);
        const wrestlefest::road::Stage &stage = level->stages[static_cast<std::size_t>(index)];
        Image art;
        if (image(std::string("ui/Sprites/UI/stage/") + stage.art + ".png", &art)) {
            canvas.blit(art.rgba, art.width, art.height, menu::kRoadCardX, menu::kRoadCardY,
                        menu::kRoadCardWidth, menu::kRoadCardHeight, false);
        }
        if (index > progress.stage) {
            const float dim[4] = {0.02f, 0.02f, 0.04f, 0.6f};
            canvas.fillRect(menu::kRoadCardX, menu::kRoadCardY, menu::kRoadCardWidth,
                            menu::kRoadCardHeight, dim);
        }
        arrows(index, static_cast<int>(level->stages.size()));
        const std::string title = "MATCH " + std::to_string(index + 1) + " / " +
                                  std::to_string(level->stages.size()) + "  -  " + stage.label;
        caption(title, 480.0f, menu::kRoadMatchInkTop, menu::kRoadMatchSize, white,
                menu::kLabelTracking);
        caption(index <= progress.stage ? "TAP THE CARD TO WRESTLE IT"
                                        : "WIN THE EARLIER MATCHES FIRST",
                480.0f, menu::kRoadPromptInkTop, menu::kRoadPromptSize, white);
        dots(index, static_cast<int>(level->stages.size()));
        backButton(1.0f);
    }

    void arenaPage(const GameData &data, int page, int selected, int page_count) {
        backdrop("background_2.png");
        heading("SELECT ARENA", std::array<float, 4>{1.0f, 0.55f, 0.0f, 1.0f}.data());
        const int first = page * menu::kArenaPageSize;
        const int count = static_cast<int>(data.arenas().size());
        for (int slot = 0; slot < menu::kArenaPageSize; ++slot) {
            const int index = first + slot;
            if (index >= count) break;
            const ArenaInfo &arena = data.arenas()[static_cast<std::size_t>(index)];
            const int column = slot / menu::kArenaRows;
            const int row = slot % menu::kArenaRows;
            const float x = menu::arenaCardX(column);
            const float y = menu::arenaCardY(row);
            Image frame;
            if (image(std::string("ui/Sprites/UI/selector/") +
                          (slot == selected ? "arena_select.png" : "arena_frame.png"),
                      &frame)) {
                canvas.blit(frame.rgba, frame.width, frame.height, x, y, menu::kArenaCardWidth,
                            menu::kArenaCardHeight, false);
            }
            Image scene;
            if (image("graphics/Sprites/Arena/" + arena.folder + "/scene.png", &scene)) {
                const menu::CoverCrop crop = menu::arenaSceneCrop(
                    static_cast<float>(scene.width), static_cast<float>(scene.height));
                canvas.blitRegion(scene.rgba, scene.width, scene.height,
                                  static_cast<int>(crop.u0 * scene.width + 0.5f),
                                  static_cast<int>(crop.v0 * scene.height + 0.5f),
                                  static_cast<int>((crop.u1 - crop.u0) * scene.width + 0.5f),
                                  static_cast<int>((crop.v1 - crop.v0) * scene.height + 0.5f),
                                  x + menu::kArenaSceneWindowX, y + menu::kArenaSceneWindowY,
                                  menu::kArenaSceneWindowWidth, menu::kArenaSceneWindowHeight,
                                  false);
            }
            caption(arena.name, x + menu::kArenaCardWidth * 0.5f, y + menu::kArenaNameInkOffset,
                    menu::kArenaNameSize, std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f}.data(),
                    menu::kArenaNameTracking);
        }
        arrows(page, page_count);
        dots(page, page_count);
        backButton(1.0f);
    }
};

void expandSst(const SstSprite &sprite, std::vector<std::uint8_t> *rgba) {
    rgba->assign(static_cast<std::size_t>(sprite.width) * sprite.height * 4, 0);
    for (std::size_t i = 0; i < sprite.indices.size(); ++i) {
        const std::uint8_t index = sprite.indices[i];
        if (index == 0) continue;
        (*rgba)[i * 4 + 0] = sprite.palette[index * 4 + 0];
        (*rgba)[i * 4 + 1] = sprite.palette[index * 4 + 1];
        (*rgba)[i * 4 + 2] = sprite.palette[index * 4 + 2];
        (*rgba)[i * 4 + 3] = sprite.palette[index * 4 + 3];
    }
}

struct PreviewActor {
    const WrestlerProfile *profile = nullptr;
    std::string frame;
    float x = 0.0f;
    float z = 0.0f;
    float facing = 1.0f;
};

}  // namespace

int main(int argc, char **argv) {
    const std::string assets = argc > 1 ? argv[1] : "android_project/app/src/main/assets";
    const std::string out = argc > 2 ? argv[2] : "/tmp/preview.png";

    // An optional trailing --arena=<FolderName> renders another venue; every
    // arena0.png is 1482x678-679, so the shared calibration holds.
    std::string arena_folder = "ARENA02";
    std::string screen;
    std::string scene;
    // --entrance=<AllEntranceSceneTable id> composites the entrance cutscene.
    std::vector<int> crowd_frames;  // the crowd's animation phase, per instance
    // --road-level=N / --road-stage=N place the Road ladder's cursor.
    wrestlefest::road::Progress road_progress;
    bool entrance_screen = false;
    int entrance_scene_id = 0;
    int entrance_show_time = 90;  // sim steps of the entrance to play out
    int menu_page = 0;
    int menu_selected = 0;
    std::vector<std::string> wrestler_folders;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--arena=", 0) == 0) {
            arena_folder = arg.substr(8);
        } else if (arg.rfind("--screen=", 0) == 0) {
            screen = arg.substr(9);
        } else if (arg.rfind("--scene=", 0) == 0) {
            scene = arg.substr(8);
        } else if (arg.rfind("--page=", 0) == 0) {
            menu_page = std::atoi(arg.substr(7).c_str());
        } else if (arg.rfind("--selected=", 0) == 0) {
            menu_selected = std::atoi(arg.substr(11).c_str());
        } else if (arg.rfind("--road-level=", 0) == 0) {
            road_progress.level = std::atoi(arg.substr(13).c_str());
        } else if (arg.rfind("--road-stage=", 0) == 0) {
            road_progress.stage = std::atoi(arg.substr(13).c_str());
        } else if (arg.rfind("--entrance=", 0) == 0) {
            entrance_screen = true;
            entrance_scene_id = std::atoi(arg.substr(11).c_str());
        } else if (arg.rfind("--entrance-time=", 0) == 0) {
            entrance_show_time = std::atoi(arg.substr(16).c_str());
        } else {
            wrestler_folders.push_back(arg);
        }
    }

    if (!screen.empty()) {
        Font font;
        if (!font.load(assets, "Century Gothic")) {
            std::printf("cannot load the menu font\n");
            return 1;
        }
        Canvas menu_canvas;
        menu_canvas.fill(0, 0, 0, 255);
        MenuPreview menu_preview{assets, menu_canvas, font};
        if (screen == "loading") {
            menu_preview.loading();
        } else if (screen == "title") {
            menu_preview.title();
        } else if (screen == "main") {
            menu_preview.mainMenu(menu_selected);
        } else if (screen == "modes") {
            menu_preview.modes(menu_selected);
        } else {
            DirectoryAssetSource menu_source(assets + "/data/Data");
            GameData menu_data;
            std::string menu_error;
            if (!menu_data.load(&menu_source, "", &menu_error)) {
                std::printf("data load failed: %s\n", menu_error.c_str());
                return 1;
            }
            if (screen == "select") {
                const int pages = std::max(1, (static_cast<int>(menu_data.wrestlers().size()) + 7) / 8);
                menu_preview.superstarPage(menu_data, std::min(menu_page, pages - 1),
                                           menu_selected, pages);
            } else if (screen == "road") {
                menu_preview.road(road_progress, menu_selected);
            } else if (screen == "arena") {
                const int pages = std::max(1, (static_cast<int>(menu_data.arenas().size()) + 3) / 4);
                menu_preview.arenaPage(menu_data, std::min(menu_page, pages - 1), menu_selected,
                                       pages);
            } else {
                std::printf("unknown screen %s\n", screen.c_str());
                return 1;
            }
        }
        if (!writePng(out, menu_canvas.rgba)) {
            std::printf("cannot write %s\n", out.c_str());
            return 1;
        }
        std::printf("wrote %s (%s)\n", out.c_str(), screen.c_str());
        return 0;
    }

    Canvas canvas;
    canvas.fill(5, 5, 8, 255);

    std::vector<std::uint8_t> bytes;
    Image crowd;
    if (readFile(assets + "/graphics/Sprites/Arena/" + arena_folder + "/ground.png", &bytes) &&
        decodePng(bytes.data(), bytes.size(), &crowd)) {
        // The crowd/floor sheet is the backdrop behind the ring; its bottom edge
        // is aligned with the bottom of the logical canvas.
        canvas.blit(crowd.rgba, crowd.width, crowd.height, kOriginX,
                    640.0f - crowd.height * kArtScale, crowd.width * kArtScale,
                    crowd.height * kArtScale, false);
    }
    Image arena;
    bytes.clear();
    if (readFile(assets + "/graphics/Sprites/Arena/" + arena_folder + "/arena0.png", &bytes) &&
        decodePng(bytes.data(), bytes.size(), &arena)) {
        canvas.blit(arena.rgba, arena.width, arena.height, kOriginX, kOriginY,
                    arena.width * kArtScale, arena.height * kArtScale, false);
    } else {
        std::printf("no arena art\n");
        return 1;
    }

    DirectoryAssetSource source(assets + "/data/Data");
    GameData data;
    std::string error;
    if (!data.load(&source, "", &error)) {
        std::printf("data load failed: %s\n", error.c_str());
        return 1;
    }

    const RingGeometry ring;
    Match sim_match;
    const bool use_sim_scene = !scene.empty();
    if (use_sim_scene) {
        sim_match.configure(&data, ring, 0, 0x5150u);
        const std::string f0 = wrestler_folders.size() > 0 ? wrestler_folders[0] : "00_John Cena";
        const std::string f1 = wrestler_folders.size() > 1 ? wrestler_folders[1] : "03_BigShow";
        sim_match.addWrestler(data.wrestlerByFolder(f0), 0, ring.centerX() - 95.0f,
                              ring.centerZ() + 14.0f, 1.0f);
        sim_match.addWrestler(data.wrestlerByFolder(f1), 1, ring.centerX() + 95.0f,
                              ring.centerZ() + 14.0f, -1.0f);
        sim_match.setAiControlled(0, false);
        sim_match.setAiControlled(1, false);
        std::vector<InputFrame> inputs(2);

        if (scene == "entrance") {
            sim_match.allowAutoStart(false);
            sim_match.playEntrance();
            sim_match.setAnnouncerCue(AnnouncerCue::AnnounceLeft);
            for (int step = 0; step < 30; ++step) sim_match.update(1.0f / 60.0f, inputs);
        } else if (scene == "close_combat") {
            sim_match.start();
            for (int step = 0; step < 24; ++step) {
                inputs[0] = InputFrame{};
                inputs[1] = InputFrame{};
                inputs[0].right = true;
                inputs[1].left = true;
                sim_match.update(1.0f / 60.0f, inputs);
            }
            inputs[0] = InputFrame{};
            inputs[0].btn0 = true;
            inputs[0].btn0_edge = true;
            for (int step = 0; step < 5; ++step) {
                sim_match.update(1.0f / 60.0f, inputs);
                inputs[0].btn0_edge = false;
            }
        } else if (scene == "grapple") {
            sim_match.start();
            for (int step = 0; step < 20; ++step) {
                inputs[0] = InputFrame{};
                inputs[1] = InputFrame{};
                inputs[0].right = true;
                inputs[1].left = true;
                sim_match.update(1.0f / 60.0f, inputs);
            }
            inputs[0] = InputFrame{};
            inputs[0].right = true;
            inputs[0].btn0 = true;
            inputs[0].btn0_edge = true;
            for (int step = 0; step < 12; ++step) {
                sim_match.update(1.0f / 60.0f, inputs);
                inputs[0].btn0_edge = false;
            }
        } else if (scene == "pin_slap") {
            sim_match.start();
            Wrestler *a = sim_match.wrestler(0);
            Wrestler *b = sim_match.wrestler(1);
            a->x = ring.centerX() - 48.0f;
            b->x = ring.centerX() + 12.0f;
            b->energy = 10.0f;
            b->energy_display = 10.0f;
            b->changeState(state::ONGROUND, 0, sim_match);
            inputs[0].btn1 = true;
            inputs[0].btn1_edge = true;
            sim_match.update(1.0f / 60.0f, inputs);
            inputs[0] = InputFrame{};
            for (int step = 0; step < 180; ++step) {
                sim_match.update(1.0f / 60.0f, inputs);
                if (sim_match.referee().count() == 2 && sim_match.referee().spriteId() == 6) {
                    break;
                }
            }
        }
    }

    Image shadow_img;
    bytes.clear();
    const bool have_shadow =
        readFile(assets + "/graphics/Sprites/Arena/" + arena_folder + "/shadow.png", &bytes) &&
        decodePng(bytes.data(), bytes.size(), &shadow_img);

    SmlDocument refdoc;
    std::vector<std::uint8_t> refbytes;
    const bool have_refdoc =
        readFile(assets + "/graphics/Sprites/Character/Ref/refData.sml", &refbytes) &&
        refdoc.parse(reinterpret_cast<const char *>(refbytes.data()), refbytes.size(), &error);

    SmlDocument anndoc;
    std::vector<std::uint8_t> annbytes;
    const bool have_anndoc =
        readFile(assets + "/graphics/Sprites/Character/Announcer/Robert/announcerData.sml",
                 &annbytes) &&
        anndoc.parse(reinterpret_cast<const char *>(annbytes.data()), annbytes.size(), &error);

    auto drawWrestlerFrame = [&](const WrestlerProfile *profile, int sst_id, float wx, float wz,
                                 float wfacing, float scale = kArtScale) {
        if (profile == nullptr) return;
        const SpriteEntry *sprite = profile->spriteFor(sst_id);
        if (sprite == nullptr) return;
        std::vector<std::uint8_t> zip_bytes;
        ZipArchive archive;
        std::vector<std::uint8_t> sst_bytes;
        SstSprite decoded;
        std::vector<std::uint8_t> rgba;
        const std::string path =
            assets + "/asset_bundles/Sprites/Character/" + profile->folder + "/wrestler.zip";
        if (!readFile(path, &zip_bytes) || !archive.open(zip_bytes.data(), zip_bytes.size()) ||
            !archive.read(sprite->name, &sst_bytes) ||
            !decodeSst(sst_bytes.data(), sst_bytes.size(), &decoded)) {
            return;
        }
        expandSst(decoded, &rgba);
        if (have_shadow) {
            const float sw = 96.0f * kArtScale;
            const float sh = 26.0f * kArtScale;
            canvas.blit(shadow_img.rgba, shadow_img.width, shadow_img.height, wx - sw * 0.5f,
                        wz - sh * 0.62f, sw, sh, false, 0.55f);
        }
        const float width = decoded.width * scale;
        const float height = decoded.height * scale;
        const float left = wfacing >= 0.0f ? wx - sprite->x_offset * scale
                                           : wx - (decoded.width - sprite->x_offset) * scale;
        const float top = wz - sprite->y_offset * scale;
        canvas.blit(rgba, decoded.width, decoded.height, left, top, width, height, wfacing < 0.0f);
    };

    // ---- Entrance cutscene (CGameEntrance::onDraw) ------------------------
    // The stage art is 1482x711 like the ring art, so it is placed the same way;
    // the arena's own crowd sheet fills the canvas under it and the men walk out
    // on the show's depth line. See GameRenderer::entranceFor / drawEntrance.
    if (entrance_screen) {
        const EntranceSceneInfo *info = data.entranceScene(entrance_scene_id);
        if (info == nullptr) {
            std::printf("no entrance scene %d\n", entrance_scene_id);
            return 1;
        }
        const std::string folder =
            assets + "/graphics/Sprites/Arena/entrance/" + info->folder + "/";

        Canvas ec;
        ec.fill(4, 4, 6, 255);
        bytes.clear();
        if (readFile(assets + "/graphics/Sprites/Arena/" + arena_folder + "/ground.png", &bytes) &&
            decodePng(bytes.data(), bytes.size(), &crowd)) {
            ec.blit(crowd.rgba, crowd.width, crowd.height, kOriginX,
                    640.0f - crowd.height * kArtScale, crowd.width * kArtScale,
                    crowd.height * kArtScale, false);
        }
        Image stage_art;
        bytes.clear();
        if (!readFile(folder + info->scene_file, &bytes) ||
            !decodePng(bytes.data(), bytes.size(), &stage_art)) {
            std::printf("cannot read %s%s\n", folder.c_str(), info->scene_file.c_str());
            return 1;
        }
        const float art_w = static_cast<float>(stage_art.width);
        const float art_h = static_cast<float>(stage_art.height);
        const float origin_x = 480.0f - art_w * 0.5f * kArtScale;
        const float origin_y = 0.0f;
        ec.blit(stage_art.rgba, stage_art.width, stage_art.height, origin_x, origin_y,
                art_w * kArtScale, art_h * kArtScale, false);
        for (const EntranceAttachObject &object : info->attach) {
            Image piece;
            bytes.clear();
            if (!readFile(folder + object.file, &bytes) ||
                !decodePng(bytes.data(), bytes.size(), &piece)) {
                continue;
            }
            ec.blit(piece.rgba, piece.width, piece.height, origin_x + object.offset_x * kArtScale,
                    origin_y + object.offset_y * kArtScale, piece.width * kArtScale,
                    piece.height * kArtScale, false);
        }

        // The crowd, drawn in front of the stage art (CCrowd::Draw(CrowdId)).
        {
            const std::vector<CrowdInstance> *crowd = data.crowd(info->crowd_id);
            if (crowd != nullptr) {
                struct CrowdZip {
                    std::vector<std::uint8_t> bytes;
                    ZipArchive archive;
                    bool ok = false;
                };
                static std::unordered_map<int, CrowdZip> zips;
                std::vector<int> &frames = crowd_frames;
                if (frames.size() != crowd->size()) {
                    frames.resize(crowd->size());
                    for (std::size_t i = 0; i < frames.size(); ++i) {
                        frames[i] = static_cast<int>(i) & 7;
                    }
                }
                // Instances are drawn back to front.
                std::vector<std::size_t> order(crowd->size());
                for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
                std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                    return (*crowd)[a].y < (*crowd)[b].y;
                });
                for (std::size_t slot : order) {
                    const CrowdInstance &ci = (*crowd)[slot];
                    CrowdZip &zip = zips[ci.crowd_id];
                    if (!zip.ok) {
                        char zname[32];
                        std::snprintf(zname, sizeof(zname), "p%02d.zip", ci.crowd_id + 1);
                        zip.ok = readFile(assets +
                                              "/asset_bundles/Sprites/Arena/crowd/" + zname,
                                          &zip.bytes) &&
                                 zip.archive.open(zip.bytes.data(), zip.bytes.size());
                    }
                    if (!zip.ok) continue;
                    char fname[48];
                    std::snprintf(fname, sizeof(fname), "ph_P%02d%c_%02d.sst", ci.crowd_id + 1,
                                  static_cast<char>('a' + ci.dir), frames[slot] + 1);
                    std::vector<std::uint8_t> sst_bytes;
                    SstSprite decoded;
                    std::vector<std::uint8_t> rgba;
                    if (!zip.archive.read(fname, &sst_bytes) ||
                        !decodeSst(sst_bytes.data(), sst_bytes.size(), &decoded)) {
                        continue;
                    }
                    expandSst(decoded, &rgba);
                    const CrowdSpriteOffset *offset =
                        data.crowdOffset(ci.crowd_id, ci.dir, frames[slot]);
                    const float ax = (offset != nullptr ? offset->x : 0.0f) * kArtScale * ci.scale;
                    const float ay = (offset != nullptr ? offset->y : 0.0f) * kArtScale * ci.scale;
                    const float w = decoded.width * kArtScale * ci.scale;
                    const float h = decoded.height * kArtScale * ci.scale;
                    const float left = origin_x + ci.x * kArtScale -
                                       (ci.facing < 1 ? w - ax : ax);
                    ec.blit(rgba, decoded.width, decoded.height, left,
                            origin_y + ci.y * kArtScale - ay, w, h, ci.facing < 1);
                }
            }
        }

        // The wrestlers, playing their own entrance animations on the stage.
        const RingGeometry ering;
        Match ematch;
        ematch.configure(&data, ering, 0, 0x5150u);
        ematch.allowAutoStart(false);
        const std::string f0 = wrestler_folders.size() > 0 ? wrestler_folders[0] : "00_John Cena";
        const std::string f1 = wrestler_folders.size() > 1 ? wrestler_folders[1] : "03_BigShow";
        const bool tag = wrestler_folders.size() > 3;
        const float start_x = info->resolvedStartX(art_w);
        const float start_z = info->resolvedStartZ(art_h);
        const float gap = info->resolvedGap();
        ematch.addWrestler(data.wrestlerByFolder(f0), 0, ering.centerX(), ering.centerZ(), 1.0f);
        ematch.addWrestler(data.wrestlerByFolder(f1), 1, ering.centerX(), ering.centerZ(), 1.0f);
        if (tag) {
            ematch.addWrestler(data.wrestlerByFolder(wrestler_folders[2]), 0, ering.centerX(),
                               ering.centerZ(), 1.0f);
            ematch.addWrestler(data.wrestlerByFolder(wrestler_folders[3]), 1, ering.centerX(),
                               ering.centerZ(), 1.0f);
        }
        for (int i = 0; i < ematch.wrestlerCount(); ++i) ematch.setAiControlled(i, false);
        ematch.playEntrance();
        std::vector<InputFrame> einputs(static_cast<std::size_t>(ematch.wrestlerCount()));
        for (int step = 0; step < entrance_show_time; ++step) {
            ematch.update(1.0f / 60.0f, einputs);
        }

        const float feet_y = origin_y + start_z * kArtScale;
        const float actor_scale = kArtScale * (info->obj_scale > 0.0f ? info->obj_scale : 1.0f);
        auto putOnStage = [&](int index, float art_x) {
            const Wrestler *w = ematch.wrestler(index);
            if (w == nullptr || w->profile() == nullptr) return;
            drawWrestlerFrame(w->profile(), w->spriteId(), origin_x + art_x * kArtScale, feet_y,
                              1.0f, actor_scale);
        };
        Canvas *saved = nullptr;
        (void)saved;
        // drawWrestlerFrame targets `canvas`; swap the buffers round it.
        std::swap(canvas.rgba, ec.rgba);
        putOnStage(0, tag ? start_x - gap : start_x);
        putOnStage(1, tag ? start_x + gap : start_x);
        if (tag) {
            putOnStage(2, start_x - gap * 3.0f);
            putOnStage(3, start_x + gap * 3.0f);
        }
        std::swap(canvas.rgba, ec.rgba);

        // The name plate: the rule, the WWE mark, and the names in yellow.
        {
            const float rule_y = 640.0f - 50.0f;
            const float rule_w = 960.0f * 0.82f;
            const float black[4] = {0.02f, 0.02f, 0.02f, 0.9f};
            const float red[4] = {0.76f, 0.08f, 0.03f, 1.0f};
            ec.fillRect(52.0f, rule_y + 2.0f, rule_w, 5.0f, black);
            ec.fillRect(50.0f, rule_y, rule_w, 5.0f, red);
            Image logo;
            bytes.clear();
            if (readFile(assets + "/ui/Sprites/UI/title_menu/wwe_logo_for_shop.png", &bytes) &&
                decodePng(bytes.data(), bytes.size(), &logo)) {
                const float lw = logo.width * 1.2f;
                const float lh = logo.height * 1.2f;
                ec.blit(logo.rgba, logo.width, logo.height, rule_w + 50.0f - lw * 0.25f,
                        rule_y - logo.height * 0.9f, lw, lh, false);
            }
            Font efont;
            if (efont.load(assets, "Century Gothic")) {
                const float tint[4] = {1.0f, 0.85f, 0.1f, 1.0f};
                const float shadow[4] = {0.0f, 0.0f, 0.0f, 0.8f};
                const float scale = 40.0f /
                                    (efont.metrics.cell_height > 0.0f
                                         ? efont.metrics.cell_height
                                         : 24.0f);
                auto line = [&](const std::string &text, float ink_top) {
                    if (text.empty()) return;
                    efont.draw(ec, 49.0f, ink_top + 1.0f, scale, text, shadow);
                    efont.draw(ec, 50.0f, ink_top, scale, text, tint);
                };
                const Wrestler *w = ematch.wrestler(0);
                const Wrestler *p = tag ? ematch.wrestler(2) : nullptr;
                line(p != nullptr && p->profile() != nullptr ? p->profile()->name : std::string(),
                     640.0f - 150.0f);
                line(w != nullptr && w->profile() != nullptr ? w->profile()->name : std::string(),
                     640.0f - 100.0f);
            }
        }

        if (!writePng(out, ec.rgba)) {
            std::printf("cannot write %s\n", out.c_str());
            return 1;
        }
        std::printf("wrote %s (entrance %d: %s)\n", out.c_str(), entrance_scene_id,
                    info->folder.c_str());
        return 0;
    }

    auto drawOfficialFrame = [&](const SmlDocument &doc, const std::string &dir, int row, float ox,
                                 float oz, float ofacing, float alpha) {
        if (row < 0 || row >= static_cast<int>(doc.records.size())) return;
        const SmlRecord &record = doc.records[static_cast<std::size_t>(row)];
        std::vector<std::uint8_t> png;
        Image img;
        const std::string name = record.string("name");
        if (!readFile(dir + name, &png) || !decodePng(png.data(), png.size(), &img)) return;
        if (have_shadow) {
            const float sw = 92.0f * kArtScale;
            const float sh = 25.0f * kArtScale;
            canvas.blit(shadow_img.rgba, shadow_img.width, shadow_img.height, ox - sw * 0.5f,
                        oz - sh * 0.62f, sw, sh, false, 0.5f * alpha);
        }
        const float xoff = static_cast<float>(record.number("xoffset"));
        const float yoff = static_cast<float>(record.number("yoffset"));
        const float w = img.width * kArtScale;
        const float h = img.height * kArtScale;
        const float left = ofacing >= 0.0f ? ox - xoff * kArtScale
                                           : ox - (img.width - xoff) * kArtScale;
        canvas.blit(img.rgba, img.width, img.height, left, oz - yoff * kArtScale, w, h,
                    ofacing < 0.0f, alpha);
    };

    if (use_sim_scene) {
        enum class Kind { Wrestler, Ref, Announcer };
        struct Item {
            float z;
            int prio;
            Kind kind;
            int idx;
        };
        std::vector<Item> items;
        for (int i = 0; i < sim_match.wrestlerCount(); ++i) {
            const Wrestler *w = sim_match.wrestler(i);
            items.push_back({w->z, w->isDown() ? -2 : 0, Kind::Wrestler, i});
        }
        items.push_back({sim_match.referee().z, 1, Kind::Ref, 0});
        if (sim_match.announcer().visible()) {
            items.push_back({sim_match.announcer().z, 0, Kind::Announcer, 0});
        }
        std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
            if (a.z != b.z) return a.z < b.z;
            return a.prio < b.prio;
        });
        for (const Item &item : items) {
            if (item.kind == Kind::Wrestler) {
                const Wrestler *w = sim_match.wrestler(item.idx);
                drawWrestlerFrame(w->profile(), w->spriteId(), w->x, w->z - w->y, w->facing);
            } else if (item.kind == Kind::Ref && have_refdoc) {
                const Referee &ref = sim_match.referee();
                drawOfficialFrame(refdoc, assets + "/graphics/Sprites/Character/Ref/",
                                  ref.spriteId(), ref.x, ref.z - ref.y, ref.facing, 1.0f);
            } else if (item.kind == Kind::Announcer && have_anndoc) {
                const RingAnnouncer &ann = sim_match.announcer();
                drawOfficialFrame(anndoc,
                                  assets + "/graphics/Sprites/Character/Announcer/Robert/",
                                  ann.spriteId(), ann.x, ann.z - ann.y, ann.facing, ann.alpha());
            }
        }
    } else {
        std::vector<PreviewActor> actors;
        const int extra = static_cast<int>(wrestler_folders.size());
        for (int i = 0; i < 2; ++i) {
            PreviewActor actor;
            actor.profile =
                data.wrestlerByFolder(i < extra ? wrestler_folders[static_cast<std::size_t>(i)]
                                                : (i == 0 ? "00_John Cena" : "03_BigShow"));
            if (actor.profile == nullptr) continue;
            actor.frame = "idle.sst";
            actor.x = ring.centerX() + (i == 0 ? -110.0f : 110.0f);
            actor.z = ring.centerZ() + 20.0f;
            actor.facing = i == 0 ? 1.0f : -1.0f;
            actors.push_back(actor);
        }

        std::sort(actors.begin(), actors.end(),
                  [](const PreviewActor &a, const PreviewActor &b) { return a.z < b.z; });
        if (have_refdoc) {
            // Row 0 is mike_1.png (standing watch pose), sorted behind z=484 actors.
            drawOfficialFrame(refdoc, assets + "/graphics/Sprites/Character/Ref/", 0,
                              ring.centerX() + 40.0f, 412.0f, -1.0f, 1.0f);
        }
        for (const PreviewActor &actor : actors) {
            const int sst_id = actor.profile->sprite_index.count(actor.frame)
                                   ? actor.profile->sprite_index.at(actor.frame)
                                   : -1;
            drawWrestlerFrame(actor.profile, sst_id, actor.x, actor.z, actor.facing);
        }
    }

    Image ropes;
    bytes.clear();
    if (readFile(assets + "/graphics/Sprites/Arena/" + arena_folder + "/ropes0_bottom.png", &bytes) &&
        decodePng(bytes.data(), bytes.size(), &ropes)) {
        const float width = ropes.width * kArtScale;
        const float height = ropes.height * kArtScale;
        canvas.blit(ropes.rgba, ropes.width, ropes.height, 480.0f - width * 0.5f,
                    560.0f - height + 18.0f, width, height, false);
    }

    // ---- HUD -------------------------------------------------------------
    // Same numbers as GameRenderer::drawHud / drawTouchControls.
    auto blitPng = [&](const std::string &path, float x, float y, float w, float h, bool flip,
                       float alpha) {
        std::vector<std::uint8_t> file;
        Image image;
        if (!readFile(assets + "/" + path, &file) ||
            !decodePng(file.data(), file.size(), &image)) {
            std::printf("missing %s\n", path.c_str());
            return false;
        }
        canvas.blit(image.rgba, image.width, image.height, x, y, w, h, flip, alpha);
        return true;
    };

    const float bar_scale = 0.55f;
    const float bar_w = 815.0f * bar_scale;
    const float bar_h = 83.0f * bar_scale;
    for (int i = 0; i < 2; ++i) {
        const bool left = i == 0;
        const float x = left ? 6.0f : 960.0f - 6.0f - bar_w;
        blitPng("ui/Sprites/UI/energy_bar/UI_energy01_background.png", x, 4.0f, bar_w, bar_h,
                !left, 1.0f);
        float value = left ? 0.7f : 0.45f;
        if (use_sim_scene && sim_match.wrestler(i) != nullptr) {
            const Wrestler *w = sim_match.wrestler(i);
            value = std::clamp(w->energy / std::max(1.0f, w->energy_max), 0.0f, 1.0f);
        }
        const float inner_x = 76.0f * bar_scale;
        const float inner_y = 32.0f * bar_scale;
        const float inner_w = 718.0f * bar_scale;
        const float inner_h = 19.0f * bar_scale;
        const float left_x = left ? x + inner_x : x + inner_x + inner_w * (1.0f - value);
        const char *fill_path = value <= 0.3f ? "ui/Sprites/UI/energy_bar/UI_energy01_2.png"
                                              : "ui/Sprites/UI/energy_bar/UI_energy01_1.png";
        blitPng(fill_path, left_x, 4.0f + inner_y,
                inner_w * value, inner_h, !left, 1.0f);
        blitPng(left ? "ui/Sprites/UI/energy_bar/UI_1P.png"
                     : "ui/Sprites/UI/energy_bar/UI_2P.png",
                left ? x + 18.0f * bar_scale : x + bar_w - 18.0f * bar_scale - 31.0f, 4.0f + 33.0f * bar_scale,
                29.0f, 16.0f, false, 1.0f);
    }
    // Timer: live remaining clock for simulated scenes, 0:59 for static mock.
    {
        int remain_sec = 59;
        if (use_sim_scene) {
            const float limit = sim_match.rule().max_match_time > 0.0f
                                    ? sim_match.rule().max_match_time
                                    : 360.0f;
            remain_sec = std::max(0, static_cast<int>(std::ceil(limit - sim_match.clock())));
        }
        const int mins = std::clamp(remain_sec / 60, 0, 9);
        const int tens = std::clamp((remain_sec % 60) / 10, 0, 9);
        const int ones = std::clamp(remain_sec % 10, 0, 9);
        const std::string d0 = "TIME_" + std::to_string(mins);
        const std::string d1 = "TIME_" + std::to_string(tens);
        const std::string d2 = "TIME_" + std::to_string(ones);
        const float digit_w = 23.0f;
        const float gap = 1.0f;
        const float total_w = digit_w * 4 + gap * 3 + 12.0f;
        float x = 480.0f - total_w * 0.5f;
        const char *names[4] = {d0.c_str(), "TIME_middle_mark", d1.c_str(), d2.c_str()};
        for (int i = 0; i < 4; ++i) {
            const bool mark = std::strcmp(names[i], "TIME_middle_mark") == 0;
            blitPng(std::string("ui/Sprites/UI/count/") + names[i] + ".png", x, mark ? 16.0f : 8.0f,
                    mark ? 8.0f : digit_w, mark ? 27.0f : 43.0f, false, 1.0f);
            x += mark ? 12.0f : digit_w + gap;
        }
    }
    // Virtual pad, at the positions the input code hit-tests.
    blitPng("graphics/Sprites/virtual_pad/joystick_base.png", 200.0f - 92.0f, 400.0f - 92.0f,
            184.0f, 184.0f, false, 0.55f);
    blitPng("graphics/Sprites/virtual_pad/joystick_icon.png", 200.0f - 32.0f, 400.0f - 32.0f, 64.0f,
            64.0f, false, 0.75f);
    blitPng("graphics/Sprites/virtual_pad/blue_btn_base.png", 565.0f - 52.0f, 425.0f - 52.0f,
            104.0f, 104.0f, false, 0.6f);
    blitPng("graphics/Sprites/virtual_pad/red_btn_base.png", 735.0f - 62.0f, 340.0f - 62.0f, 124.0f,
            124.0f, false, 0.6f);

    if (use_sim_scene && sim_match.referee().showingPinCount()) {
        const int value = sim_match.referee().count();
        if (value >= 1 && value <= 9) {
            const float scale = 2.4f * sim_match.referee().countPopScale();
            const float w = 23.0f * scale;
            const float h = 43.0f * scale;
            blitPng("ui/Sprites/UI/count/TIME_" + std::to_string(value) + ".png",
                    480.0f - w * 0.5f, 241.5f - h * 0.5f, w, h, false, 1.0f);
        }
    }

    if (!writePng(out, canvas.rgba)) {
        std::printf("cannot write %s\n", out.c_str());
        return 1;
    }
    std::printf("wrote %s\n", out.c_str());
    return 0;
}
