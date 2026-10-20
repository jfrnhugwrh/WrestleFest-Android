// Asset pipeline acceptance tests: PNG decoding, SST frame decoding and ZIP
// extraction against the real shipped assets.
//
// Run: ./image_test [assets directory]
//
// No GPU is involved: this exercises everything up to (but not including) the
// glTexImage2D calls in gl_sprite.cpp.

#include "../data/sml_parser.h"
#include "image.h"
#include "zip_reader.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace wrestlefest;

#ifndef WRESTLEFEST_ASSET_ROOT
#define WRESTLEFEST_ASSET_ROOT "android_project/app/src/main/assets"
#endif

namespace {

int g_failures = 0;

void check(bool condition, const std::string &what) {
    if (condition) {
        std::printf("  ok   %s\n", what.c_str());
    } else {
        std::printf("  FAIL %s\n", what.c_str());
        ++g_failures;
    }
}

bool readFile(const std::string &path, std::vector<std::uint8_t> *bytes) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(file);
        return false;
    }
    bytes->resize(static_cast<std::size_t>(size));
    const std::size_t read = std::fread(bytes->data(), 1, bytes->size(), file);
    std::fclose(file);
    bytes->resize(read);
    return read == static_cast<std::size_t>(size);
}

void testPng(const std::string &assets) {
    std::printf("[png]\n");
    std::vector<std::uint8_t> bytes;
    Image image;
    const std::string arena = assets + "/graphics/Sprites/Arena/ARENA02/arena0.png";
    if (readFile(arena, &bytes)) {
        check(decodePng(bytes.data(), bytes.size(), &image), "arena0.png decodes");
        check(image.width == 1482 && image.height == 679, "arena0.png is 1482x679");
        check(image.rgba.size() == static_cast<std::size_t>(image.width) * image.height * 4,
              "RGBA buffer has 4 bytes per pixel");
        bool opaque = false;
        for (std::size_t i = 3; i < image.rgba.size(); i += 4) {
            if (image.rgba[i] == 255) {
                opaque = true;
                break;
            }
        }
        check(opaque, "arena art has opaque pixels");
    } else {
        std::printf("  info %s missing\n", arena.c_str());
        ++g_failures;
    }

    // The HUD energy bar is a small RGBA PNG; make sure the decoder handles a
    // second colour type as well.
    Image bar;
    bytes.clear();
    const std::string energy = assets + "/ui/Sprites/UI/energy_bar/UI_energy01_background.png";
    if (readFile(energy, &bytes)) {
        check(decodePng(bytes.data(), bytes.size(), &bar), "energy bar decodes");
        check(bar.width == 815 && bar.height == 83, "energy bar is 815x83");
    }

    // The font atlas is the largest texture the UI uses.
    Image atlas;
    bytes.clear();
    const std::string font_png = assets + "/graphics/Fonts/Arial Rounded MT Bold.png";
    if (readFile(font_png, &bytes)) {
        check(decodePng(bytes.data(), bytes.size(), &atlas), "font atlas decodes");
        check(atlas.width == 512 && atlas.height == 512, "font atlas is 512x512");
    }

    // Truncated data must fail, not read out of bounds.
    if (!bytes.empty()) {
        Image broken;
        check(!decodePng(bytes.data(), bytes.size() / 3, &broken), "truncated PNG rejected");
    }
}

