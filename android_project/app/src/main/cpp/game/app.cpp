#include "app.h"

#include "../audio_player.h"

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace wrestlefest {

using ::AudioPlayer;

namespace {

constexpr char kLogTag[] = "WrestleFestApp";

// Control layout in logical 960x640 space.
// Positions follow ui/Sprites/UI/manual/manual_background_0.png, the control
// layout sheet that ships with the game: d-pad bottom-left, blue button in the
// middle-right, red button above and to the right of it.
constexpr float kStickRadius = 92.0f;
constexpr float kStickDeadzone = 0.35f;  // per axis, as in the original
constexpr float kStickHomeX = 200.0f;
constexpr float kStickHomeY = 400.0f;
constexpr float kRedX = 735.0f;
constexpr float kRedY = 340.0f;
constexpr float kBlueX = 565.0f;
constexpr float kBlueY = 425.0f;
constexpr float kRedRadius = 62.0f;
constexpr float kBlueRadius = 52.0f;
// Pause button and the two choices of the pause menu.
constexpr float kPauseX = 930.0f;
constexpr float kPauseY = 592.0f;
constexpr float kPauseRadius = 34.0f;
constexpr float kResumeY = 268.0f;
constexpr float kQuitY = 336.0f;

bool insideCircle(float x, float y, float cx, float cy, float radius) {
    const float dx = x - cx;
    const float dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

std::string twoDigits(int value) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%02d", value);
    return buffer;
}

}  // namespace

// ---------------------------------------------------------------------------
// LoaderAssetSource
// ---------------------------------------------------------------------------
bool LoaderAssetSource::read(const std::string &path, std::vector<std::uint8_t> *bytes) {
    return assets_ != nullptr && assets_->readAsset(path, bytes);
}

bool LoaderAssetSource::exists(const std::string &path) {
    return assets_ != nullptr && assets_->hasAsset(path);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
bool GameApp::initialize(AssetLoader *assets, AudioPlayer *audio) {
    assets_ = assets;
    audio_ = audio;
    source_ = LoaderAssetSource(assets);

    std::string error;
    if (!data_.load(&source_, "data/Data", &error)) {
        error_ = error;
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "data load failed: %s", error.c_str());
        return false;
    }
    if (!renderer_.initialize(assets, &data_)) {
        error_ = "renderer init failed";
        return false;
    }

    // Default card: Cena against the biggest man on the roster, like the
    // original's demo match.
    const int count = static_cast<int>(data_.wrestlers().size());
    if (count > 0) {
        selected_player_ = 0;
        selected_opponent_ = 1;
        for (int i = 0; i < count; ++i) {
            const WrestlerProfile &w = data_.wrestlers()[static_cast<std::size_t>(i)];
            if (w.folder == "00_John Cena") selected_player_ = i;
        }
        int heaviest = 0;
        for (int i = 0; i < count; ++i) {
            const WrestlerProfile &w = data_.wrestlers()[static_cast<std::size_t>(i)];
            if (w.base.energy > heaviest && i != selected_player_) {
                heaviest = static_cast<int>(w.base.energy);
                selected_opponent_ = i;
            }
        }
    }
    scene_ = Scene::Title;
    ready_ = true;
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "loaded %zu states, %zu anims, %zu moves, %zu wrestlers",
                        data_.states().size(), data_.anims().size(), data_.moves().size(),
                        data_.wrestlers().size());
    return true;
}

void GameApp::shutdown() {
    renderer_.shutdown();
    ready_ = false;
}

void GameApp::startMatch(const WrestlerProfile *player, const WrestlerProfile *opponent) {
    if (player == nullptr || opponent == nullptr) return;
    const RingGeometry ring;
    match_.configure(&data_, ring, 0,
                     0x20120423u ^ static_cast<std::uint32_t>(clock_ * 1000.0f) ^ 0x9e3779b9u);
    match_.addWrestler(player, 0, ring.centerX() - 110.0f, ring.centerZ() + 20.0f, 1.0f);
    match_.addWrestler(opponent, 1, ring.centerX() + 110.0f, ring.centerZ() + 20.0f, -1.0f);
    match_.setAiControlled(0, false);  // the human
    match_.setAiControlled(1, true);
    beginMatchFlow(player, opponent);
}

