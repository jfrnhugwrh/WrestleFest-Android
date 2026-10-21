#include "gl_sprite.h"

#include "../data/sml_parser.h"

#include <android/log.h>

#include <cmath>
#include <cstring>

namespace wrestlefest {
namespace {

const char *kVertexShader = R"(#version 300 es
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
uniform mat4 u_projection;
out vec2 v_uv;
out vec4 v_color;
void main() {
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
}
)";

// Mirrors graphics/Shaders/SpritePalette.fsh: the sprite texture holds palette
// indices, the palette texture is 128 texels wide.
const char *kFragmentShader = R"(#version 300 es
precision mediump float;
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_texture;
uniform sampler2D u_palette;
uniform int u_use_palette;
uniform float u_palette_offset;
out vec4 outColor;
void main() {
    if (u_use_palette == 1) {
        float index = texture(u_texture, v_uv).r * 255.0;
        float slot = floor(index + 0.5) + u_palette_offset;
        vec4 colour = texture(u_palette, vec2((slot + 0.5) / 128.0, 0.5));
        outColor = colour * v_color;
    } else {
        outColor = texture(u_texture, v_uv) * v_color;
    }
}
)";

GLuint compile(GLenum type, const char *source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        __android_log_print(ANDROID_LOG_ERROR, "WrestleFestRender", "shader: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

}  // namespace

bool SpriteBatch::initialize() {
    const GLuint vertex = compile(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, kFragmentShader);
    if (vertex == 0 || fragment == 0) return false;

    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);
    GLint linked = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (linked != GL_TRUE) return false;

    u_projection_ = glGetUniformLocation(program_, "u_projection");
    u_use_palette_ = glGetUniformLocation(program_, "u_use_palette");
    u_color_ = glGetUniformLocation(program_, "u_color");
    u_texture_ = glGetUniformLocation(program_, "u_texture");
    u_palette_ = glGetUniformLocation(program_, "u_palette");
    u_palette_offset_ = glGetUniformLocation(program_, "u_palette_offset");

    glGenBuffers(1, &vbo_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    Image white_image;
    white_image.width = 1;
    white_image.height = 1;
    white_image.rgba = {255, 255, 255, 255};
    white_ = uploadRgba(white_image);
    return true;
}

void SpriteBatch::shutdown() {
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    if (white_.id != 0) {
        glDeleteTextures(1, &white_.id);
        white_ = Texture{};
    }
}

Texture SpriteBatch::uploadRgba(const Image &image) {
    Texture texture;
    if (image.empty()) return texture;
    glGenTextures(1, &texture.id);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image.rgba.data());
    texture.width = image.width;
    texture.height = image.height;
    return texture;
}

Texture SpriteBatch::uploadIndexMap(const SstSprite &sprite) {
    Texture texture;
    if (sprite.width <= 0 || sprite.height <= 0 || sprite.indices.empty()) return texture;
    glGenTextures(1, &texture.id);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    // Nearest keeps the palette indices exact (linear filtering would blend
    // neighbouring indices and produce wrong colours).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, sprite.width, sprite.height, 0, GL_RED, GL_UNSIGNED_BYTE,
                 sprite.indices.data());
    texture.width = sprite.width;
    texture.height = sprite.height;
    return texture;
}

Texture SpriteBatch::uploadPalette(const std::uint8_t *rgba128) {
    Texture texture;
    glGenTextures(1, &texture.id);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba128);
    texture.width = 128;
    texture.height = 1;
    return texture;
}

Texture SpriteBatch::uploadWhite() { return white_; }

void SpriteBatch::begin(int viewport_x, int viewport_y, int viewport_w, int viewport_h,
                        int logical_w, int logical_h) {
    glViewport(viewport_x, viewport_y, viewport_w, viewport_h);
    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Ortho, top-left origin, y down (matches the logical touch space).
    std::memset(projection_, 0, sizeof(projection_));
    projection_[0] = 2.0f / static_cast<float>(logical_w);
    projection_[5] = -2.0f / static_cast<float>(logical_h);
    projection_[10] = 1.0f;
    projection_[12] = -1.0f;
    projection_[13] = 1.0f;
    projection_[15] = 1.0f;

    vertices_.clear();
    current_texture_ = 0;
    current_palette_ = 0;
    current_palette_mode_ = false;
    current_palette_offset_ = 0.0f;

    glUseProgram(program_);
    glUniformMatrix4fv(u_projection_, 1, GL_FALSE, projection_);
    glUniform1i(u_texture_, 0);
    glUniform1i(u_palette_, 1);
}

