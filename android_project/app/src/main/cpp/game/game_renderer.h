#ifndef WRESTLEFEST_GAME_RENDERER_H
#define WRESTLEFEST_GAME_RENDERER_H

// Draws a Match: arena layers -> actors by depth -> front ropes -> HUD.
//
// The simulation knows nothing about this file (GAMEPLAY_IMPLEMENTATION 2.3);
// everything here reads actor state and turns it into sprites. Coordinates are
// the logical 960x640 canvas, so the numbers line up with the SML tables.

#include "../asset_loader.h"  // RangeReader for streaming the bundle zips
#include "../render/gl_sprite.h"
#include "../render/zip_reader.h"
#include "../sim/match.h"

#include <string>
#include <unordered_map>
#include <vector>

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

// The arena's entrance stage (CGameEntrance::onCreate).
//
// graphics/Sprites/Arena/entrance/<FolderName>/<SceneFileName> is the stage
// itself; AttachObjFileName_N are the extra pieces pinned to it (ramp rails,
// light rigs, pyro columns). Both are authored in the same 1482x711 space as
// the ring art, so they share the ring's kArtScale. PositionAndGap places the
// wrestlers: x (negative = centre of the art), z (negative = art height - 200)
// and the gap a tag partner stands apart by.
struct EntranceStage {
    struct Piece {
        Texture texture;
        float x = 0.0f;  // art-space top-left
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    Texture background;
    std::vector<Piece> pieces;
    float art_scale = 0.72f;
    float origin_x = -53.5f;  // art-space 0 in logical canvas coordinates
    float origin_y = 0.0f;
    float art_width = 1482.0f;
    float art_height = 711.0f;
    // Where the wrestlers walk out to, in art space.
    float start_x = 741.0f;
    float start_z = 511.0f;
    float gap = 120.0f;      // already halved, as SetupWrestler uses it
    float max_show_time = 8.0f;
    float obj_scale = 1.0f;    // GameObjScale: how large the wrestlers are drawn
    float actor_scale = 0.72f;  // kArtScale * obj_scale, i.e. the draw scale
    int crowd_id = 0;
    bool valid = false;

    float toScreenX(float art_x) const { return origin_x + art_x * art_scale; }
    float toScreenY(float art_y) const { return origin_y + art_y * art_scale; }
};

class GameRenderer {
public:
    bool initialize(AssetLoader *assets, const GameData *data);
    void shutdown();

    void begin(int viewport_x, int viewport_y, int viewport_w, int viewport_h);
    void end();

    void drawArenaBackground();
    // Officials in the ring: frames come straight from refData.sml (0..13) and
    // announcerData.sml (0..12), selected deterministically by the simulation.
    void drawReferee(const Match &match);
    void drawAnnouncer(const Match &match);
    // Draws one animation frame of a wrestler at an arbitrary spot - used by the
    // select screen for the idle portrait inside the frame.
    void drawActorPortrait(const WrestlerProfile &profile, int sst_id, float center_x,
                           float feet_y, float alpha, float scale = 0.0f);
    // The shared body of drawActorPortrait / drawEntranceActor: one frame,
    // anchored at the sprite's feet, optionally mirrored.
    void drawActorFrame(const WrestlerProfile &profile, int sst_id, float center_x, float feet_y,
                        float alpha, float scale, bool mirrored);
    void drawMatch(const Match &match, float alpha);
    void drawActor(const Wrestler &wrestler, float alpha);
    void drawArenaFront();
    void drawHud(const Match &match, int player_index);

    // --- entrance cutscene (CGameEntrance::onDraw) ---------------------------
    // Loads the selected arena's entrance stage (AllEntranceSceneTable), and
    // draws it with the wrestlers walking out onto it. The stage art is 1482x711
    // like the ring art, so it is scaled and placed the same way; the floor
    // under it is the arena's own crowd sheet.
    const EntranceStage &entranceFor(int entrance_scene_id);
    void drawEntranceBackground(const EntranceStage &stage);
    // CCrowd::Draw(CrowdId): the crowd stands in front of the stage art, in the
    // same art space, animated at eight frames a second.
    void drawCrowd(const EntranceStage &stage, float dt);
    // One wrestler on the stage. `feet_x/feet_y` are logical canvas coordinates.
    void drawEntranceActor(const WrestlerProfile &profile, int sst_id, float feet_x, float feet_y,
                           float facing, float alpha = 1.0f, float scale = 0.0f);
    // The name plate CGameEntrance prints along the bottom of the stage shot:
    // the man's name in yellow at viewportHeight - 100 (his partner's 50 px
    // above it), a black rule and a red rule two pixels under it at
    // viewportHeight - 50, and the WWE mark at the end of the rule.
    void drawEntrancePlate(const std::string &name, const std::string &partner);

