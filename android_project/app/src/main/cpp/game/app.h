#ifndef WRESTLEFEST_GAME_APP_H
#define WRESTLEFEST_GAME_APP_H

// The application shell: asset source, match flow, touch input and the fixed
// 60 Hz simulation step (GAMEPLAY_IMPLEMENTATION 2.5).
//
// Flow mirrors the original's front end (TitleMenu.cpp / TitleMenuMain.cpp):
//
//   Loading -> Title -> Main menu -> Game modes -> Superstar select
//           -> Arena select -> Match -> Result
//
// with the options list hanging off the main menu and the pause menu over the
// match. Every screen is drawn by MenuScreens; this class owns the state and
// the transitions.

#include "../asset_loader.h"
#include "../data/game_data.h"
#include "../render/gl_sprite.h"
#include "../sim/ai.h"
#include "../sim/match.h"
#include "game_renderer.h"
#include "menu_screens.h"
#include "road.h"

#include <memory>
#include <string>
#include <vector>

class AudioPlayer;  // OpenSL ES mixer (global namespace, see audio_player.h)

namespace wrestlefest {

// AssetSource over the APK/OBB loader so GameData can read the .sml tables.
class LoaderAssetSource : public AssetSource {
public:
    explicit LoaderAssetSource(AssetLoader *assets) : assets_(assets) {}
    bool read(const std::string &path, std::vector<std::uint8_t> *bytes) override;
    bool exists(const std::string &path) override;

private:
    AssetLoader *assets_ = nullptr;
};

enum class Scene {
    Loading,
    Title,
    MainMenu,
    Modes,
    Select,
    Arena,
    Road,
    Options,
    Match,
    Result,
};

class GameApp {
public:
    bool initialize(AssetLoader *assets, AudioPlayer *audio);
    void shutdown();

    // Called once per frame with the elapsed time; runs the fixed-step sim.
    void update(double frame_seconds);
    void render(int viewport_x, int viewport_y, int viewport_w, int viewport_h);

    // Touch input in logical (960x640) coordinates.
    void touchDown(int pointer_id, float x, float y);
    void touchMove(int pointer_id, float x, float y);
    void touchUp(int pointer_id);

    bool ready() const { return ready_; }
    const std::string &error() const { return error_; }

private:
    void startMatch(const WrestlerProfile *player, const WrestlerProfile *opponent);
    void startRumble(const WrestlerProfile *player, const WrestlerProfile *opponent);
    void startTagMatch(const WrestlerProfile *player, const WrestlerProfile *opponent);
    // Shared tail of both setups: entrance staging, timers, scene change.
    void beginMatchFlow(const WrestlerProfile *player, const WrestlerProfile *opponent);
    // Applies the selected arena art (falls back to the default arena).
    void applyArena();
    void stepSimulation(float dt);
    void updateIntro(float dt);
    const EntranceStage &entranceStage();
    // CGameEntrance: build the running order, play a show, then hand over to the
    // ring announcer and the bell.
    void setupEntranceShows();
    void beginEntranceShow();
    void beginAnnouncePlayer();
    void beginAnnounceOpponent();
    void beginFight();
    void advanceIntro();
    // Plays a superstar's ring-announcer call and returns its length in seconds.
    float announce(const WrestlerProfile *profile);
    void drawEntrance();
    void drawEntranceWrestler(int index, float feet_x, float feet_y, float scale);
    void drawIntroBanner();
    void drawTouchControls();
    void drawPauseOverlay();
    void drawResult();
    void drawOptions();

    // Front end.
    void enterScene(Scene scene);
    void updateMenus(float dt);
    void handleMenuTap(float x, float y);
    void handleMainMenuTap(float x, float y);
    void handleModesTap(float x, float y);
    void handleOptionsTap(float x, float y);
    void handleSuperstarTap(float x, float y);
    void handleArenaTap(float x, float y);
    void handleRoadTap(float x, float y);
    void startSelectedMatch();
    // Road to WrestleMania: plays the card under the ladder's cursor, and
    // comes back to the ladder when it is over.
    void startRoadMatch();
    void returnFromRoadMatch();
    void chooseOpponent();
    void showNotice(const std::string &text);
    int pageCount(int item_count, int per_page) const;
    int pageOfSelection(int selection, int per_page) const;
    // Page the selector grids should open on.
    static int clampPage(int page, int page_count);
    const WrestlerProfile *playerProfile() const;
    const WrestlerProfile *opponentProfile() const;

