#include "game_renderer.h"

#include "../asset_loader.h"
#include "../render/image.h"
#include "../data/sml_parser.h"
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
    loadReferee();
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
    if (button_red_icon_.valid()) glDeleteTextures(1, &button_red_icon_.id);
    if (button_blue_icon_.valid()) glDeleteTextures(1, &button_blue_icon_.id);
    if (stick_arrows_.valid()) glDeleteTextures(1, &stick_arrows_.id);
    for (Texture *texture : {&ui_background_, &title_logo_, &tap_to_start_, &superstar_frame_,
                             &superstar_select_, &arrow_previous_, &arrow_next_, &arrow_point_,
                             &button_strip_, &pause_button_, &pause_button_pressed_,
                             &pause_background_, &pause_menu_button_}) {
        if (texture->valid()) glDeleteTextures(1, &texture->id);
    }
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
    load("graphics/Sprites/virtual_pad/red_btn_icon.png", &button_red_icon_);
    load("graphics/Sprites/virtual_pad/blue_btn_icon.png", &button_blue_icon_);
    // Directional hints around the stick come straight from the manual sheet.
    load("ui/Sprites/UI/hint/Arrow.png", &stick_arrows_);

    // Front-end.
    load("ui/Sprites/UI/background.png", &ui_background_);
    load("ui/Sprites/UI/title_menu/title.png", &title_logo_);
    load("ui/Sprites/UI/title_menu/taptostart_en.png", &tap_to_start_);
    load("ui/Sprites/UI/selector/superstar_frame.png", &superstar_frame_);
    load("ui/Sprites/UI/selector/superstar_select.png", &superstar_select_);
    load("ui/Sprites/UI/selector/previous.png", &arrow_previous_);
    load("ui/Sprites/UI/selector/next.png", &arrow_next_);
    load("ui/Sprites/UI/selector/point.png", &arrow_point_);
    load("ui/Sprites/UI/main_menu/main_button_0.png", &button_strip_);
    load("graphics/Sprites/virtual_pad/pause_btn.png", &pause_button_);
    load("graphics/Sprites/virtual_pad/pause_btn_clicked.png", &pause_button_pressed_);
    load("ui/Sprites/UI/pause/background.png", &pause_background_);
    load("ui/Sprites/UI/report/frame.png", &report_frame_);
    load("ui/Sprites/UI/report/background.png", &report_background_);
    load("ui/Sprites/UI/pause/button.png", &pause_menu_button_);
    return true;
}

bool GameRenderer::loadReferee() {
    // refData.sml lists the 14 referee frames in atlas order with the anchor
    // each sprite is drawn at (xoffset is the left edge, yoffset the height up
    // from the feet - the same convention as wrestlerData.sml).
    std::vector<std::uint8_t> bytes;
    const std::string table = "graphics/Sprites/Character/Ref/refData.sml";
    if (assets_ == nullptr || !assets_->readAsset(table, &bytes)) return false;
    SmlDocument doc;
    std::string error;
    if (!doc.parse(reinterpret_cast<const char *>(bytes.data()), bytes.size(), &error)) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "refData.sml: %s", error.c_str());
        return false;
    }
    for (const SmlRecord &record : doc.records) {
        RefFrame frame;
        const std::string name = record.string("name");
        if (name.empty()) continue;
        std::vector<std::uint8_t> png;
        if (!assets_->readAsset("graphics/Sprites/Character/Ref/" + name, &png)) {
            ref_frames_.push_back(frame);
            continue;
        }
        Image image;
        if (decodePng(png.data(), png.size(), &image)) {
            frame.texture = batch_.uploadRgba(image);
            frame.x_offset = static_cast<float>(record.number("xoffset"));
            frame.y_offset = static_cast<float>(record.number("yoffset"));
        }
        ref_frames_.push_back(frame);
    }
    return !ref_frames_.empty();
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