    SpriteBatch &batch() { return batch_; }
    // Body face (Arial Rounded MT Bold): HUD, result card, prompts.
    BitmapFont &font() { return font_; }
    // Menu face (Century Gothic): every front-end caption.
    BitmapFont &menuFont() { return menu_font_; }

    // Assets the app shell draws itself (virtual pad, plain fills).
    const Texture &white() const { return white_; }
    // Front-end art (title / select / pause).
    const Texture &uiBackground() const { return ui_background_; }
    const Texture &uiBackground2() const { return ui_background_2_; }
    const Texture &titleLogo() const { return title_logo_; }
    const Texture &tapToStart() const { return tap_to_start_; }
    const Texture &superstarFrame() const { return superstar_frame_; }
    const Texture &superstarSelect() const { return superstar_select_; }
    const Texture &arenaFrame() const { return arena_frame_; }
    const Texture &arenaSelect() const { return arena_select_; }
    const Texture &mainButton(int index) const {
        return index >= 0 && index < 4 ? main_button_[index] : white_;
    }
    const Texture &modeButton(int index) const {
        return index >= 0 && index < 6 ? mode_button_[index] : white_;
    }
    const Texture &wweLogo() const { return wwe_logo_; }
    const Texture &backButton() const { return back_button_; }
    const Texture &arrowPrevious() const { return arrow_previous_; }
    const Texture &arrowNext() const { return arrow_next_; }
    const Texture &arrowPoint() const { return arrow_point_; }
    const Texture &arrowPointSelect() const { return arrow_point_select_; }
    const Texture &buttonStrip() const { return button_strip_; }
    const Texture &pauseButton() const { return pause_button_; }
    const Texture &reportFrame() const { return report_frame_; }
    const Texture &reportBackground() const { return report_background_; }
    const Texture &pauseMenuBackground() const { return pause_background_; }
    const Texture &pauseMenuButton() const { return pause_menu_button_; }

    // Front-end portraits and arena thumbnails are read on demand (a page of
    // eight is at most a megabyte) and then kept for the life of the app.
    const Texture &avatarFor(const std::string &wrestler_folder);
    const Texture &arenaSceneFor(const std::string &arena_folder);
    // The Road to WrestleMania stage cards, keyed by art name.
    const Texture &roadStageArt(const char *name);

    const Texture &stickBase() const { return virtual_stick_base_; }
    const Texture &stickIcon() const { return virtual_stick_icon_; }
    const Texture &redButton() const { return button_red_; }
    const Texture &blueButton() const { return button_blue_; }
    const Texture &redButtonIcon() const { return button_red_icon_; }
    const Texture &blueButtonIcon() const { return button_blue_icon_; }

    // Convenience for the menu screens.
    void drawText(const std::string &text, float x, float y, float scale, const Color &color);
    void drawTextCentered(const std::string &text, float center_x, float y, float scale,
                          const Color &color);
    // Menu captions: the Century Gothic face, positioned on the capital ink so
    // a row of text can be centred inside a button strip.
    void drawMenuTextCentered(const std::string &text, float center_x, float ink_top, float size,
                              const Color &color, float tracking, float shadow_offset);
    void drawMenuText(const std::string &text, float x, float ink_top, float size,
                      const Color &color, float tracking, float shadow_offset);
    float measureMenuText(const std::string &text, float size, float tracking) const;
    // Logo art on the loading screen is tilted; the batch has no rotation, so
    // the quad corners are rotated here.
    void drawRotated(const Texture &texture, float center_x, float center_y, float w, float h,
                     float degrees, const Color &color);
    float measureText(const std::string &text, float scale) const { return font_.measure(text, scale); }

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

public:
    // Swaps the arena art (AllArenaTable FolderName). Keeps the default
    // ARENA02 art when the folder cannot be loaded.
    bool setArena(const std::string &folder);

private:
    bool loadUi();
    bool loadReferee();
    bool loadAnnouncer();
    // Fetches (and caches) one animation frame of a wrestler bundle.
    const FrameTexture *frameFor(const WrestlerProfile &profile, int sst_id);
    // Same, for a crowd "person": asset_bundles/Sprites/Arena/crowd/pNN.zip.
    const FrameTexture *crowdFrame(int person, int dir, int frame);
    bool openCrowdBundle(int person);
    void tickCrowd(int crowd_id, float dt);
    void drawEnergyBar(const EnergyBarAssets &assets, const Wrestler &wrestler, float x,
                       float y, bool mirrored);
    void drawTimer(const Match &match);

