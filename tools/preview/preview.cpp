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

#include <algorithm>
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
    void blit(const std::vector<std::uint8_t> &image, int iw, int ih, float x, float y, float w,
              float h, bool flip_x, float alpha = 1.0f) {
        if (iw <= 0 || ih <= 0 || w <= 0.0f || h <= 0.0f) return;
        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const int x1 = static_cast<int>(std::ceil(x + w));
        const int y1 = static_cast<int>(std::ceil(y + h));
        for (int py = y0; py < y1; ++py) {
            const int sy = static_cast<int>((py + 0.5f - y) / h * ih);
            if (sy < 0 || sy >= ih) continue;
            for (int px = x0; px < x1; ++px) {
                int sx = static_cast<int>((px + 0.5f - x) / w * iw);
                if (sx < 0 || sx >= iw) continue;
                if (flip_x) sx = iw - 1 - sx;
                std::uint8_t pixel[4] = {image[(static_cast<std::size_t>(sy) * iw + sx) * 4 + 0],
                                         image[(static_cast<std::size_t>(sy) * iw + sx) * 4 + 1],
                                         image[(static_cast<std::size_t>(sy) * iw + sx) * 4 + 2],
                                         image[(static_cast<std::size_t>(sy) * iw + sx) * 4 + 3]};
                pixel[3] = static_cast<std::uint8_t>(pixel[3] * alpha);
                blendPixel(px, py, pixel);
            }
        }
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
    std::vector<std::string> wrestler_folders;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--arena=", 0) == 0) {
            arena_folder = arg.substr(8);
        } else {
            wrestler_folders.push_back(arg);
        }
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