bool GameRenderer::openBundle(BundleCache *cache, const std::string &folder) {
    if (cache->loaded) return !cache->failed;
    cache->loaded = true;
    cache->folder = folder;
    const std::string path = "asset_bundles/Sprites/Character/" + folder + "/wrestler.zip";
    if (assets_ == nullptr) {
        cache->failed = true;
        return false;
    }
    const std::uint64_t size = assets_->assetSize(path);
    std::vector<std::uint8_t> ignored;
    if (size == 0 || !assets_->openAssetRange(path, 1, &ignored, &cache->reader)) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "missing bundle %s", path.c_str());
        cache->failed = true;
        return false;
    }
    // The archive is never held in memory: the central directory is read from
    // the end and each frame is fetched with a byte-range read.
    BundleCache *target = cache;
    auto reader = [target](std::size_t offset, std::size_t length, std::uint8_t *destination) {
        return target->reader && target->reader(offset, length, destination);
    };
    if (!cache->archive.openStreaming(static_cast<std::size_t>(size), reader)) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "bad zip %s", path.c_str());
        cache->failed = true;
        return false;
    }
    return true;
}

const GameRenderer::FrameTexture *GameRenderer::frameFor(const WrestlerProfile &profile, int sst_id) {
    const SpriteEntry *sprite = profile.spriteFor(sst_id);
    if (sprite == nullptr || sprite->name.empty()) return nullptr;

    BundleCache &cache = bundles_[profile.folder];
    if (!openBundle(&cache, profile.folder)) return nullptr;

    auto cached = cache.textures.find(sprite->name);
    if (cached != cache.textures.end()) return cached->second.valid ? &cached->second : nullptr;

    const ZipEntry *entry = cache.archive.find(sprite->name);
    std::vector<std::uint8_t> sst_bytes;
    if (entry == nullptr || !cache.archive.readStreaming(*entry, &sst_bytes)) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "missing frame %s in %s",
                            sprite->name.c_str(), profile.folder.c_str());
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
        for (auto &entry_it : cache.textures) {
            if (entry_it.second.indices.valid()) glDeleteTextures(1, &entry_it.second.indices.id);
            if (entry_it.second.palette.valid()) glDeleteTextures(1, &entry_it.second.palette.id);
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

void GameRenderer::drawActorPortrait(const WrestlerProfile &profile, int sst_id, float center_x,
                                     float feet_y, float alpha, float scale) {
    const SpriteEntry *sprite = profile.spriteFor(sst_id);
    const FrameTexture *frame = frameFor(profile, sst_id);
    if (sprite == nullptr || frame == nullptr) return;
    if (scale <= 0.0f) scale = kArtScale;
    const float width = frame->indices.width * scale;
    const float height = frame->indices.height * scale;
    const float left = center_x - width * 0.5f;
    const float top = feet_y - sprite->y_offset * scale;
    const Color tint{1.0f, 1.0f, 1.0f, alpha};
    if (frame->palette.valid()) {
        batch_.drawIndexed(frame->indices, frame->palette, left, top, width, height, tint);
    } else {
        batch_.draw(frame->indices, left, top, width, height, tint);
    }
}

void GameRenderer::drawReferee(const Match &match) {
    if (ref_frames_.empty()) return;
    const Referee &ref = match.referee();

    // Pick the pose: the three-finger count while pinning, a warning wave when
    // he is chasing a wrestler out of the ring, otherwise the walk cycle.
    // Pose selection follows the referee's sub-state: a three-finger count on a
    // pin, "ask give up" on a submission, a warning wave when he has to get a
    // wrestler back in the ring, and the mike_* walk cycle otherwise.
    int frame = ref_walk_frames_[static_cast<int>(match.clock() / 0.3f) & 3];
    switch (ref.sub()) {
        case RefSub::PinCount:
            frame = ref_count_frames_[std::clamp(ref.count(), 1, 3) - 1];
            break;
        case RefSub::GiveUp:
            frame = ref_ask_frames_[static_cast<int>(match.clock() / 0.5f) & 1];
            break;
        case RefSub::TagBreak:
            frame = ref_warn_frames_[static_cast<int>(match.clock() / 0.4f) & 1];
            break;
        case RefSub::MoveToPosition:
            frame = ref_walk_frames_[static_cast<int>(match.clock() / 0.2f) & 3];
            break;
        case RefSub::None:
        default:
            if (ref.master() == RefMaster::Outside || match.outsideCount() > 0) {
                frame = ref_warn_frames_[static_cast<int>(match.clock() / 0.5f) & 1];
            } else if (ref.master() == RefMaster::Watch) {
                frame = ref_judge_frames_[static_cast<int>(match.clock() / 0.6f) % 3];
            }
            break;
    }
    if (frame < 0 || frame >= static_cast<int>(ref_frames_.size())) return;
    const RefFrame &sprite = ref_frames_[static_cast<std::size_t>(frame)];
    if (!sprite.texture.valid()) return;
    const float scale = kArtScale;
    const float width = sprite.texture.width * scale;
    const float height = sprite.texture.height * scale;
    const float left = ref.facing >= 0.0f ? ref.x - sprite.x_offset * scale
                                          : ref.x - (sprite.texture.width - sprite.x_offset) * scale;
    const float top = (ref.z - ref.y) - sprite.y_offset * scale;
    if (arena_.shadow.valid()) {
        const float shadow_w = 96.0f * scale;
        const float shadow_h = 26.0f * scale;
        batch_.draw(arena_.shadow, ref.x - shadow_w * 0.5f, (ref.z - ref.y) - shadow_h * 0.62f,
                    shadow_w, shadow_h, Color{1.0f, 1.0f, 1.0f, 0.5f});
    }
    batch_.draw(sprite.texture, left, top, width, height, Color{}, ref.facing < 0.0f);
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
    // The referee sorts with the actors by depth.
    if (match.referee().z < 480.0f) {
        drawReferee(match);
        for (const Wrestler *actor : actors) drawActor(*actor, alpha);
    } else {
        for (const Wrestler *actor : actors) drawActor(*actor, alpha);
        drawReferee(match);
    }

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

    // Royal Rumble: how many are left and when the next man walks in.
    if (match.isRumble()) {
        int left = 0;
        for (int i = 0; i < count; ++i) {
            const Wrestler *w = match.wrestler(i);
            if (w != nullptr && !w->isDead()) ++left;
        }
        left += match.waitingEntrants();
        char line[64];
        std::snprintf(line, sizeof(line), "ROYAL RUMBLE   %d LEFT", left);
        drawTextCentered(line, 480.0f, 128.0f, 0.7f, Color{1.0f, 0.85f, 0.3f, 1.0f});
        if (match.waitingEntrants() > 0) {
            std::snprintf(line, sizeof(line), "NEXT ENTRANT IN %d",
                          static_cast<int>(match.nextEntryIn() + 0.999f));
            drawTextCentered(line, 480.0f, 158.0f, 0.5f, Color{0.85f, 0.9f, 1.0f, 1.0f});
        }
    }

    // 2 VS 2: a green TAG! prompt once the corner is within reach.
    if (match.tagEnabled()) {
        for (int i = 0; i < count; ++i) {
            const Wrestler *w = match.wrestler(i);
            if (w == nullptr || !w->active || w->isDead() || w->isBusy()) continue;
            const int partner = match.partnerIndex(i);
            if (partner < 0 || !match.isOnApron(partner)) continue;
            const Wrestler *mate = match.wrestler(partner);
            if (mate == nullptr || mate->isDead()) continue;
            if (std::fabs(w->x - mate->x) < tuning::kTagReach &&
                std::fabs(w->z - mate->z) < tuning::kTagReach) {
                drawTextCentered("TAG!", 480.0f, 300.0f, 0.95f, Color{0.35f, 1.0f, 0.45f, 1.0f});
            }
        }
    }

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
