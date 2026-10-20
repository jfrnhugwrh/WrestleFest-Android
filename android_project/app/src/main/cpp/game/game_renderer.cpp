#include "game_renderer.h"

#include "../asset_loader.h"
#include "../render/image.h"
#include "../render/zip_reader.h"

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace wrestlefest {
namespace {

constexpr char kLogTag[] = "WrestleFestRender";

// Sprites and arena art share one scale so a wrestler stands the right height
// inside the ring trapezoid (1 unit == 1 pixel of the 1482x679 arena art).
constexpr float kArtScale = 0.72f;

std::string toString(int value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return buffer;
}

}  // namespace

bool GameRenderer::initialize(AssetLoader *assets, const GameData *data) {
    assets_ = assets;
    data_ = data;
    if (!batch_.initialize()) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "sprite batch init failed");
        return false;
    }
    white_ = batch_.uploadWhite();

    // Font: tyF descriptor + its atlas PNG.
    std::vector<std::uint8_t> tyf;
    std::vector<std::uint8_t> atlas_bytes;
    if (assets_ != nullptr &&
        assets_->readAsset("graphics/Fonts/Arial Rounded MT Bold.tyF", &tyf) &&
        assets_->readAsset("graphics/Fonts/Arial Rounded MT Bold.png", &atlas_bytes)) {
        Image atlas;
        if (decodePng(atlas_bytes.data(), atlas_bytes.size(), &atlas)) {
            if (!font_.load(std::string(tyf.begin(), tyf.end()), atlas, &batch_)) {
                __android_log_print(ANDROID_LOG_WARN, kLogTag, "font parse failed");
            }
        }
    }

    loadUi();
    loadArena("ARENA02");
    return true;
}

void GameRenderer::shutdown() {
    if (arena_.ground.valid()) glDeleteTextures(1, &arena_.ground.id);
    if (arena_.background.valid()) glDeleteTextures(1, &arena_.background.id);
    if (arena_.ropes_front.valid()) glDeleteTextures(1, &arena_.ropes_front.id);
    if (arena_.shadow.valid()) glDeleteTextures(1, &arena_.shadow.id);
    EnergyBarAssets *bars[3] = {&energy_single_, &energy_pair_, &energy_multi_};
    for (EnergyBarAssets *bar : bars) {
        if (bar->background.valid()) glDeleteTextures(1, &bar->background.id);
        if (bar->fill.valid()) glDeleteTextures(1, &bar->fill.id);
        if (bar->low.valid()) glDeleteTextures(1, &bar->low.id);
    }
    if (label_1p_.valid()) glDeleteTextures(1, &label_1p_.id);
    if (label_2p_.valid()) glDeleteTextures(1, &label_2p_.id);
    if (ref_middle_.valid()) glDeleteTextures(1, &ref_middle_.id);
    if (ref_infinite_.valid()) glDeleteTextures(1, &ref_infinite_.id);
    for (Texture &digit : ref_count_) {
        if (digit.valid()) glDeleteTextures(1, &digit.id);
    }
    if (virtual_stick_base_.valid()) glDeleteTextures(1, &virtual_stick_base_.id);
    if (virtual_stick_icon_.valid()) glDeleteTextures(1, &virtual_stick_icon_.id);
    if (button_red_.valid()) glDeleteTextures(1, &button_red_.id);
    if (button_blue_.valid()) glDeleteTextures(1, &button_blue_.id);
    for (auto &bundle : bundles_) {
        for (auto &entry : bundle.second.textures) {
            if (entry.second.indices.valid()) glDeleteTextures(1, &entry.second.indices.id);
            if (entry.second.palette.valid()) glDeleteTextures(1, &entry.second.palette.id);
        }
    }
    bundles_.clear();
    batch_.shutdown();
}