void SpriteBatch::pushQuad(float x, float y, float w, float h, float u0, float v0, float u1,
                           float v1, const Color &color, bool flip_x) {
    if (flip_x) {
        const float t = u0;
        u0 = u1;
        u1 = t;
    }
    const float x1 = x + w;
    const float y1 = y + h;
    const float quad[6][4] = {{x, y, u0, v0},  {x1, y, u1, v0}, {x, y1, u0, v1},
                              {x1, y, u1, v0}, {x1, y1, u1, v1}, {x, y1, u0, v1}};
    for (const auto &vertex : quad) {
        vertices_.push_back(vertex[0]);
        vertices_.push_back(vertex[1]);
        vertices_.push_back(vertex[2]);
        vertices_.push_back(vertex[3]);
        vertices_.push_back(color.r);
        vertices_.push_back(color.g);
        vertices_.push_back(color.b);
        vertices_.push_back(color.a);
    }
}

void SpriteBatch::flush() {
    if (vertices_.empty()) return;
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices_.size() * sizeof(float)), vertices_.data(),
                 GL_STREAM_DRAW);
    const GLsizei stride = 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void *>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void *>(4 * sizeof(float)));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, current_texture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, current_palette_);
    glUniform1i(u_use_palette_, current_palette_mode_ ? 1 : 0);
    glUniform1f(u_palette_offset_, current_palette_offset_);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size() / 8));
    vertices_.clear();
}

void SpriteBatch::draw(const Texture &texture, float x, float y, float w, float h,
                       const Color &color, bool flip_x) {
    drawRegion(texture, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, color, flip_x);
}