void GameApp::beginMatchFlow(const WrestlerProfile *player, const WrestlerProfile *opponent) {
    // The flow owns the bell: entrance first, then the announcer, then FIGHT.
    match_.allowAutoStart(false);
    match_.playEntrance();

    intro_stage_ = IntroStage::Entrance;
    intro_timer_ = 0.0f;
    intro_stage_length_ = 3.2f;
    // Entrance music: the original streams a track per superstar; the WAV cue
    // set here is the announcer, which plays on the announce beats below.
    inputs_.assign(static_cast<std::size_t>(match_.wrestlerCount()), InputFrame{});
    accumulator_ = 0.0;
    finish_hold_ = 0.0f;
    red_edge_ = blue_edge_ = false;
    red_down_ = blue_down_ = false;
    stick_.active = false;
    scene_ = Scene::Match;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "match start: %s vs %s", player->name.c_str(),
                        opponent->name.c_str());
}

void GameApp::startRumble(const WrestlerProfile *player, const WrestlerProfile *opponent) {
    if (player == nullptr || opponent == nullptr) return;
    const RingGeometry ring;
    match_.configure(&data_, ring, 2,
                     0x20120423u ^ static_cast<std::uint32_t>(clock_ * 1000.0f) ^ 0x9e3779b9u);

    // The player is entrant one and starts in the ring; five more men fill the
    // card, each on his own side (a Rumble has no teams), walking in every 25 s.
    match_.addWrestler(player, 0, ring.centerX() - 120.0f, ring.centerZ() + 20.0f, 1.0f);
    match_.addWrestler(opponent, 1, ring.centerX() + 120.0f, ring.centerZ() + 20.0f, -1.0f);
    const int count = static_cast<int>(data_.wrestlers().size());
    int cursor = 0;
    for (int i = 2; i < 6 && count > 0; ++i) {
        int pick = -1;
        for (int attempts = 0; attempts < count; ++attempts) {
            const int candidate = (selected_player_ + 3 * i + cursor) % count;
            ++cursor;
            const WrestlerProfile &candidate_profile =
                data_.wrestlers()[static_cast<std::size_t>(candidate)];
            bool already_in = candidate_profile.folder == player->folder ||
                              candidate_profile.folder == opponent->folder;
            for (int j = 2; j < i && !already_in; ++j) {
                const Wrestler *w = match_.wrestler(j);
                already_in = w != nullptr && w->profile() != nullptr &&
                             w->profile()->folder == candidate_profile.folder;
            }
            if (!already_in) {
                pick = candidate;
                break;
            }
        }
        if (pick < 0) break;
        const float offset = -120.0f + 60.0f * static_cast<float>(i - 2);
        match_.addWrestler(&data_.wrestlers()[static_cast<std::size_t>(pick)], i,
                           ring.centerX() + offset, ring.centerZ(), 1.0f);
    }
    match_.setAiControlled(0, false);  // the human
    for (int i = 1; i < match_.wrestlerCount(); ++i) match_.setAiControlled(i, true);
    match_.enableStaggeredEntry(25.0f, 2);
    beginMatchFlow(player, opponent);
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void GameApp::update(double frame_seconds) {
    // Clamp so a stalled frame (app resume, debugger) cannot fast-forward the match.
    if (frame_seconds > 0.25) frame_seconds = 0.25;
    clock_ += static_cast<float>(frame_seconds);

    switch (scene_) {
        case Scene::Title:
            if (tap_pending_) {
                tap_pending_ = false;
                scene_ = Scene::Select;
            }
            break;
        case Scene::Select: {
            if (!tap_pending_) break;
            tap_pending_ = false;
            const float x = tap_x_;
            const float y = tap_y_;
            const int count = static_cast<int>(data_.wrestlers().size());
            if (count <= 1) break;
            // Mode chip, top right: taps between 1 VS 1 and the Royal Rumble.
            if (y > 14.0f && y < 62.0f && x > 660.0f && x < 946.0f) {
                mode_ = (mode_ + 1) % 2;
                break;
            }
            const bool on_fight = y > 480.0f && x > 60.0f && x < 900.0f;
            if (on_fight) {
                const WrestlerProfile &player =
                    data_.wrestlers()[static_cast<std::size_t>(selected_player_)];
                const WrestlerProfile &opponent =
                    data_.wrestlers()[static_cast<std::size_t>(selected_opponent_)];
                if (mode_ == 1) {
                    startRumble(&player, &opponent);
                } else {
                    startMatch(&player, &opponent);
                }
            } else if (x < 480.0f) {
                // Left panel: tap the arrows/portrait to cycle the player.
                selected_player_ = (selected_player_ + 1) % count;
                if (selected_player_ == selected_opponent_) {
                    selected_player_ = (selected_player_ + 1) % count;
                }
            } else {
                selected_opponent_ = (selected_opponent_ + 1) % count;
                if (selected_opponent_ == selected_player_) {
                    selected_opponent_ = (selected_opponent_ + 1) % count;
                }
            }
            break;
        }
        case Scene::Match: {
            if (paused_) break;
            if (intro_stage_ != IntroStage::Done) {
                updateIntro(static_cast<float>(frame_seconds));
                // The sim still ticks so the entrance animation plays.
                accumulator_ += frame_seconds;
                int intro_steps = 0;
                while (accumulator_ >= tuning::kFixedDelta &&
                       intro_steps < tuning::kMaxStepsPerFrame) {
                    match_.update(tuning::kFixedDelta, inputs_);
                    accumulator_ -= tuning::kFixedDelta;
                    ++intro_steps;
                }
                break;
            }
            accumulator_ += frame_seconds;
            int steps = 0;
            while (accumulator_ >= tuning::kFixedDelta && steps < tuning::kMaxStepsPerFrame) {
                inputs_[0] = InputFrame{};
                inputs_[0].left = stick_dir_x_ <= -kStickDeadzone;
                inputs_[0].right = stick_dir_x_ >= kStickDeadzone;
                inputs_[0].up = stick_dir_y_ <= -kStickDeadzone;
                inputs_[0].down = stick_dir_y_ >= kStickDeadzone;
                inputs_[0].btn0 = red_down_;
                inputs_[0].btn1 = blue_down_;
                inputs_[0].btn0_edge = red_edge_;
                inputs_[0].btn1_edge = blue_edge_;
                red_edge_ = false;
                blue_edge_ = false;
                stepSimulation(tuning::kFixedDelta);
                accumulator_ -= tuning::kFixedDelta;
                ++steps;
            }
            if (steps == tuning::kMaxStepsPerFrame) accumulator_ = 0.0;

            // In a Rumble the match outlives the player: once entrant one is
            // gone the result screen comes up (the original does the same).
            if (match_.isRumble() && match_.phase() == MatchPhase::Running) {
                const Wrestler *player_wrestler = match_.wrestler(0);
                if (player_wrestler != nullptr && player_wrestler->isDead()) {
                    match_.finish(-1);
                }
            }
            if (match_.phase() == MatchPhase::Finished) {
                finish_hold_ += static_cast<float>(frame_seconds);
                if (finish_hold_ > 2.5f) {
                    finish_hold_ = 0.0f;
                    scene_ = Scene::Result;
                }
            }
            break;
        }
        case Scene::Result:
            if (tap_pending_) {
                tap_pending_ = false;
                scene_ = Scene::Select;
            }
            break;
        case Scene::Loading:
            break;
    }
}

void GameApp::updateIntro(float dt) {
    intro_timer_ += dt;
    if (intro_timer_ < intro_stage_length_) return;
    intro_timer_ = 0.0f;

    const WrestlerProfile *player = data_.wrestlers().empty()
                                        ? nullptr
                                        : &data_.wrestlers()[static_cast<std::size_t>(selected_player_)];
    const WrestlerProfile *opponent = data_.wrestlers().empty()
                                          ? nullptr
                                          : &data_.wrestlers()[static_cast<std::size_t>(selected_opponent_)];
    auto announce = [&](const WrestlerProfile *profile) {
        const SfxInfo *cue = profile != nullptr ? profile->introCue() : nullptr;
        if (cue == nullptr) return 1.2f;
        playCue(("audio/sfx/" + cue->file_name).c_str());
        // Hold on the call for its own length (the table stores it).
        return cue->length > 0.0f ? cue->length : 2.5f;
    };

    switch (intro_stage_) {
        case IntroStage::Entrance:
            intro_stage_ = IntroStage::AnnouncePlayer;
            intro_stage_length_ = announce(player);
            break;
        case IntroStage::AnnouncePlayer:
            intro_stage_ = IntroStage::AnnounceOpponent;
            intro_stage_length_ = announce(opponent);
            break;
        case IntroStage::AnnounceOpponent:
            intro_stage_ = IntroStage::Fight;
            intro_stage_length_ = 1.1f;
            playCue("audio/sfx/9011_Large_Crowd_F01.wav");
            break;
        case IntroStage::Fight:
            intro_stage_ = IntroStage::Done;
            intro_stage_length_ = 0.0f;
            match_.start();  // the bell
            break;
        case IntroStage::Done:
            break;
    }
}

void GameApp::playCue(const char *asset) {
    if (audio_ == nullptr || assets_ == nullptr || asset == nullptr) return;
    audio_->playAsset(assets_, asset);
}

void GameApp::playRandomCue(const char *const *paths, int count, const char *tag) {
    if (count <= 0) return;
    const std::uint32_t roll = match_.rng().next();
    playCue(paths[roll % static_cast<std::uint32_t>(count)]);
    (void)tag;
}

void GameApp::stepSimulation(float dt) {
    match_.update(dt, inputs_);

    // Sound cues are event-driven, exactly like the original: the simulation
    // itself never touches audio (GAMEPLAY_IMPLEMENTATION 2.3).
    static const char *const kHitCues[] = {"audio/sfx/Attack01.wav", "audio/sfx/Attack02.wav",
                                           "audio/sfx/Attack03.wav"};
    static const char *const kPainCues[] = {"audio/sfx/Pain01.wav", "audio/sfx/Pain02.wav",
                                            "audio/sfx/Pain03.wav"};
    static const char *const kEffortCues[] = {"audio/sfx/Effort01.wav", "audio/sfx/Effort02.wav",
                                              "audio/sfx/Effort03.wav"};
    static const char *const kCrowdCues[] = {"audio/sfx/9010_Short_Crowd.wav",
                                             "audio/sfx/9007_Small_Crowd.wav"};

    for (const SimEvent &event : match_.events()) {
        switch (event.type) {
            case EventType::Hit:
                playRandomCue(kHitCues, 3, "hit");
                playRandomCue(kPainCues, 3, "pain");
                break;
            case EventType::Impact:
                // The power-up flash shares this event type (value 1 = on).
                if (event.value < 0.5f) {
                    playRandomCue(kEffortCues, 3, "effort");
                } else {
                    playRandomCue(kCrowdCues, 2, "crowd");
                }
                break;
            case EventType::KnockDown:
            case EventType::KnockedOut:
                playRandomCue(kPainCues, 3, "pain");
                break;
            case EventType::RopeBounce:
                playCue("audio/sfx/CrashWall.wav");
                break;
            case EventType::Bell:
                playCue("audio/sfx/50_G_sys_5001_OneRingBellMatch.wav");
                break;
            case EventType::PinStarted:
            case EventType::RefCount:
                playRandomCue(kCrowdCues, 2, "crowd");
                break;
            case EventType::Victory:
                playRandomCue(kCrowdCues, 2, "crowd");
                break;
            case EventType::Taunt:
                playRandomCue(kEffortCues, 3, "effort");
                break;
            default:
                break;
        }
    }
    match_.clearEvents();
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------
void GameApp::touchDown(int pointer_id, float x, float y) {
    if (scene_ == Scene::Match && paused_) {
        if (insideCircle(x, y, kPauseX, kPauseY, kPauseRadius)) {
            paused_ = false;  // the pause button doubles as resume
            return;
        }
        if (x > 300.0f && x < 660.0f) {
            if (y > kResumeY - 30.0f && y < kResumeY + 30.0f) {
                paused_ = false;
                playCue("audio/sfx/Attack01.wav");
            } else if (y > kQuitY - 30.0f && y < kQuitY + 30.0f) {
                paused_ = false;
                scene_ = Scene::Select;
            }
        }
        return;
    }
    if (scene_ == Scene::Match && insideCircle(x, y, kPauseX, kPauseY, kPauseRadius)) {
        paused_ = true;
        stick_.active = false;
        red_down_ = blue_down_ = false;
        stick_dir_x_ = stick_dir_y_ = 0.0f;
        return;
    }
    if (scene_ != Scene::Match) {
        tap_pending_ = true;
        tap_x_ = x;
        tap_y_ = y;
        return;
    }
    if (insideCircle(x, y, kRedX, kRedY, kRedRadius)) {
        button_red_.id = pointer_id;
        button_red_.active = true;
        red_down_ = true;
        red_edge_ = true;
    } else if (insideCircle(x, y, kBlueX, kBlueY, kBlueRadius)) {
        button_blue_.id = pointer_id;
        button_blue_.active = true;
        blue_down_ = true;
        blue_edge_ = true;
    } else if (x < 480.0f) {
        // Floating stick: its origin is the first touch point.
        stick_.id = pointer_id;
        stick_.start_x = x;
        stick_.start_y = y;
        stick_.x = x;
        stick_.y = y;
        stick_.active = true;
        stick_dir_x_ = 0.0f;
        stick_dir_y_ = 0.0f;
    }
}

void GameApp::touchMove(int pointer_id, float x, float y) {
    if (scene_ != Scene::Match) return;
    if (stick_.active && stick_.id == pointer_id) {
        stick_.x = x;
        stick_.y = y;
        const float dx = std::clamp((x - stick_.start_x) / kStickRadius, -1.0f, 1.0f);
        const float dy = std::clamp((y - stick_.start_y) / kStickRadius, -1.0f, 1.0f);
        stick_dir_x_ = dx;
        stick_dir_y_ = dy;
    }
    if (button_red_.active && button_red_.id == pointer_id &&
        !insideCircle(x, y, kRedX, kRedY, kRedRadius * 1.7f)) {
        button_red_.active = false;
        red_down_ = false;
    }
    if (button_blue_.active && button_blue_.id == pointer_id &&
        !insideCircle(x, y, kBlueX, kBlueY, kBlueRadius * 1.7f)) {
        button_blue_.active = false;
        blue_down_ = false;
    }
}

void GameApp::touchUp(int pointer_id) {
    if (scene_ != Scene::Match) return;
    if (stick_.active && stick_.id == pointer_id) {
        stick_.active = false;
        stick_.id = -1;
        stick_dir_x_ = 0.0f;
        stick_dir_y_ = 0.0f;
    }
    if (button_red_.active && button_red_.id == pointer_id) {
        button_red_.active = false;
        red_down_ = false;
    }
    if (button_blue_.active && button_blue_.id == pointer_id) {
        button_blue_.active = false;
        blue_down_ = false;
    }
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
void GameApp::render(int viewport_x, int viewport_y, int viewport_w, int viewport_h) {
    if (!ready_) return;
    renderer_.begin(viewport_x, viewport_y, viewport_w, viewport_h);
    switch (scene_) {
        case Scene::Title:
            drawTitle();
            break;
        case Scene::Select:
            drawSelect();
            break;
        case Scene::Match:
            renderer_.drawMatch(match_, 0.0f);
            renderer_.drawHud(match_, 0);
            if (intro_stage_ != IntroStage::Done) {
                drawIntroBanner();
            } else {
                drawTouchControls();
            }
            if (paused_) drawPauseOverlay();
            break;
        case Scene::Result:
            renderer_.drawMatch(match_, 0.0f);
            drawResult();
            break;
        case Scene::Loading:
            break;
    }
    renderer_.end();
}

void GameApp::drawTitle() {
    SpriteBatch &batch = renderer_.batch();
    // The shipped backdrop and logo, exactly as they are in the original.
    if (renderer_.uiBackground().valid()) {
        batch.draw(renderer_.uiBackground(), 0.0f, 0.0f, 960.0f, 640.0f, Color{});
    } else {
        batch.draw(renderer_.white(), 0.0f, 0.0f, 960.0f, 640.0f,
                   Color{0.05f, 0.06f, 0.10f, 1.0f});
    }
    if (renderer_.titleLogo().valid()) {
        const float logo_w = 790.0f * 0.85f;
        const float logo_h = 204.0f * 0.85f;
        batch.draw(renderer_.titleLogo(), 480.0f - logo_w * 0.5f, 110.0f, logo_w, logo_h, Color{});
    } else {
        renderer_.drawTextCentered("WRESTLEFEST", 480.0f, 150.0f, 2.0f,
                                   Color{1.0f, 0.82f, 0.18f, 1});
    }

    // Blinking "tap to start", 1 Hz like the original's attract loop.
    const float pulse = 0.55f + 0.45f * std::fabs(std::sin(clock_ * 2.2f));
    if (renderer_.tapToStart().valid()) {
        const float w = 374.0f;
        const float h = 47.0f;
        batch.draw(renderer_.tapToStart(), 480.0f - w * 0.5f, 420.0f, w, h,
                   Color{1.0f, 1.0f, 1.0f, pulse});
    } else {
        renderer_.drawTextCentered("TAP TO START", 480.0f, 420.0f, 0.85f,
                                   Color{1.0f, 1.0f, 1.0f, pulse});
    }

    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%d superstars  %d moves  %d states",
                  static_cast<int>(data_.wrestlers().size()),
                  static_cast<int>(data_.moves().size()), static_cast<int>(data_.states().size()));
    renderer_.drawTextCentered(buffer, 480.0f, 590.0f, 0.45f, Color{0.8f, 0.82f, 0.9f, 0.9f});
}

void GameApp::drawSelect() {
    SpriteBatch &batch = renderer_.batch();
    if (renderer_.uiBackground().valid()) {
        batch.draw(renderer_.uiBackground(), 0.0f, 0.0f, 960.0f, 640.0f, Color{0.7f, 0.7f, 0.75f, 1});
    } else {
        batch.draw(renderer_.white(), 0.0f, 0.0f, 960.0f, 640.0f, Color{0.04f, 0.05f, 0.08f, 1.0f});
    }
    renderer_.drawTextCentered("SELECT YOUR SUPERSTAR", 480.0f, 26.0f, 0.8f,
                               Color{1.0f, 1.0f, 1.0f, 1.0f});

    // Rule chip: 1 VS 1 or the Royal Rumble, straight from InGameTable.
    {
        const char *label = mode_ == 1 ? "ROYAL RUMBLE" : "1 VS 1";
        const bool rumble = mode_ == 1;
        batch.draw(renderer_.white(), 664.0f, 18.0f, 278.0f, 40.0f,
                   rumble ? Color{0.55f, 0.14f, 0.1f, 0.95f} : Color{0.12f, 0.2f, 0.36f, 0.95f});
        renderer_.drawTextCentered(label, 803.0f, 30.0f, 0.5f, Color{1.0f, 0.9f, 0.65f, 1.0f});
    }

    const int count = static_cast<int>(data_.wrestlers().size());
    if (count == 0) return;
    const int player_index = selected_player_;
    const int opponent_index = selected_opponent_;
    const WrestlerProfile &player = data_.wrestlers()[static_cast<std::size_t>(player_index)];
    const WrestlerProfile &opponent = data_.wrestlers()[static_cast<std::size_t>(opponent_index)];

    // Panels: 1P on the left, the CPU on the right, framed like the original.
    auto panel = [&](const WrestlerProfile &profile, float center_x, bool active) {
        if (renderer_.superstarFrame().valid()) {
            const float size = 300.0f;
            batch.draw(renderer_.superstarFrame(), center_x - size * 0.5f, 110.0f, size, size,
                       active ? Color{} : Color{0.55f, 0.55f, 0.6f, 1.0f});
        }
        // Idle portrait inside the frame.
        const int sst = profile.sprite_index.count("idle.sst") ? profile.sprite_index.at("idle.sst")
                                                               : -1;
        renderer_.drawActorPortrait(profile, sst, center_x, 380.0f, active ? 1.0f : 0.6f);

        renderer_.drawTextCentered(profile.name_line1, center_x, 396.0f, 0.7f,
                                   active ? Color{1.0f, 0.92f, 0.7f, 1} : Color{0.75f, 0.75f, 0.8f, 1});
        if (!profile.name_line2.empty()) {
            renderer_.drawTextCentered(profile.name_line2, center_x, 424.0f, 0.5f,
                                       Color{0.9f, 0.9f, 0.95f, 1});
        }
        char stats[96];
        std::snprintf(stats, sizeof(stats), "ENERGY %d  POWER %d",
                      static_cast<int>(profile.base.energy),
                      static_cast<int>(profile.base.motion_power));
        renderer_.drawTextCentered(stats, center_x, 452.0f, 0.4f, Color{0.8f, 0.85f, 0.95f, 1});
    };
    panel(player, 250.0f, true);
    panel(opponent, 710.0f, false);

    // Arrows: the previous/next sprites, mirrored for the left side.
    if (renderer_.arrowPrevious().valid()) {
        batch.draw(renderer_.arrowPrevious(), 100.0f, 250.0f, 41.0f, 64.0f, Color{});
        batch.draw(renderer_.arrowNext(), 816.0f, 250.0f, 41.0f, 64.0f, Color{});
    }
    if (renderer_.arrowNext().valid()) {
        batch.draw(renderer_.arrowNext(), 372.0f, 250.0f, 41.0f, 64.0f, Color{});
        batch.draw(renderer_.arrowPrevious(), 552.0f, 250.0f, 41.0f, 64.0f, Color{});
    }

    // FIGHT button: the shipped button strip with text over it.
    if (renderer_.buttonStrip().valid()) {
        batch.draw(renderer_.buttonStrip(), 480.0f - 388.0f, 500.0f, 776.0f, 49.0f,
                   Color{1.0f, 1.0f, 1.0f, 0.95f});
    } else {
        batch.draw(renderer_.white(), 330.0f, 500.0f, 300.0f, 60.0f,
                   Color{0.78f, 0.14f, 0.14f, 1.0f});
    }
    renderer_.drawTextCentered("FIGHT", 480.0f, 510.0f, 0.9f, Color{1.0f, 1.0f, 1.0f, 1.0f});
    renderer_.drawTextCentered("tap a panel to switch superstar   /   FIGHT to start", 480.0f, 590.0f,
                               0.42f, Color{0.9f, 0.9f, 1.0f, 1.0f});
}

void GameApp::drawResult() {
    SpriteBatch &batch = renderer_.batch();
    batch.draw(renderer_.white(), 0.0f, 430.0f, 960.0f, 210.0f, Color{0.0f, 0.0f, 0.0f, 0.7f});
    const bool player_won = match_.winnerTeam() == 0;
    renderer_.drawTextCentered(player_won ? "WINNER" : "DEFEATED", 480.0f, 470.0f, 1.4f,
                               player_won ? Color{1.0f, 0.85f, 0.2f, 1} : Color{0.9f, 0.3f, 0.3f, 1});
    if (player_won) {
        const Wrestler *w = match_.wrestler(0);
        if (w != nullptr && w->profile() != nullptr) {
            renderer_.drawTextCentered(w->profile()->name, 480.0f, 540.0f, 0.7f,
                                       Color{1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
    renderer_.drawTextCentered("tap to continue", 480.0f, 590.0f, 0.55f,
                               Color{0.9f, 0.9f, 0.95f, 1.0f});
}

void GameApp::drawPauseOverlay() {
    SpriteBatch &batch = renderer_.batch();
    if (renderer_.pauseMenuBackground().valid()) {
        batch.draw(renderer_.pauseMenuBackground(), 0.0f, 0.0f, 960.0f, 640.0f,
                   Color{1.0f, 1.0f, 1.0f, 0.86f});
    } else {
        batch.draw(renderer_.white(), 0.0f, 0.0f, 960.0f, 640.0f, Color{0.0f, 0.0f, 0.0f, 0.75f});
    }
    // The shipped backdrop is a light brushed-metal sheet, so the heading is
    // dark and the button labels stay white on the dark button art.
    renderer_.drawTextCentered("PAUSED", 480.0f, 168.0f, 1.5f, Color{0.07f, 0.09f, 0.13f, 1.0f});

    auto row = [&](const char *label, float y) {
        if (renderer_.pauseMenuButton().valid()) {
            batch.draw(renderer_.pauseMenuButton(), 480.0f - 368.0f, y, 736.0f, 51.0f,
                       Color{1.0f, 1.0f, 1.0f, 0.95f});
        } else {
            batch.draw(renderer_.white(), 480.0f - 300.0f, y, 600.0f, 51.0f,
                       Color{0.2f, 0.25f, 0.35f, 1.0f});
        }
        renderer_.drawTextCentered(label, 480.0f, y + 10.0f, 0.75f, Color{1.0f, 1.0f, 1.0f, 1.0f});
    };
    row("RESUME", kResumeY);
    row("QUIT TO SELECT", kQuitY);
    renderer_.drawTextCentered("RESUME or the pause button to keep playing", 480.0f, 560.0f, 0.4f,
                               Color{0.12f, 0.15f, 0.2f, 1.0f});
}

void GameApp::drawIntroBanner() {
    SpriteBatch &batch = renderer_.batch();
    batch.draw(renderer_.white(), 0.0f, 500.0f, 960.0f, 140.0f, Color{0.0f, 0.0f, 0.0f, 0.55f});
    const WrestlerProfile *player = nullptr;
    const WrestlerProfile *opponent = nullptr;
    if (!data_.wrestlers().empty()) {
        player = &data_.wrestlers()[static_cast<std::size_t>(selected_player_)];
        opponent = &data_.wrestlers()[static_cast<std::size_t>(selected_opponent_)];
    }
    switch (intro_stage_) {
        case IntroStage::Entrance:
            renderer_.drawTextCentered("MAKING THEIR WAY TO THE RING", 480.0f, 520.0f, 0.6f,
                                       Color{0.9f, 0.9f, 1.0f, 1.0f});
            break;
        case IntroStage::AnnouncePlayer:
            if (player != nullptr) {
                renderer_.drawTextCentered(player->name, 480.0f, 510.0f, 1.1f,
                                           Color{1.0f, 0.9f, 0.55f, 1.0f});
            }
            break;
        case IntroStage::AnnounceOpponent:
            if (opponent != nullptr) {
                renderer_.drawTextCentered(opponent->name, 480.0f, 510.0f, 1.1f,
                                           Color{1.0f, 0.9f, 0.55f, 1.0f});
            }
            break;
        case IntroStage::Fight:
            renderer_.drawTextCentered("FIGHT!", 480.0f, 500.0f, 2.0f, Color{1.0f, 0.25f, 0.2f, 1});
            break;
        case IntroStage::Done:
            break;
    }
}

void GameApp::drawTouchControls() {
    SpriteBatch &batch = renderer_.batch();

    // D-pad: a floating stick whose base appears wherever the thumb lands.
    const float base_x = stick_.active ? stick_.start_x : kStickHomeX;
    const float base_y = stick_.active ? stick_.start_y : kStickHomeY;
    const float base_size = kStickRadius * 2.0f;
    if (renderer_.stickBase().valid()) {
        batch.draw(renderer_.stickBase(), base_x - base_size * 0.5f, base_y - base_size * 0.5f,
                   base_size, base_size, Color{1.0f, 1.0f, 1.0f, 0.55f});
    } else {
        batch.draw(renderer_.white(), base_x - kStickRadius, base_y - kStickRadius, base_size,
                   base_size, Color{1.0f, 1.0f, 1.0f, 0.12f});
    }
    float icon_x = base_x + stick_dir_x_ * kStickRadius * 0.5f;
    float icon_y = base_y + stick_dir_y_ * kStickRadius * 0.5f;
    if (renderer_.stickIcon().valid()) {
        const float icon = kStickRadius * 0.78f;
        batch.draw(renderer_.stickIcon(), icon_x - icon * 0.5f, icon_y - icon * 0.5f, icon, icon,
                   Color{1.0f, 1.0f, 1.0f, 0.8f});
    }

    // Two buttons, red (punch) and blue (grapple), exactly like the manual page.
    auto drawButton = [&](const Texture &base, const Texture &icon, float cx, float cy,
                          float radius, bool down) {
        const float size = radius * 2.0f;
        const Color tint = down ? Color{0.75f, 0.75f, 0.75f, 1.0f} : Color{1.0f, 1.0f, 1.0f, 0.72f};
        if (base.valid()) {
            const float draw = down ? size * 0.94f : size;
            batch.draw(base, cx - draw * 0.5f, cy - draw * 0.5f, draw, draw,
                       down ? Color{1.0f, 1.0f, 1.0f, 0.95f} : tint);
        } else {
            batch.draw(renderer_.white(), cx - radius, cy - radius, size, size,
                       down ? Color{1.0f, 0.3f, 0.3f, 0.8f} : Color{0.4f, 0.4f, 0.5f, 0.4f});
        }
        if (icon.valid()) {
            const float icon_size = radius * 1.05f;
            batch.draw(icon, cx - icon_size * 0.5f, cy - icon_size * 0.5f, icon_size, icon_size,
                       Color{1.0f, 1.0f, 1.0f, 0.9f});
        }
    };
    drawButton(renderer_.redButton(), renderer_.redButtonIcon(), kRedX, kRedY, kRedRadius,
               red_down_);
    drawButton(renderer_.blueButton(), renderer_.blueButtonIcon(), kBlueX, kBlueY, kBlueRadius,
               blue_down_);

    // Pause button, bottom right, as in the original.
    if (renderer_.pauseButton().valid()) {
        batch.draw(renderer_.pauseButton(), kPauseX - kPauseRadius, kPauseY - kPauseRadius,
                   kPauseRadius * 2.0f, kPauseRadius * 2.0f, Color{1.0f, 1.0f, 1.0f, 0.6f});
    }

    // Mash prompt while the player is caught in a pin.
    const Wrestler *player = match_.wrestler(0);
    if (player != nullptr && player->subState() == state::BE_PINNED) {
        renderer_.drawTextCentered("MASH THE BUTTONS!", 480.0f, 250.0f, 0.8f,
                                   Color{1.0f, 0.9f, 0.3f, 1.0f});
    }
}

}  // namespace wrestlefest
