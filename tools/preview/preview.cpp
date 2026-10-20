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
#include "../../android_project/app/src/main/cpp/sim/collision.h"
#include "../../android_project/app/src/main/cpp/game/menu_layout.h"

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
            if (menu::mainEntryLeftAligned(i)) {
                const float scale = menu::scaleForSize(font.metrics, menu::kMainLabelSize);
                const float shadow_color[4] = {0.0f, 0.0f, 0.0f, 0.75f};
                font.draw(canvas, menu::kShopLabelX + menu::kLabelShadow,
                          menu::inkTopToCellTop(font.metrics, ink_top, menu::kMainLabelSize) +
                              menu::kLabelShadow,
                          scale, menu::mainEntryLabel(i), shadow_color, menu::kLabelTracking);
                font.draw(canvas, menu::kShopLabelX,
                          menu::inkTopToCellTop(font.metrics, ink_top, menu::kMainLabelSize), scale,
                          menu::mainEntryLabel(i), color, menu::kLabelTracking);
            } else {
                caption(menu::mainEntryLabel(i), 480.0f, ink_top, menu::kMainLabelSize, color);
            }
        }
        Image wwe;
        if (image("ui/Sprites/UI/title_menu/wwe_logo_for_shop.png", &wwe)) {
            canvas.blit(wwe.rgba, wwe.width, wwe.height, menu::kWweLogoX, menu::kWweLogoY,
                        wwe.width * menu::kWweLogoScale, wwe.height * menu::kWweLogoScale, false);
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
                const menu::Placement place = menu::avatarPlacement(
                    static_cast<float>(avatar.width), static_cast<float>(avatar.height));
                canvas.blit(avatar.rgba, avatar.width, avatar.height, x + place.x, y + place.y,
                            place.width, place.height, false);
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
                const menu::Placement place = menu::arenaScenePlacement(
                    static_cast<float>(scene.width), static_cast<float>(scene.height));
                canvas.blit(scene.rgba, scene.width, scene.height, x + place.x, y + place.y,
                            place.width, place.height, false);
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

struct Actor {
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
    int menu_page = 0;
    int menu_selected = 0;
    std::vector<std::string> wrestler_folders;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--arena=", 0) == 0) {
            arena_folder = arg.substr(8);
        } else if (arg.rfind("--screen=", 0) == 0) {
            screen = arg.substr(9);
        } else if (arg.rfind("--page=", 0) == 0) {
            menu_page = std::atoi(arg.substr(7).c_str());
        } else if (arg.rfind("--selected=", 0) == 0) {
            menu_selected = std::atoi(arg.substr(11).c_str());
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
    std::vector<Actor> actors;
    const int extra = static_cast<int>(wrestler_folders.size());
    for (int i = 0; i < 2; ++i) {
        Actor actor;
        actor.profile = data.wrestlerByFolder(i < extra ? wrestler_folders[static_cast<std::size_t>(i)]
                                                        : (i == 0 ? "00_John Cena"
                                                                  : "03_BigShow"));
        if (actor.profile == nullptr) continue;
        actor.frame = "idle.sst";
        actor.x = ring.centerX() + (i == 0 ? -110.0f : 110.0f);
        actor.z = ring.centerZ() + 20.0f;
        actor.facing = i == 0 ? 1.0f : -1.0f;
        actors.push_back(actor);
    }

    // Same order as GameRenderer::drawMatch: depth sort, then draw.
    std::sort(actors.begin(), actors.end(),
              [](const Actor &a, const Actor &b) { return a.z < b.z; });

    for (const Actor &actor : actors) {
        const int sst_id = actor.profile->sprite_index.count(actor.frame)
                               ? actor.profile->sprite_index.at(actor.frame)
                               : -1;
        const SpriteEntry *sprite = actor.profile->spriteFor(sst_id);
        if (sprite == nullptr) continue;
        std::vector<std::uint8_t> zip_bytes;
        ZipArchive archive;
        std::vector<std::uint8_t> sst_bytes;
        SstSprite decoded;
        std::vector<std::uint8_t> rgba;
        const std::string path =
            assets + "/asset_bundles/Sprites/Character/" + actor.profile->folder + "/wrestler.zip";
        if (!readFile(path, &zip_bytes) || !archive.open(zip_bytes.data(), zip_bytes.size()) ||
            !archive.read(sprite->name, &sst_bytes) ||
            !decodeSst(sst_bytes.data(), sst_bytes.size(), &decoded)) {
            std::printf("frame %s missing for %s\n", sprite->name.c_str(),
                        actor.profile->folder.c_str());
            continue;
        }
        expandSst(decoded, &rgba);
        const float width = decoded.width * kArtScale;
        const float height = decoded.height * kArtScale;
        const float ground = actor.z;
        const float left = actor.facing >= 0.0f
                               ? actor.x - sprite->x_offset * kArtScale
                               : actor.x - (decoded.width - sprite->x_offset) * kArtScale;
        const float top = ground - sprite->y_offset * kArtScale;
        canvas.blit(rgba, decoded.width, decoded.height, left, top, width, height,
                    actor.facing < 0.0f);
        std::printf("%s %s: %.0fx%.0f at %.1f,%.1f (offset %.1f,%.1f)\n",
                    actor.profile->name.c_str(), sprite->name.c_str(), width, height, left, top,
                    sprite->x_offset, sprite->y_offset);
    }

    // ---- Referee ---------------------------------------------------------
    {
        SmlDocument refdoc;
        std::vector<std::uint8_t> refbytes;
        if (readFile(assets + "/graphics/Sprites/Character/Ref/refData.sml", &refbytes) &&
            refdoc.parse(reinterpret_cast<const char *>(refbytes.data()), refbytes.size(), &error)) {
            const int row = 10;  // mike_1, the standing walk pose
            if (row < static_cast<int>(refdoc.records.size())) {
                const SmlRecord &record = refdoc.records[static_cast<std::size_t>(row)];
                std::vector<std::uint8_t> png;
                Image ref_image;
                const std::string name = record.string("name");
                if (readFile(assets + "/graphics/Sprites/Character/Ref/" + name, &png) &&
                    decodePng(png.data(), png.size(), &ref_image)) {
                    const float xoff = static_cast<float>(record.number("xoffset"));
                    const float yoff = static_cast<float>(record.number("yoffset"));
                    const float ref_x = ring.centerX() + 40.0f;
                    const float ref_z = 470.0f;
                    const float w = ref_image.width * kArtScale;
                    const float h = ref_image.height * kArtScale;
                    canvas.blit(ref_image.rgba, ref_image.width, ref_image.height,
                                ref_x - xoff * kArtScale, ref_z - yoff * kArtScale, w, h, false);
                    std::printf("referee %s: %.0fx%.0f at %.1f,%.1f\n", name.c_str(), w, h,
                                ref_x - xoff * kArtScale, ref_z - yoff * kArtScale);
                }
            }
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
        // 70 % energy for the player, 45 % for the CPU.
        const float value = left ? 0.7f : 0.45f;
        const float inner_x = 76.0f * bar_scale;
        const float inner_y = 32.0f * bar_scale;
        const float inner_w = 718.0f * bar_scale;
        const float inner_h = 19.0f * bar_scale;
        const float left_x = left ? x + inner_x : x + inner_x + inner_w * (1.0f - value);
        blitPng("ui/Sprites/UI/energy_bar/UI_energy01_1.png", left_x, 4.0f + inner_y,
                inner_w * value, inner_h, !left, 1.0f);
        blitPng(left ? "ui/Sprites/UI/energy_bar/UI_1P.png"
                     : "ui/Sprites/UI/energy_bar/UI_2P.png",
                left ? x + 18.0f * bar_scale : x + bar_w - 18.0f * bar_scale - 31.0f, 4.0f + 33.0f * bar_scale,
                29.0f, 16.0f, false, 1.0f);
    }
    // Timer: 0:59 -> TIME_0 TIME_middle TIME_5 TIME_9.
    {
        const float digit_w = 23.0f;
        const float gap = 1.0f;
        const float total_w = digit_w * 4 + gap * 3 + 12.0f;
        float x = 480.0f - total_w * 0.5f;
        const char *names[4] = {"TIME_0", "TIME_middle_mark", "TIME_5", "TIME_9"};
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

    if (!writePng(out, canvas.rgba)) {
        std::printf("cannot write %s\n", out.c_str());
        return 1;
    }
    std::printf("wrote %s\n", out.c_str());
    return 0;
}