bool GameRenderer::loadArena(const std::string &folder) {
    auto load = [&](const char *file, Texture *out) {
        std::vector<std::uint8_t> bytes;
        const std::string path = "graphics/Sprites/Arena/" + folder + "/" + file;
        if (assets_ == nullptr || !assets_->readAsset(path, &bytes)) return false;
        Image image;
        if (!decodePng(bytes.data(), bytes.size(), &image)) return false;
        *out = batch_.uploadRgba(image);
        return out->valid();
    };
    load("ground.png", &arena_.ground);
    load("arena0.png", &arena_.background);
    load("ropes0_bottom.png", &arena_.ropes_front);
    load("shadow.png", &arena_.shadow);

    // The art is placed so the ring mat's corners sit on the simulation's
    // trapezoid (calibrated from arena0.png: mat top y=164 x=384..1100,
    // bottom y=430 x=268..1216).
    arena_.art_scale = kArtScale;
    arena_.origin_x = 480.0f - 742.0f * kArtScale;
    arena_.origin_y = 560.0f - 430.0f * kArtScale;
    return arena_.background.valid();
}

bool GameRenderer::loadUi() {
    auto load = [&](const std::string &file, Texture *out) {
        std::vector<std::uint8_t> bytes;
        if (assets_ == nullptr || !assets_->readAsset(file, &bytes)) return false;
        Image image;
        if (!decodePng(bytes.data(), bytes.size(), &image)) return false;
        *out = batch_.uploadRgba(image);
        return out->valid();
    };
    const std::string bar = "ui/Sprites/UI/energy_bar/";
    // Three bar sizes ship with the game: one wrestler per side (01), tag teams
    // (02) and multi-man matches such as the Rumble (03).
    load(bar + "UI_energy01_background.png", &energy_single_.background);
    load(bar + "UI_energy01_1.png", &energy_single_.fill);
    load(bar + "UI_energy01_2.png", &energy_single_.low);
    load(bar + "UI_energy02_background.png", &energy_pair_.background);
    load(bar + "UI_energy02_1.png", &energy_pair_.fill);
    load(bar + "UI_energy02_2.png", &energy_pair_.low);
    load(bar + "UI_energy03_background.png", &energy_multi_.background);
    load(bar + "UI_energy03_1.png", &energy_multi_.fill);
    load(bar + "UI_energy03_2.png", &energy_multi_.low);
    load(bar + "UI_1P.png", &label_1p_);
    load(bar + "UI_2P.png", &label_2p_);

    for (int i = 0; i < 10; ++i) {
        load("ui/Sprites/UI/count/TIME_" + toString(i) + ".png", &ref_count_[i]);
    }
    load("ui/Sprites/UI/count/TIME_middle_mark.png", &ref_middle_);
    load("ui/Sprites/UI/count/infinite.png", &ref_infinite_);

    load("graphics/Sprites/virtual_pad/joystick_base.png", &virtual_stick_base_);
    load("graphics/Sprites/virtual_pad/joystick_icon.png", &virtual_stick_icon_);
    load("graphics/Sprites/virtual_pad/red_btn_base.png", &button_red_);
    load("graphics/Sprites/virtual_pad/blue_btn_base.png", &button_blue_);
    return true;
}

void GameRenderer::begin(int viewport_x, int viewport_y, int viewport_w, int viewport_h) {
    batch_.begin(viewport_x, viewport_y, viewport_w, viewport_h, kLogicalWidth, kLogicalHeight);
}

void GameRenderer::end() { batch_.end(); }

void GameRenderer::drawArenaBackground() {
    // Crowd sheet first: it is wider than the ring art and its bottom edge sits
    // on the bottom of the logical canvas.
    if (arena_.ground.valid()) {
        const float ground_w = static_cast<float>(arena_.ground.width) * arena_.art_scale;
        const float ground_h = static_cast<float>(arena_.ground.height) * arena_.art_scale;
        batch_.draw(arena_.ground, arena_.origin_x, kLogicalHeight - ground_h, ground_w, ground_h,
                    Color{});
    }
    const float width = static_cast<float>(arena_.background.width) * arena_.art_scale;
    const float height = static_cast<float>(arena_.background.height) * arena_.art_scale;
    batch_.draw(arena_.background, arena_.origin_x, arena_.origin_y, width, height, Color{});
}

