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

// A tyF bitmap font: <Font> Header{texture} + CharTable[256]{w,h,preSpace,
// postSpace,u0,v0,u1,v1}.
class BitmapFont {
public:
    bool load(const std::string &tyf_text, const Image &atlas, SpriteBatch *batch);
    float measure(const std::string &text, float scale) const;
    void draw(SpriteBatch *batch, float x, float y, float scale, const std::string &text,
              const Color &color) const;
    float lineHeight(float scale) const { return 42.0f * scale; }
    bool valid() const { return texture_.valid(); }

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
    Glyph glyphs_[256]{};
    Texture texture_;
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_RENDER_GL_SPRITE_H
