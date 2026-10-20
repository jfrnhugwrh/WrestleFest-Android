#ifndef WRESTLEFEST_GAME_RENDERER_H
#define WRESTLEFEST_GAME_RENDERER_H

// Draws a Match: arena layers -> actors by depth -> front ropes -> HUD.
//
// The simulation knows nothing about this file (GAMEPLAY_IMPLEMENTATION 2.3);
// everything here reads actor state and turns it into sprites. Coordinates are
// the logical 960x640 canvas, so the numbers line up with the SML tables.

#include "../render/gl_sprite.h"
#include "../sim/match.h"

#include <string>
#include <unordered_map>
#include <vector>

class AssetLoader;

namespace wrestlefest {

struct ArenaAssets {
    Texture ground;       // ground.png (crowd + floor), drawn behind the ring
    Texture background;   // arena0.png (ring + ropes + apron)
    Texture ropes_front;  // ropes0_bottom.png, drawn over the wrestlers
    Texture shadow;       // shadow.png
    // Placement of the art in logical space, calibrated against the ring
    // geometry so the mat's corners land on the simulation's trapezoid.
    float art_scale = 0.72f;
    float origin_x = -53.5f;
    float origin_y = 250.4f;
};

class GameRenderer {
public:
    bool initialize(AssetLoader *assets, const GameData *data);
    void shutdown();

    void begin(int viewport_x, int viewport_y, int viewport_w, int viewport_h);
    void end();

    void drawArenaBackground();
    void drawMatch(const Match &match, float alpha);
    void drawActor(const Wrestler &wrestler, float alpha);
    void drawArenaFront();
    void drawHud(const Match &match, int player_index);

    SpriteBatch &batch() { return batch_; }
    BitmapFont &font() { return font_; }

    // Assets the app shell draws itself (virtual pad, plain fills).
    const Texture &white() const { return white_; }
    const Texture &stickBase() const { return virtual_stick_base_; }
    const Texture &stickIcon() const { return virtual_stick_icon_; }
    const Texture &redButton() const { return button_red_; }
    const Texture &blueButton() const { return button_blue_; }

    // Convenience for the menu screens.
    void drawText(const std::string &text, float x, float y, float scale, const Color &color);
    void drawTextCentered(const std::string &text, float center_x, float y, float scale,
                          const Color &color);

    static constexpr int kLogicalWidth = 960;
    static constexpr int kLogicalHeight = 640;

private:
    struct EnergyBarAssets {
        Texture background;
        Texture fill;
        Texture low;  // red fill, shown when the wrestler is nearly out
        float scale = 0.55f;
    };

    struct FrameTexture {
        Texture indices;
        Texture palette;
        bool valid = false;
    };

    bool loadArena(const std::string &folder);
    bool loadUi();
    // Fetches (and caches) one animation frame of a wrestler bundle.
    const FrameTexture *frameFor(const WrestlerProfile &profile, int sst_id);
    void drawEnergyBar(const EnergyBarAssets &assets, const Wrestler &wrestler, float x,
                       float y, bool mirrored);
    void drawTimer(const Match &match);

    SpriteBatch batch_;
    BitmapFont font_;
    const GameData *data_ = nullptr;
    AssetLoader *assets_ = nullptr;
    ArenaAssets arena_;

    EnergyBarAssets energy_single_;  // UI_energy01_* (1 vs 1)
    EnergyBarAssets energy_pair_;    // UI_energy02_* (2 vs 2)
    EnergyBarAssets energy_multi_;   // UI_energy03_* (rumble)
    Texture label_1p_;
    Texture label_2p_;
    Texture ref_count_[10];
    Texture ref_middle_;
    Texture ref_infinite_;
    Texture virtual_stick_base_;
    Texture virtual_stick_icon_;
    Texture button_red_;
    Texture button_blue_;
    Texture white_;

    struct BundleCache {
        std::string folder;
        // The whole wrestler.zip, kept so single frames can be pulled out by
        // name later (AssetLoader only reads whole assets).
        std::vector<std::uint8_t> zip;
        bool loaded = false;
        std::unordered_map<std::string, FrameTexture> textures;
    };
    std::unordered_map<std::string, BundleCache> bundles_;
    std::size_t texture_budget_ = 0;

    float logical_to_scale_ = 0.72f;
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_GAME_RENDERER_H