    void playCue(const char *asset);
    void playRandomCue(const char *const *paths, int count, const char *tag);

    AssetLoader *assets_ = nullptr;
    AudioPlayer *audio_ = nullptr;
    LoaderAssetSource source_{nullptr};
    GameData data_;
    GameRenderer renderer_;
    MenuScreens menu_;
    Match match_;
    std::unique_ptr<AiEntity> dummy_ai_;
    std::vector<InputFrame> inputs_;

    // Pre-match staging (GAMEPLAY_IMPLEMENTATION 4.3): entrance -> announcer
    // for each wrestler -> bell / "FIGHT".
    enum class IntroStage {
        EntrancePlayer,    // Team 1 walks out on the arena's entrance stage
        EntranceOpponent,  // Team 2 does the same
        AnnouncePlayer,    // Ring announcer: "From Death Valley, weighing 309lb..."
        AnnounceOpponent,  // Ring announcer again for the other side
        Fight,             // "Fight !" + bell
        Done,              // The match is live
    };
    IntroStage intro_stage_ = IntroStage::EntrancePlayer;
    float intro_timer_ = 0.0f;
    float intro_stage_length_ = 0.0f;

    // Entrance running order (CGameEntrance): one show per side, or a single
    // show for the player alone in a Rumble. A tag show brings both partners.
    struct EntranceShow {
        int first = -1;   // the man himself
        int second = -1;  // his tag partner, when the rule is 2 VS 2
        int team = 0;
    };
    std::vector<EntranceShow> entrance_shows_;
    std::size_t entrance_show_ = 0;
    bool intro_skip_ = false;  // the tap that cuts the current stage short

    Scene scene_ = Scene::Loading;
    bool ready_ = false;    // renderer up: the app can draw
    bool data_ok_ = false;  // the SML tables loaded: the front end can advance
    std::string error_;
    // The loading card stays up for a beat so the title does not snap in.
    float loading_timer_ = 0.0f;

    // Front-end selections, all indices into the tables.
    int main_entry_ = 0;    // MainScene cursor
    int mode_entry_ = 0;    // ModeScene cursor
    int superstar_page_ = 0;
    int arena_page_ = 0;
    // Card that was just tapped; it stays highlighted while the next screen
    // slides in, like the original's select-then-confirm.
    int superstar_pick_ = -1;
    int arena_pick_ = -1;
    float confirm_timer_ = 0.0f;
    bool confirm_action_ = false;
    std::string notice_;
    float notice_timer_ = 0.0f;
    bool sound_on_ = true;

    int selected_player_ = 0;
    int selected_opponent_ = 1;

    double accumulator_ = 0.0;
    float clock_ = 0.0f;
    float finish_hold_ = 0.0f;

    // Touch state: a floating stick on the left half and two buttons on the right.
    struct Pointer {
        int id = -1;
        float start_x = 0.0f;
        float start_y = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        bool active = false;
    };
    Pointer stick_;
    Pointer button_red_;
    Pointer button_blue_;
    float stick_dir_x_ = 0.0f;
    float stick_dir_y_ = 0.0f;
    // Hysteresis-latched stick axes (-1/0/1 per axis) — see update().
    int stick_axis_x_ = 0;
    int stick_axis_z_ = 0;
    bool red_down_ = false;
    bool blue_down_ = false;
    bool red_edge_ = false;
    bool blue_edge_ = false;
    bool paused_ = false;
    // The last entrance stage this match asked the renderer for (cached so the
    // draw path does not look it up twice a frame).
    const EntranceStage *entrance_stage_ = nullptr;
    // The InGameTable rule id of the selected mode (menu::kRule*), i.e. the
    // same value Match::configure takes. Keep it that way: a separate
    // "menu mode index" here is what left Royal Rumble and Tag Team swapped.
    int mode_ = menu::kRuleExhibition;
    // Index into AllArenaTable; the art folder is applied when a match starts.
    int arena_index_ = 0;
    // Road to WrestleMania: how far the run has got, and which card the ladder
    // is showing. Both live for the session only - there is no save file yet.
    road::Progress road_;
    int road_cursor_ = 0;
    std::string road_result_;
    bool on_road_ = false;  // the match that just ended was a road card
    // Index of the wrestler the human is driving; a tag hands it to the partner.
    int controlled_ = 0;
    bool tap_pending_ = false;
    float tap_x_ = 0.0f;
    float tap_y_ = 0.0f;
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_GAME_APP_H
