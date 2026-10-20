#ifndef WRESTLEFEST_RENDER_IMAGE_H
#define WRESTLEFEST_RENDER_IMAGE_H

// Texture decoders for the two image formats in the game data.
//
// PNG   - everything the UI and the arenas are made of.
// SST   - the sprite container behind every animation frame. Reversed from
//         tyCTBuffer::_AllocSST (WrestleFest Premium.c:67915):
//
//           0  u32 magic 0x73737400 ("\0tss")
//           4  u16 version (must be 1)
//           6  u16 unknown (1)
//           8  u32 format   low nibble: 1 = A8, 2 = RGB565, 3 = palettized,
//                          4 = RGBA5551; high nibble for palettes: 0x20 =
//                          RGBA5551 palette, 0x30 = RGB565 palette
//          12  u32 width
//          16  u32 height
//          20  128 colours * u16   (palettized formats only)
//          ..  width*height indices/pixels, row-major, top-down
//
// Palettized images are 8-bit index sprites drawn through the palette texture,
// which is what makes the power-up colour blink (PaletteOffset) cheap.

#include <cstdint>
#include <string>
#include <vector>

namespace wrestlefest {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  // width*height*4, straight alpha
    bool empty() const { return width <= 0 || height <= 0; }
};

struct SstSprite {
    int width = 0;
    int height = 0;
    bool palettized = false;
    std::uint32_t format = 0;
    std::vector<std::uint8_t> indices;   // palettized: width*height indices
    std::vector<std::uint8_t> rgba;      // direct colour: width*height*4
    std::uint8_t palette[128 * 4]{};     // rgba, 128 entries
};

// Decodes an 8-bit PNG (grey/RGB/palette/alpha) - enough for the shipped art.
bool decodePng(const std::uint8_t *data, std::size_t size, Image *out);

// Decodes a .sst animation frame.
bool decodeSst(const std::uint8_t *data, std::size_t size, SstSprite *out);

}  // namespace wrestlefest

#endif  // WRESTLEFEST_RENDER_IMAGE_H
