#ifndef WRESTLEFEST_RENDER_GL_SPRITE_H
#define WRESTLEFEST_RENDER_GL_SPRITE_H

// Minimal GLES3 sprite path for the game's assets.
//
// The original draws everything through one of two techniques: "Sprite"
// (straight RGBA) and "SpritePalette" (an 8-bit index texture + a 128-colour
// palette, which is how the power-up flash is done - the shader just shifts the
// palette lookup). Both are implemented here; `drawIndexed` sets uUsePalette.

#include "image.h"

#include <GLES3/gl3.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace wrestlefest {

struct Texture {
    GLuint id = 0;
    int width = 0;
    int height = 0;
    bool valid() const { return id != 0; }
};

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

class SpriteBatch {
public:
    bool initialize();
    void shutdown();

    // Screen-space setup: the game's logical canvas is 960x640, top-left origin.
    void begin(int viewport_x, int viewport_y, int viewport_w, int viewport_h, int logical_w,
               int logical_h);
    void end();

    Texture uploadRgba(const Image &image);
    // Palettized frames upload the index map plus a 128x1 palette texture.
    Texture uploadIndexMap(const SstSprite &sprite);
    Texture uploadPalette(const std::uint8_t *rgba128);
    Texture uploadWhite();

    void draw(const Texture &texture, float x, float y, float w, float h, const Color &color,
              bool flip_x = false);
    void drawIndexed(const Texture &indices, const Texture &palette, float x, float y, float w,
                     float h, const Color &color, bool flip_x = false, float palette_offset = 0.0f);
    // Sub-rectangle of an atlas texture (fonts, energy bar fills).
    void drawRegion(const Texture &texture, float x, float y, float w, float h, float u0, float v0,
                    float u1, float v1, const Color &color, bool flip_x = false);
    // Whole texture about its centre, rotated clockwise by `degrees` (the
    // loading screen tilts the logo the way the original's attract loop does).
    void drawRotated(const Texture &texture, float center_x, float center_y, float w, float h,
                     float degrees, const Color &color);

private:
    void pushQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                  const Color &color, bool flip_x);
    void flush();

    GLuint program_ = 0;
    GLuint vbo_ = 0;
    GLint u_projection_ = -1;
    GLint u_use_palette_ = -1;
    GLint u_color_ = -1;
    GLint u_texture_ = -1;
    GLint u_palette_ = -1;
    GLint u_palette_offset_ = -1;

    std::vector<float> vertices_;  // x,y,u,v,r,g,b,a
    GLuint current_texture_ = 0;
    GLuint current_palette_ = 0;
    bool current_palette_mode_ = false;
    float current_palette_offset_ = 0.0f;
    float projection_[16]{};
    Texture white_;
};

// Vertical metrics of a tyF font, in the descriptor's own units (the cell is
// `cell_height` tall). The ink box is measured from the atlas at load time, so
// a caption can be positioned on its capital letters instead of its cell.
struct FontMetrics {
    float cell_height = 24.0f;
    float ink_top = 0.0f;     // cell top -> top of the capitals
    float cap_height = 0.0f;  // height of the capitals
    bool measured = false;
};

// A tyF bitmap font: <Font> Header{texture} + CharTable[256]{w,h,preSpace,
// postSpace,u0,v0,u1,v1}.
class BitmapFont {
public:
    bool load(const std::string &tyf_text, const Image &atlas, SpriteBatch *batch);
    // `tracking` is extra advance per glyph, in font units (the original's
    // menus space their captions out a little).
    float measure(const std::string &text, float scale, float tracking = 0.0f) const;
    // `bold_px` thickens the strokes the way the original's canvas text looks:
    // the glyph is stamped at eight sub-pixel offsets around the pen (0 = off).
    void draw(SpriteBatch *batch, float x, float y, float scale, const std::string &text,
              const Color &color, float tracking = 0.0f, float bold_px = 0.0f) const;
    float lineHeight(float scale) const { return 42.0f * scale; }
    bool valid() const { return texture_.valid(); }
    const FontMetrics &metrics() const { return metrics_; }

private:
    struct Glyph {
        float w = 0.0f;
        float h = 0.0f;
        float pre_space = 0.0f;
        float post_space = 0.0f;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
    };
    // Ink extents of 'H' inside its cell, converted back to font units. Every
    // glyph of a tyF atlas shares one cell height, so this is enough to place
    // any caption on its capitals.
    void measureInk(const Image &atlas);
    Glyph glyphs_[256]{};
    Texture texture_;
    FontMetrics metrics_;
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_RENDER_GL_SPRITE_H
