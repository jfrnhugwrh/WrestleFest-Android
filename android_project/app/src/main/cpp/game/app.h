#ifndef WRESTLEFEST_GAME_APP_H
#define WRESTLEFEST_GAME_APP_H

// The application shell: asset source, match flow, touch input and the fixed
// 60 Hz simulation step (GAMEPLAY_IMPLEMENTATION 2.5).
//
// Flow mirrors the original: Title -> Superstar select -> Match -> Result.

#include "../asset_loader.h"
#include "../data/game_data.h"
#include "../render/gl_sprite.h"
#include "../sim/ai.h"
#include "../sim/match.h"
#include "game_renderer.h"

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
    Select,
    Match,
    Result,
};

struct TouchPoint {
    float x = 0.0f;  // logical coordinates
    float y = 0.0f;
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
    void stepSimulation(float dt);
    void updateTouchInput();
    void drawTitle();
    void drawSelect();
    void drawResult();
    void drawTouchControls();
    void updateIntro(float dt);
    void drawIntroBanner();
    void drawPauseOverlay();

    void playCue(const char *asset);
    void playRandomCue(const char *const *paths, int count, const char *tag);

    AssetLoader *assets_ = nullptr;
    AudioPlayer *audio_ = nullptr;
    LoaderAssetSource source_{nullptr};
    GameData data_;
    GameRenderer renderer_;
    Match match_;
    std::unique_ptr<AiEntity> dummy_ai_;
    std::vector<InputFrame> inputs_;

    // Pre-match staging (GAMEPLAY_IMPLEMENTATION 4.3): entrance -> announcer
    // for each wrestler -> bell / "FIGHT".
    enum class IntroStage {
        Entrance,
        AnnouncePlayer,
        AnnounceOpponent,
        Fight,
        Done,
    };
    IntroStage intro_stage_ = IntroStage::Entrance;
    float intro_timer_ = 0.0f;
    float intro_stage_length_ = 0.0f;

    Scene scene_ = Scene::Loading;
    bool ready_ = false;
    std::string error_;

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
    bool red_down_ = false;
    bool blue_down_ = false;
    bool red_edge_ = false;
    bool blue_edge_ = false;
    bool paused_ = false;
    bool tap_pending_ = false;
    float tap_x_ = 0.0f;
    float tap_y_ = 0.0f;
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_GAME_APP_H