void GameRenderer::drawArenaFront() {
    // Front ropes sit on the near edge of the mat.
    const float width = 1180.0f * arena_.art_scale;
    const float height = 210.0f * arena_.art_scale;
    const float x = 480.0f - width * 0.5f;
    const float y = 560.0f - height + 18.0f;
    batch_.draw(arena_.ropes_front, x, y, width, height, Color{});
}

const GameRenderer::FrameTexture *GameRenderer::frameFor(const WrestlerProfile &profile, int sst_id) {
    const SpriteEntry *sprite = profile.spriteFor(sst_id);
    if (sprite == nullptr || sprite->name.empty()) return nullptr;

    BundleCache &cache = bundles_[profile.folder];
    auto cached = cache.textures.find(sprite->name);
    if (cached != cache.textures.end()) return cached->second.valid ? &cached->second : nullptr;

    if (!cache.loaded) {
        // First use: read the whole bundle once and keep it, since the frames
        // live inside the archive.
        cache.loaded = true;
        const std::string path =
            "asset_bundles/Sprites/Character/" + profile.folder + "/wrestler.zip";
        if (assets_ == nullptr || !assets_->readAsset(path, &cache.zip)) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "missing bundle %s", path.c_str());
            return nullptr;
        }
        ZipArchive probe;
        if (!probe.open(cache.zip.data(), cache.zip.size())) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "bad zip %s", path.c_str());
            cache.zip.clear();
            return nullptr;
        }
    }
    if (cache.zip.empty()) return nullptr;

    ZipArchive archive;
    if (!archive.open(cache.zip.data(), cache.zip.size())) return nullptr;
    std::vector<std::uint8_t> sst_bytes;
    if (!archive.read(sprite->name, &sst_bytes)) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "missing frame %s", sprite->name.c_str());
        cache.textures[sprite->name] = FrameTexture{};
        return nullptr;
    }
    SstSprite decoded;
    if (!decodeSst(sst_bytes.data(), sst_bytes.size(), &decoded)) {
        cache.textures[sprite->name] = FrameTexture{};
        return nullptr;
    }
    FrameTexture frame;
    if (decoded.palettized) {
        frame.indices = batch_.uploadIndexMap(decoded);
        frame.palette = batch_.uploadPalette(decoded.palette);
        frame.valid = frame.indices.valid() && frame.palette.valid();
    } else {
        Image image;
        image.width = decoded.width;
        image.height = decoded.height;
        image.rgba = decoded.rgba;
        frame.indices = batch_.uploadRgba(image);
        frame.valid = frame.indices.valid();
    }
    // Keep the texture cache bounded (a match touches a few hundred frames).
    if (cache.textures.size() > 420) {
        for (auto &entry : cache.textures) {
            if (entry.second.indices.valid()) glDeleteTextures(1, &entry.second.indices.id);
            if (entry.second.palette.valid()) glDeleteTextures(1, &entry.second.palette.id);
        }
        cache.textures.clear();
    }
    auto inserted = cache.textures.emplace(sprite->name, frame);
    ++texture_budget_;
    return inserted.first->second.valid ? &inserted.first->second : nullptr;
}

void GameRenderer::drawActor(const Wrestler &wrestler, float alpha) {
    (void)alpha;
    const WrestlerProfile *profile = wrestler.profile();
    if (profile == nullptr || wrestler.hidden()) return;

    const int sst_id = wrestler.spriteId();
    const SpriteEntry *sprite = profile->spriteFor(sst_id);
    const FrameTexture *frame = frameFor(*profile, sst_id);
    if (sprite == nullptr || frame == nullptr) return;

    const float scale = kArtScale;
    const float ground = wrestler.z - wrestler.y;
    float left;
    if (wrestler.facing >= 0.0f) {
        left = wrestler.x - sprite->x_offset * scale;
    } else {
        left = wrestler.x - (static_cast<float>(frame->indices.width) - sprite->x_offset) * scale;
    }
    const float top = ground - sprite->y_offset * scale;
    const float width = static_cast<float>(frame->indices.width) * scale;
    const float height = static_cast<float>(frame->indices.height) * scale;

    // Contact shadow, drawn under the sprite.
    if (arena_.shadow.valid()) {
        const float shadow_w = 96.0f * scale;
        const float shadow_h = 26.0f * scale;
        batch_.draw(arena_.shadow, wrestler.x - shadow_w * 0.5f,
                    ground - shadow_h * 0.62f, shadow_w, shadow_h, Color{1.0f, 1.0f, 1.0f, 0.55f});
    }

    Color tint{1.0f, 1.0f, 1.0f, 1.0f};
    float palette_offset = 0.0f;
    if (wrestler.powered_up) {
        // The original's only power-up cue: a palette blink (PaletteOffset).
        tint = Color{1.0f, 1.0f, 1.0f, 1.0f};
        palette_offset = (std::fmod(wrestler.gameClock(), 0.24f) < 0.12f) ? 8.0f : 0.0f;
    }
    if (frame->palette.valid()) {
        batch_.drawIndexed(frame->indices, frame->palette, left, top, width, height, tint,
                           wrestler.facing < 0.0f, palette_offset);
    } else {
        batch_.draw(frame->indices, left, top, width, height, tint, wrestler.facing < 0.0f);
    }
}