    SpriteBatch batch_;
    BitmapFont font_;
    BitmapFont menu_font_;
    const GameData *data_ = nullptr;
    AssetLoader *assets_ = nullptr;
    ArenaAssets arena_;

    EnergyBarAssets energy_single_;  // UI_energy01_* (1 vs 1)
    EnergyBarAssets energy_pair_;    // UI_energy02_* (2 vs 2)
    EnergyBarAssets energy_multi_;   // UI_energy03_* (rumble)
    Texture ui_background_;
    Texture ui_background_2_;
    Texture title_logo_;
    Texture tap_to_start_;
    Texture superstar_frame_;
    Texture superstar_select_;
    Texture arena_frame_;
    Texture arena_select_;
    Texture main_button_[4];
    Texture mode_button_[6];
    Texture wwe_logo_;
    Texture back_button_;
    Texture arrow_previous_;
    Texture arrow_next_;
    Texture arrow_point_;
    Texture arrow_point_select_;
    Texture button_strip_;
    Texture pause_button_;
    Texture pause_button_pressed_;
    Texture pause_background_;
    Texture pause_menu_button_;
    Texture report_frame_;
    Texture report_background_;
    Texture label_1p_;
    Texture label_2p_;
    Texture ref_count_[10];
    Texture ref_middle_;
    Texture ref_infinite_;
    // Referee and ring announcer frames (PNGs, indexed in exact .sml order).
    struct RefFrame {
        Texture texture;
        float x_offset = 0.0f;
        float y_offset = 0.0f;
    };
    std::vector<RefFrame> ref_frames_;        // graphics/Sprites/Character/Ref/refData.sml (0..13)
    std::vector<RefFrame> announcer_frames_;  // graphics/Sprites/Character/Announcer/Robert/announcerData.sml (0..12)

    Texture virtual_stick_base_;
    Texture virtual_stick_icon_;
    Texture button_red_;
    Texture button_blue_;
    Texture button_red_icon_;    // icon drawn on top of the red button
    Texture button_blue_icon_;
    Texture stick_arrows_;
    Texture white_;

    // A wrestler bundle is ~5 MB of .sst frames, so it is opened once and then
    // read on demand: the central directory is parsed from the first 64 KB and
    // individual frames are fetched with a byte-range read instead of keeping
    // the whole archive (and every decoded frame) resident.
    struct BundleCache {
        std::string folder;
        ZipArchive archive;
        AssetLoader::RangeReader reader;  // byte-range access to the bundle file
        bool loaded = false;
        bool failed = false;
        std::unordered_map<std::string, FrameTexture> textures;
    };
    bool openBundle(BundleCache *cache, const std::string &folder);
    std::unordered_map<std::string, BundleCache> bundles_;
    // One cache per crowd person (15 small palettised archives).
    struct CrowdCache {
        ZipArchive archive;
        AssetLoader::RangeReader reader;
        bool loaded = false;
        bool failed = false;
        std::unordered_map<std::string, FrameTexture> textures;
    };
    std::unordered_map<int, CrowdCache> crowd_bundles_;
    float crowd_clock_ = 0.0f;
    // Per-crowd-id animation frame for each instance (the tables are const).
    std::unordered_map<int, std::vector<int>> crowd_frames_;
    std::size_t texture_budget_ = 0;
    // Menu art loaded on demand: wrestler avatars and arena thumbnails.
    std::unordered_map<std::string, Texture> avatars_;
    std::unordered_map<std::string, Texture> arena_scenes_;
    std::unordered_map<std::string, Texture> road_stage_art_;
    // Entrance stages are loaded on demand and kept for the life of the app.
    std::unordered_map<int, EntranceStage> entrances_;
    EntranceStage entrance_fallback_;

    float logical_to_scale_ = 0.72f;
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_GAME_RENDERER_H