void testSst(const std::string &assets) {
    std::printf("[sst]\n");
    const std::string bundle =
        assets + "/asset_bundles/Sprites/Character/00_John Cena/wrestler.zip";
    std::vector<std::uint8_t> zip_bytes;
    if (!readFile(bundle, &zip_bytes)) {
        std::printf("  FAIL missing %s\n", bundle.c_str());
        ++g_failures;
        return;
    }

    ZipArchive archive;
    check(archive.open(zip_bytes.data(), zip_bytes.size()), "wrestler.zip opens");
    check(archive.entries().size() > 500, "wrestler.zip holds the whole move set");

    std::vector<std::uint8_t> sst_bytes;
    check(archive.read("idle.sst", &sst_bytes), "idle.sst extracted");
    SstSprite sprite;
    check(decodeSst(sst_bytes.data(), sst_bytes.size(), &sprite), "idle.sst decodes");
    check(sprite.width == 128 && sprite.height == 240, "idle.sst is 128x240");
    check(sprite.palettized, "idle.sst is palettized");
    check((sprite.format & 0xf0) == 0x40, "idle.sst palette is RGBA4444 (0x4_)");
    check(sprite.indices.size() == static_cast<std::size_t>(sprite.width) * sprite.height,
          "index map matches the frame size");
    int transparent = 0;
    int coloured = 0;
    for (std::uint8_t index : sprite.indices) {
        if (index == 0) {
            ++transparent;
        } else {
            ++coloured;
        }
    }
    check(transparent > 0 && coloured > 0, "frame has both transparent and solid pixels");

    // A second, differently sized frame: the walk cycle uses narrower sprites.
    std::vector<std::uint8_t> walk_bytes;
    if (archive.read("walk_a.sst", &walk_bytes)) {
        SstSprite walk;
        check(decodeSst(walk_bytes.data(), walk_bytes.size(), &walk), "walk_a.sst decodes");
        check(walk.width > 0 && walk.height > 0, "walk_a.sst has a size");
    }

    // Every entry in the bundle must be readable; this catches a bad central
    // directory or a deflate implementation that gives up on some streams.
    int failures = 0;
    int frames = 0;
    for (const ZipEntry &entry : archive.entries()) {
        if (entry.name.size() < 4 ||
            entry.name.compare(entry.name.size() - 4, 4, ".sst") != 0) {
            continue;
        }
        std::vector<std::uint8_t> data;
        SstSprite frame;
        if (!archive.read(entry, &data) || !decodeSst(data.data(), data.size(), &frame)) {
            ++failures;
        } else {
            ++frames;
        }
    }
    std::printf("  info %d .sst frames decoded\n", frames);
    check(frames > 500, "the whole bundle decodes");
    check(failures == 0, "no undecodable frames");

    check(!archive.read("this_frame_does_not_exist.sst", &sst_bytes), "missing frame rejected");
}

// The tyF glyph rectangles are normalised UVs into the atlas. Checking that a
// glyph cell actually contains ink proves both the UV convention (v measured
// from the top) and the atlas pairing.
void testFontAtlas(const std::string &assets) {
    std::printf("[font atlas]\n");
    std::vector<std::uint8_t> tyf;
    std::vector<std::uint8_t> png;
    Image atlas;
    const std::string tyf_path = assets + "/graphics/Fonts/Arial Rounded MT Bold.tyF";
    const std::string png_path = assets + "/graphics/Fonts/Arial Rounded MT Bold.png";
    if (!readFile(tyf_path, &tyf) || !readFile(png_path, &png) ||
        !decodePng(png.data(), png.size(), &atlas)) {
        std::printf("  info font assets missing, skipped\n");
        return;
    }
    check(atlas.width == 512 && atlas.height == 512, "font atlas is 512x512");

    SmlDocument doc;
    std::string error;
    check(doc.parse(reinterpret_cast<const char *>(tyf.data()), tyf.size(), &error),
          "font descriptor parses");
    const SmlArray *table = doc.array("CharTable");
    check(table != nullptr && table->records.size() == 256, "CharTable has 256 glyphs");
    if (table == nullptr) return;

    auto ink = [&](int glyph, bool flip_v) {
        const SmlRecord &record = table->records[static_cast<std::size_t>(glyph)];
        const int x0 = static_cast<int>(record.number("u0") * atlas.width);
        const int x1 = static_cast<int>(record.number("u1") * atlas.width) + 1;
        const int y0 = static_cast<int>(record.number("v0") * atlas.height);
        const int y1 = static_cast<int>(record.number("v1") * atlas.height) + 1;
        int count = 0;
        for (int y = y0; y < y1 && y < atlas.height; ++y) {
            for (int x = x0; x < x1 && x < atlas.width; ++x) {
                const int row = flip_v ? atlas.height - 1 - y : y;
                if (atlas.rgba[(static_cast<std::size_t>(row) * atlas.width + x) * 4 + 3] > 16) {
                    ++count;
                }
            }
        }
        return count;
    };

    check(ink('A', false) > 0, "glyph 'A' samples ink at its UV rectangle");
    check(ink('0', false) > 0, "glyph '0' samples ink at its UV rectangle");
    check(ink('0', true) == 0, "glyph UVs are top-down (vertically flipped cell is empty)");
    check(ink(' ', false) == 0, "the space glyph has no ink");
}

}  // namespace

int main(int argc, char **argv) {
    const std::string assets = argc > 1 ? argv[1] : WRESTLEFEST_ASSET_ROOT;
    testPng(assets);
    testSst(assets);
    testFontAtlas(assets);

    if (g_failures == 0) {
        std::printf("\nimage_test: PASS\n");
        return 0;
    }
    std::printf("\nimage_test: %d failure(s)\n", g_failures);
    return 1;
}