void GameRenderer::drawMatch(const Match &match, float alpha) {
    drawArenaBackground();

    // Sort by depth: far wrestlers first (the original uses draw layers).
    std::vector<const Wrestler *> actors;
    actors.reserve(static_cast<std::size_t>(match.wrestlerCount()));
    for (int i = 0; i < match.wrestlerCount(); ++i) {
        const Wrestler *w = match.wrestler(i);
        if (w != nullptr) actors.push_back(w);
    }
    std::stable_sort(actors.begin(), actors.end(), [](const Wrestler *a, const Wrestler *b) {
        if (a->z != b->z) return a->z < b->z;
        return a->layer < b->layer;
    });
    for (const Wrestler *actor : actors) drawActor(*actor, alpha);

    drawArenaFront();
}

void GameRenderer::drawEnergyBar(const EnergyBarAssets &assets, const Wrestler &wrestler,
                                 float x, float y, bool mirrored) {
    const float scale = assets.scale;
    // Geometry inside UI_energy01_background.png (815x83): a portrait notch at
    // the outer edge, then the tube the 718x19 fill sits in. The 02/03 bars use
    // the same proportions.
    const float bar_w = 815.0f * scale;
    const float bar_h = 83.0f * scale;
    const float inner_x = 76.0f * scale;
    const float inner_y = 32.0f * scale;
    const float inner_w = 718.0f * scale;
    const float inner_h = 19.0f * scale;

    if (assets.background.valid()) {
        batch_.draw(assets.background, x, y, bar_w, bar_h, Color{}, mirrored);
    }

    const float max = wrestler.energy_max > 0.0f ? wrestler.energy_max : 100.0f;
    const float ghost = std::clamp(wrestler.energy_display / max, 0.0f, 1.0f);
    const float value = std::clamp(wrestler.energy / max, 0.0f, 1.0f);
    // Below 30 % the bar switches to its red fill, as the original does.
    const Texture &fill = (value <= 0.3f && assets.low.valid()) ? assets.low : assets.fill;
    if (!fill.valid()) return;

    // `value` is measured from the outer (notch) end, so a mirrored bar fills
    // from the right and samples the texture backwards.
    auto segment = [&](float from, float to, const Color &colour) {
        if (to <= from) return;
        const float width = inner_w * (to - from);
        const float left = mirrored ? x + inner_x + inner_w * (1.0f - to)
                                    : x + inner_x + inner_w * from;
        const float u0 = mirrored ? 1.0f - to : from;
        const float u1 = mirrored ? 1.0f - from : to;
        batch_.drawRegion(fill, left, y + inner_y, width, inner_h, u0, 0.0f, u1, 1.0f, colour,
                          false);
    };

    // Ghost section first: the part the bar has not caught up with yet.
    if (ghost > value) {
        segment(value, ghost, Color{0.18f, 0.02f, 0.02f, 0.9f});
    }
    segment(0.0f, value, Color{});
}