void SpriteBatch::drawRotated(const Texture &texture, float center_x, float center_y, float w,
                              float h, float degrees, const Color &color) {
    if (!texture.valid() || w <= 0.0f || h <= 0.0f) return;
    if (current_texture_ != texture.id || current_palette_mode_) {
        flush();
        current_texture_ = texture.id;
        current_palette_ = 0;
        current_palette_mode_ = false;
    }
    // Corners in local space, then rotated about the centre. Screen y grows
    // downwards, so a positive angle turns clockwise on screen.
    const float radians = degrees * 3.14159265358979f / 180.0f;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float hw = w * 0.5f;
    const float hh = h * 0.5f;
    const float corners[4][2] = {{-hw, -hh}, {hw, -hh}, {-hw, hh}, {hw, hh}};
    float x[4];
    float y[4];
    for (int i = 0; i < 4; ++i) {
        x[i] = center_x + corners[i][0] * c - corners[i][1] * s;
        y[i] = center_y + corners[i][0] * s + corners[i][1] * c;
    }
    const float u[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    const float v[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    const int order[6] = {0, 1, 2, 1, 3, 2};
    for (int i : order) {
        vertices_.push_back(x[i]);
        vertices_.push_back(y[i]);
        vertices_.push_back(u[i]);
        vertices_.push_back(v[i]);
        vertices_.push_back(color.r);
        vertices_.push_back(color.g);
        vertices_.push_back(color.b);
        vertices_.push_back(color.a);
    }
}

void SpriteBatch::drawRegion(const Texture &texture, float x, float y, float w, float h, float u0,
                             float v0, float u1, float v1, const Color &color, bool flip_x) {
    if (!texture.valid()) return;
    // Coalesce runs that share a texture; anything else flushes first.
    if (current_texture_ != texture.id || current_palette_mode_) {
        flush();
        current_texture_ = texture.id;
        current_palette_ = 0;
        current_palette_mode_ = false;
    }
    pushQuad(x, y, w, h, u0, v0, u1, v1, color, flip_x);
}

void SpriteBatch::drawIndexed(const Texture &indices, const Texture &palette, float x, float y,
                              float w, float h, const Color &color, bool flip_x,
                              float palette_offset) {
    if (!indices.valid()) return;
    if (current_texture_ != indices.id || !current_palette_mode_ ||
        current_palette_ != palette.id || current_palette_offset_ != palette_offset) {
        flush();
        current_texture_ = indices.id;
        current_palette_ = palette.id;
        current_palette_mode_ = true;
        current_palette_offset_ = palette_offset;
    }
    pushQuad(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, color, flip_x);
}

void SpriteBatch::end() { flush(); }

// ---------------------------------------------------------------------------
// BitmapFont
// ---------------------------------------------------------------------------
bool BitmapFont::load(const std::string &tyf_text, const Image &atlas, SpriteBatch *batch) {
    SmlDocument doc;
    std::string error;
    if (!doc.parse(tyf_text.data(), tyf_text.size(), &error)) return false;
    const SmlArray *table = doc.array("CharTable");
    if (table == nullptr || table->records.size() < 256) return false;
    for (int i = 0; i < 256; ++i) {
        const SmlRecord &record = table->records[static_cast<std::size_t>(i)];
        Glyph &glyph = glyphs_[i];
        glyph.w = static_cast<float>(record.number("w"));
        glyph.h = static_cast<float>(record.number("h"));
        glyph.pre_space = static_cast<float>(record.number("preSpace"));
        glyph.post_space = static_cast<float>(record.number("postSpace"));
        glyph.u0 = static_cast<float>(record.number("u0"));
        glyph.v0 = static_cast<float>(record.number("v0"));
        glyph.u1 = static_cast<float>(record.number("u1"));
        glyph.v1 = static_cast<float>(record.number("v1"));
    }
    measureInk(atlas);
    texture_ = batch->uploadRgba(atlas);
    return texture_.valid();
}

void BitmapFont::measureInk(const Image &atlas) {
    const Glyph &h = glyphs_[static_cast<unsigned char>('H')];
    metrics_ = FontMetrics{};
    metrics_.cell_height = h.h > 0.0f ? h.h : 24.0f;
    if (atlas.width <= 0 || atlas.height <= 0 || h.w <= 0.0f || h.h <= 0.0f) return;
    const int x0 = static_cast<int>(h.u0 * atlas.width);
    const int y0 = static_cast<int>(h.v0 * atlas.height);
    const int x1 = static_cast<int>(std::lround(h.u1 * atlas.width));
    const int y1 = static_cast<int>(std::lround(h.v1 * atlas.height));
    int top = -1;
    int bottom = -1;
    for (int y = y0; y < y1 && y < atlas.height; ++y) {
        for (int x = x0; x < x1 && x < atlas.width; ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * atlas.width + x) * 4 + 3;
            if (index < atlas.rgba.size() && atlas.rgba[index] > 40) {
                if (top < 0) top = y;
                bottom = y;
                break;
            }
        }
    }
    if (top < 0 || y1 <= y0) return;
    const float cell_px = static_cast<float>(y1 - y0);
    metrics_.ink_top = (top - y0) * h.h / cell_px;
    metrics_.cap_height = (bottom - top + 1) * h.h / cell_px;
    metrics_.measured = true;
}

float BitmapFont::measure(const std::string &text, float scale, float tracking) const {
    float width = 0.0f;
    for (char c : text) {
        const Glyph &glyph = glyphs_[static_cast<unsigned char>(c)];
        width += (glyph.pre_space + glyph.w + glyph.post_space + tracking) * scale;
    }
    return width;
}

void BitmapFont::draw(SpriteBatch *batch, float x, float y, float scale, const std::string &text,
                      const Color &color, float tracking, float bold_px) const {
    if (batch == nullptr || !texture_.valid()) return;
    // The stamps used for the faux-bold ring, in logical pixels.
    static const float kRing[8][2] = {{1.0f, 0.0f},  {-1.0f, 0.0f}, {0.0f, 1.0f},  {0.0f, -1.0f},
                                      {0.7f, 0.7f},  {-0.7f, 0.7f}, {0.7f, -0.7f}, {-0.7f, -0.7f}};
    float pen = x;
    for (char c : text) {
        const Glyph &glyph = glyphs_[static_cast<unsigned char>(c)];
        pen += glyph.pre_space * scale;
        if (glyph.w > 0.0f && glyph.h > 0.0f) {
            if (bold_px > 0.0f) {
                for (const auto &offset : kRing) {
                    batch->drawRegion(texture_, pen + offset[0] * bold_px,
                                      y + offset[1] * bold_px, glyph.w * scale, glyph.h * scale,
                                      glyph.u0, glyph.v0, glyph.u1, glyph.v1, color);
                }
            }
            batch->drawRegion(texture_, pen, y, glyph.w * scale, glyph.h * scale, glyph.u0,
                              glyph.v0, glyph.u1, glyph.v1, color);
        }
        pen += (glyph.w + glyph.post_space + tracking) * scale;
    }
}

}  // namespace wrestlefest
