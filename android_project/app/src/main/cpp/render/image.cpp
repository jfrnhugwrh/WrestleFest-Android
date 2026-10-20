#include "image.h"

#include <cstring>

// The Android build links the NDK's zlib. Host builds have the runtime library
// but not always the headers, so the two entry points we need are declared here
// (ABI-identical to zlib's prototypes).
extern "C" {
using wrestlefest_uLong = unsigned long;
int uncompress(unsigned char *dest, wrestlefest_uLong *destLen, const unsigned char *source,
               wrestlefest_uLong sourceLen);
}

namespace wrestlefest {
namespace {

std::uint32_t readU32(const std::uint8_t *p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::uint16_t readU16(const std::uint8_t *p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

bool inflateAll(const std::uint8_t *data, std::size_t size, std::vector<std::uint8_t> *out,
                std::size_t expected) {
    out->resize(expected);
    wrestlefest_uLong dest_len = static_cast<wrestlefest_uLong>(expected);
    const int rc = uncompress(out->data(), &dest_len, data,
                              static_cast<wrestlefest_uLong>(size));
    if (rc != 0 /* Z_OK */) return false;
    out->resize(static_cast<std::size_t>(dest_len));
    return dest_len == expected;
}

}  // namespace

bool decodePng(const std::uint8_t *data, std::size_t size, Image *out) {
    static const std::uint8_t kMagic[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (size < 8 || std::memcmp(data, kMagic, 8) != 0) return false;

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    int bit_depth = 0;
    int colour_type = 0;
    int interlace = 0;
    std::vector<std::uint8_t> palette;
    std::vector<std::uint8_t> idat;

    std::size_t pos = 8;
    while (pos + 8 <= size) {
        const std::uint32_t length = readU32(data + pos);
        const std::uint8_t *type = data + pos + 4;
        const std::uint8_t *chunk = data + pos + 8;
        pos += 12 + length;
        if (pos > size) return false;

        if (std::memcmp(type, "IHDR", 4) == 0) {
            width = readU32(chunk);
            height = readU32(chunk + 4);
            bit_depth = chunk[8];
            colour_type = chunk[9];
            interlace = chunk[12];
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            palette.assign(chunk, chunk + length);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), chunk, chunk + length);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            // Palette transparency: append alpha entries after the RGB palette.
            const std::size_t entries = palette.size() / 3;
            palette.resize(entries * 4, 255);
            for (std::size_t i = 0; i < entries; ++i) {
                palette[i * 4 + 3] = i < length ? chunk[i] : 255;
            }
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
    }
    if (width == 0 || height == 0 || (bit_depth != 8 && bit_depth != 16) || interlace != 0) {
        return false;
    }

    int channels = 0;
    switch (colour_type) {
        case 0: channels = 1; break;
        case 2: channels = 3; break;
        case 3: channels = 1; break;
        case 4: channels = 2; break;
        case 6: channels = 4; break;
        default: return false;
    }

    // Indexed-color PNGs only use 8-bit samples; 16-bit RGB/RGBA PNGs are
    // common in the older DLC arenas and are reduced to their high byte when
    // uploading to the game's 8-bit RGBA textures.
    if (colour_type == 3 && bit_depth != 8) return false;
    const std::size_t bytes_per_sample = bit_depth == 16 ? 2U : 1U;
    const std::size_t stride = static_cast<std::size_t>(width) * channels * bytes_per_sample;
    std::vector<std::uint8_t> raw;
    if (!inflateAll(idat.data(), idat.size(), &raw, (stride + 1) * height)) return false;

    // Un-filter using the source pixel width (including both bytes of 16-bit
    // samples), then convert to 8-bit RGBA below.
    std::vector<std::uint8_t> pixels(stride * height);
    const std::size_t bpp = static_cast<std::size_t>(channels) * bytes_per_sample;
    for (std::uint32_t y = 0; y < height; ++y) {
        const int filter = raw[y * (stride + 1)];
        const std::uint8_t *src = raw.data() + y * (stride + 1) + 1;
        std::uint8_t *dst = pixels.data() + y * stride;
        const std::uint8_t *prev = y > 0 ? pixels.data() + (y - 1) * stride : nullptr;
        for (std::size_t x = 0; x < stride; ++x) {
            const int a = x >= bpp ? dst[x - bpp] : 0;
            const int b = prev != nullptr ? prev[x] : 0;
            const int c = (prev != nullptr && x >= bpp) ? prev[x - bpp] : 0;
            int value = src[x];
            switch (filter) {
                case 0: break;
                case 1: value += a; break;
                case 2: value += b; break;
                case 3: value += (a + b) / 2; break;
                case 4: value += paeth(a, b, c); break;
                default: return false;
            }
            dst[x] = static_cast<std::uint8_t>(value);
        }
    }

    out->width = static_cast<int>(width);
    out->height = static_cast<int>(height);
    out->rgba.resize(static_cast<std::size_t>(width) * height * 4);
    const bool palette_has_alpha = palette.size() == 256 * 4;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint8_t *dst = out->rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            const std::uint8_t *src =
                pixels.data() + y * stride + x * channels * bytes_per_sample;
            auto sample = [src, bytes_per_sample](int channel) {
                // PNG 16-bit samples are network byte order. Keeping the most
                // significant byte gives a fast, deterministic 8-bit preview.
                return src[static_cast<std::size_t>(channel) * bytes_per_sample];
            };
            if (colour_type == 3) {
                const std::size_t index = sample(0);
                if ((index + 1) * 3 > palette.size() && !palette_has_alpha) return false;
                dst[0] = palette[index * (palette_has_alpha ? 4 : 3) + 0];
                dst[1] = palette[index * (palette_has_alpha ? 4 : 3) + 1];
                dst[2] = palette[index * (palette_has_alpha ? 4 : 3) + 2];
                dst[3] = palette_has_alpha ? palette[index * 4 + 3] : 255;
            } else if (colour_type == 0 || colour_type == 4) {
                dst[0] = dst[1] = dst[2] = sample(0);
                dst[3] = channels == 2 ? sample(1) : 255;
            } else {
                dst[0] = sample(0);
                dst[1] = sample(1);
                dst[2] = sample(2);
                dst[3] = channels == 4 ? sample(3) : 255;
            }
        }
    }
    return true;
}

// --- SST -------------------------------------------------------------------
bool decodeSst(const std::uint8_t *data, std::size_t size, SstSprite *out) {
    if (size < 20) return false;
    const std::uint32_t magic = static_cast<std::uint32_t>(data[0]) |
                                (static_cast<std::uint32_t>(data[1]) << 8) |
                                (static_cast<std::uint32_t>(data[2]) << 16) |
                                (static_cast<std::uint32_t>(data[3]) << 24);
    if (magic != 0x73737400u) return false;
    if (readU16(data + 4) != 1) return false;

    // Header (verified against tyCTBuffer::_AllocSST):
    //   8 width, 12 height, 16 format, 18 pad.
    const std::uint32_t width = readU16(data + 8) | (static_cast<std::uint32_t>(data[10]) << 16);
    const std::uint32_t height = readU16(data + 12) | (static_cast<std::uint32_t>(data[14]) << 16);
    const std::uint16_t format = readU16(data + 16);
    if (width == 0 || height == 0 || width > 4096 || height > 4096) return false;

    const std::uint32_t format_lo = format & 0x0fu;
    const std::uint32_t palette_format = format & 0xf0u;
    out->format = format;
    out->palettized = format_lo == 3;
    out->width = static_cast<int>(width);
    out->height = static_cast<int>(height);

    std::size_t offset = 20;
    if (out->palettized) {
        // 128-colour palette, 16 bits per entry. The nibble variants match the
        // GL upload formats in the engine: 0x20 RGBA5551, 0x30 RGB565, 0x40 RGBA4444.
        if (size < offset + 256) return false;
        for (int i = 0; i < 128; ++i) {
            const std::uint16_t colour = readU16(data + offset + i * 2);
            std::uint8_t r = 0;
            std::uint8_t g = 0;
            std::uint8_t b = 0;
            std::uint8_t a = 255;
            if (palette_format == 0x30) {  // RGB565
                r = static_cast<std::uint8_t>(((colour >> 11) & 0x1f) * 255 / 31);
                g = static_cast<std::uint8_t>(((colour >> 5) & 0x3f) * 255 / 63);
                b = static_cast<std::uint8_t>((colour & 0x1f) * 255 / 31);
            } else if (palette_format == 0x40) {  // RGBA4444
                r = static_cast<std::uint8_t>(((colour >> 12) & 0xf) * 17);
                g = static_cast<std::uint8_t>(((colour >> 8) & 0xf) * 17);
                b = static_cast<std::uint8_t>(((colour >> 4) & 0xf) * 17);
                a = static_cast<std::uint8_t>((colour & 0xf) * 17);
            } else {  // RGBA5551 (0x20)
                r = static_cast<std::uint8_t>(((colour >> 11) & 0x1f) * 255 / 31);
                g = static_cast<std::uint8_t>(((colour >> 6) & 0x1f) * 255 / 31);
                b = static_cast<std::uint8_t>(((colour >> 1) & 0x1f) * 255 / 31);
                a = static_cast<std::uint8_t>((colour & 0x1) ? 255 : 0);
            }
            out->palette[i * 4 + 0] = r;
            out->palette[i * 4 + 1] = g;
            out->palette[i * 4 + 2] = b;
            out->palette[i * 4 + 3] = a;
        }
        offset += 256;
    }

    const std::size_t count = static_cast<std::size_t>(width) * height;
    if (out->palettized) {
        if (size < offset + count) return false;
        out->indices.assign(data + offset, data + offset + count);
        return true;
    }

    out->rgba.assign(count * 4, 0);
    if (format_lo == 4 && size >= offset + count * 2) {  // RGBA5551
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint16_t v = readU16(data + offset + i * 2);
            out->rgba[i * 4 + 0] = static_cast<std::uint8_t>(((v >> 11) & 0x1f) * 255 / 31);
            out->rgba[i * 4 + 1] = static_cast<std::uint8_t>(((v >> 6) & 0x1f) * 255 / 31);
            out->rgba[i * 4 + 2] = static_cast<std::uint8_t>(((v >> 1) & 0x1f) * 255 / 31);
            out->rgba[i * 4 + 3] = static_cast<std::uint8_t>((v & 0x1) ? 255 : 0);
        }
    } else if (format_lo == 2 && size >= offset + count * 2) {  // RGB565
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint16_t v = readU16(data + offset + i * 2);
            out->rgba[i * 4 + 0] = static_cast<std::uint8_t>(((v >> 11) & 0x1f) * 255 / 31);
            out->rgba[i * 4 + 1] = static_cast<std::uint8_t>(((v >> 5) & 0x3f) * 255 / 63);
            out->rgba[i * 4 + 2] = static_cast<std::uint8_t>((v & 0x1f) * 255 / 31);
            out->rgba[i * 4 + 3] = 255;
        }
    } else if (format_lo == 1 && size >= offset + count) {  // A8 luminance
        for (std::size_t i = 0; i < count; ++i) {
            out->rgba[i * 4 + 0] = out->rgba[i * 4 + 1] = out->rgba[i * 4 + 2] = 255;
            out->rgba[i * 4 + 3] = data[offset + i];
        }
    } else {
        return false;
    }
    return true;
}

}  // namespace wrestlefest