void GameRenderer::drawTimer(const Match &match) {
    const float digit_w = 23.0f;
    const float digit_h = 43.0f;
    const float gap = 1.0f;
    if (match.timeLeft() < 0.0f) {
        if (ref_infinite_.valid()) {
            batch_.draw(ref_infinite_, 480.0f - 42.5f, 8.0f, 85.0f, 42.0f, Color{});
        }
        return;
    }
    const int total = static_cast<int>(match.timeLeft());
    const int minutes = total / 60;
    const int seconds = total % 60;
    const int digits[4] = {minutes / 10, minutes % 10, seconds / 10, seconds % 10};
    const float total_w = digit_w * 4 + gap * 3 + 12.0f;
    float x = 480.0f - total_w * 0.5f;
    for (int i = 0; i < 4; ++i) {
        if (i == 2) {
            if (ref_middle_.valid()) {
                batch_.draw(ref_middle_, x, 8.0f + 8.0f, 8.0f, 27.0f, Color{});
            }
            x += 12.0f;
        }
        if (ref_count_[digits[i]].valid()) {
            batch_.draw(ref_count_[digits[i]], x, 8.0f, digit_w, digit_h, Color{});
        }
        x += digit_w + gap;
    }
}

void GameRenderer::drawHud(const Match &match, int player_index) {
    const int count = match.wrestlerCount();
    const EnergyBarAssets &assets = count <= 2 ? energy_single_
                                               : (count <= 4 ? energy_pair_ : energy_multi_);
    (void)assets;

    // 1 vs 1: player on the left, opponent mirrored on the right.
    for (int i = 0; i < count && i < 2; ++i) {
        const Wrestler *w = match.wrestler(i);
        if (w == nullptr) continue;
        const bool left_side = (i == player_index);
        const float scale = energy_single_.scale;
        const float bar_w = 815.0f * scale;
        const float x = left_side ? 6.0f : 960.0f - 6.0f - bar_w;
        drawEnergyBar(energy_single_, *w, x, 4.0f, !left_side);

        // Player label sits in the portrait notch, which is at the outer edge.
        const Texture &label = left_side ? label_1p_ : label_2p_;
        if (label.valid()) {
            const float label_scale = 1.0f;
            const float label_x = left_side ? x + 18.0f * scale
                                            : x + bar_w - 18.0f * scale - 31.0f * label_scale;
            batch_.draw(label, label_x, 4.0f + 33.0f * scale, 29.0f * label_scale,
                        16.0f * label_scale, Color{});
        }

        // Name under the bar, aligned to the outer edge.
        if (font_.valid() && w->profile() != nullptr) {
            const std::string &name = w->profile()->name_line1;
            const float text_scale = 0.62f;
            const float width = font_.measure(name, text_scale);
            const float text_x = left_side ? x + 8.0f : x + bar_w - width - 8.0f;
            font_.draw(&batch_, text_x, 4.0f + 83.0f * scale + 2.0f, text_scale, name,
                       Color{1.0f, 0.93f, 0.78f, 1.0f});
        }
    }

    drawTimer(match);

    // Referee count, big and centred, while a pin is being counted.
    if (match.referee().counting()) {
        const int value = match.referee().count();
        if (value >= 0 && value <= 9 && ref_count_[value].valid()) {
            const float scale = 2.4f;
            batch_.draw(ref_count_[value], 480.0f - 23.0f * scale * 0.5f, 190.0f,
                        23.0f * scale, 43.0f * scale, Color{});
        }
    }

    // Mash prompt: the pinning wrestler has to press, the pinned one to escape.
    for (int i = 0; i < count; ++i) {
        const Wrestler *w = match.wrestler(i);
        if (w == nullptr) continue;
        if (w->subState() == state::BE_PINNED) {
            drawTextCentered("MASH!", 480.0f, 300.0f, 0.9f, Color{1.0f, 0.35f, 0.3f, 1.0f});
        }
    }
}

void GameRenderer::drawText(const std::string &text, float x, float y, float scale,
                            const Color &color) {
    font_.draw(&batch_, x, y, scale, text, color);
}

void GameRenderer::drawTextCentered(const std::string &text, float center_x, float y, float scale,
                                    const Color &color) {
    const float width = font_.measure(text, scale);
    font_.draw(&batch_, center_x - width * 0.5f, y, scale, text, color);
}

}  // namespace wrestlefest
