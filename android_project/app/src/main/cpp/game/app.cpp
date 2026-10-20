#include "app.h"

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace wrestlefest {
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
bool GameApp::initialize(AssetLoader *assets) {
    assets_ = assets;
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
    match_.start();
    inputs_.assign(2, InputFrame{});
    accumulator_ = 0.0;
    finish_hold_ = 0.0f;
    red_edge_ = blue_edge_ = false;
    red_down_ = blue_down_ = false;
    stick_.active = false;
    scene_ = Scene::Match;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "match start: %s vs %s", player->name.c_str(),
                        opponent->name.c_str());
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
            // Bottom = fight, left half = player, right half = opponent.
            if (y > 430.0f && x > 300.0f && x < 660.0f) {
                startMatch(&data_.wrestlers()[static_cast<std::size_t>(selected_player_)],
                           &data_.wrestlers()[static_cast<std::size_t>(selected_opponent_)]);
            } else if (x < 480.0f) {
                selected_player_ = (selected_player_ + 1) % count;
            } else {
                selected_opponent_ = (selected_opponent_ + 1) % count;
            }
            break;
        }
        case Scene::Match: {
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

void GameApp::stepSimulation(float dt) {
    match_.update(dt, inputs_);
    for (const SimEvent &event : match_.events()) {
        switch (event.type) {
            case EventType::Hit:
            case EventType::Impact:
            case EventType::KnockDown:
            case EventType::KnockedOut:
                // SFX hook: Attack01.wav / Pain samples, chosen by the actor.
                break;
            case EventType::Bell:
                // SFX hook: 50_G_sys_5001_OneRingBellMatch.wav.
                break;
            case EventType::Announcer:
                // Announcer hook: RA_JR_SSN_<WRESTLER>_<take>.wav.
                break;
            case EventType::Victory:
            case EventType::Lose:
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
            drawTouchControls();
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
    batch.draw(renderer_.white(), 0.0f, 0.0f, 960.0f, 640.0f, Color{0.05f, 0.06f, 0.10f, 1.0f});
    renderer_.drawTextCentered("WRESTLEFEST", 480.0f, 170.0f, 2.0f, Color{1.0f, 0.82f, 0.18f, 1});
    renderer_.drawTextCentered("ANDROID", 480.0f, 250.0f, 0.9f, Color{0.95f, 0.95f, 1.0f, 1});
    renderer_.drawTextCentered("TAP TO START", 480.0f, 420.0f, 0.85f,
                               Color{1.0f, 1.0f, 1.0f, 1.0f});

    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%d superstars  %d moves  %d states",
                  static_cast<int>(data_.wrestlers().size()),
                  static_cast<int>(data_.moves().size()), static_cast<int>(data_.states().size()));
    renderer_.drawTextCentered(buffer, 480.0f, 560.0f, 0.5f, Color{0.65f, 0.68f, 0.78f, 1.0f});
}

void GameApp::drawSelect() {
    SpriteBatch &batch = renderer_.batch();
    batch.draw(renderer_.white(), 0.0f, 0.0f, 960.0f, 640.0f, Color{0.04f, 0.05f, 0.08f, 1.0f});
    renderer_.drawTextCentered("SELECT YOUR SUPERSTAR", 480.0f, 24.0f, 0.9f,
                               Color{1.0f, 1.0f, 1.0f, 1.0f});

    const int count = static_cast<int>(data_.wrestlers().size());
    if (count == 0) return;
    const WrestlerProfile &player = data_.wrestlers()[static_cast<std::size_t>(selected_player_)];
    const WrestlerProfile &opponent =
        data_.wrestlers()[static_cast<std::size_t>(selected_opponent_)];
    const WrestlerProfile &highlight = (cast_row_ == 0) ? player : opponent;

    // Panels.
    batch.draw(renderer_.white(), 40.0f, 90.0f, 400.0f, 300.0f,
               cast_row_ == 0 ? Color{0.20f, 0.26f, 0.40f, 1.0f} : Color{0.10f, 0.12f, 0.18f, 1.0f});
    batch.draw(renderer_.white(), 520.0f, 90.0f, 400.0f, 300.0f,
               cast_row_ == 1 ? Color{0.20f, 0.26f, 0.40f, 1.0f} : Color{0.10f, 0.12f, 0.18f, 1.0f});

    renderer_.drawTextCentered("1P", 240.0f, 100.0f, 0.6f, Color{0.75f, 0.8f, 0.9f, 1.0f});
    renderer_.drawTextCentered("CPU", 720.0f, 100.0f, 0.6f, Color{0.75f, 0.8f, 0.9f, 1.0f});
    renderer_.drawTextCentered(player.name, 240.0f, 180.0f, 0.72f, Color{1.0f, 0.9f, 0.6f, 1});
    renderer_.drawTextCentered(opponent.name, 720.0f, 180.0f, 0.72f, Color{1.0f, 0.9f, 0.6f, 1});

    char stats[128];
    std::snprintf(stats, sizeof(stats), "ENERGY %d   POWER %d", static_cast<int>(player.base.energy),
                  static_cast<int>(player.base.motion_power));
    renderer_.drawTextCentered(stats, 240.0f, 300.0f, 0.45f, Color{0.8f, 0.85f, 0.95f, 1});
    std::snprintf(stats, sizeof(stats), "ENERGY %d   POWER %d",
                  static_cast<int>(opponent.base.energy),
                  static_cast<int>(opponent.base.motion_power));
    renderer_.drawTextCentered(stats, 720.0f, 300.0f, 0.45f, Color{0.8f, 0.85f, 0.95f, 1});

    // Arrow blocks: << >> on each panel.
    const Color arrow{0.32f, 0.38f, 0.55f, 1.0f};
    batch.draw(renderer_.white(), 60.0f, 210.0f, 46.0f, 70.0f, arrow);
    batch.draw(renderer_.white(), 374.0f, 210.0f, 46.0f, 70.0f, arrow);
    batch.draw(renderer_.white(), 540.0f, 210.0f, 46.0f, 70.0f, arrow);
    batch.draw(renderer_.white(), 854.0f, 210.0f, 46.0f, 70.0f, arrow);

    batch.draw(renderer_.white(), 330.0f, 460.0f, 300.0f, 90.0f,
               Color{0.78f, 0.14f, 0.14f, 1.0f});
    renderer_.drawTextCentered("FIGHT", 480.0f, 484.0f, 1.15f, Color{1.0f, 1.0f, 1.0f, 1});
    renderer_.drawTextCentered("tap a name panel to switch, FIGHT to start", 480.0f, 590.0f,
                               0.45f, Color{0.65f, 0.7f, 0.8f, 1.0f});
    (void)highlight;
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

void GameApp::drawTouchControls() {
    SpriteBatch &batch = renderer_.batch();
    const Texture &stick_base = renderer_.stickBase();
    const Texture &stick_icon = renderer_.stickIcon();
    const Texture &red = renderer_.redButton();
    const Texture &blue = renderer_.blueButton();

    // The stick appears where the thumb landed; it falls back to its home spot.
    float base_x = stick_.active ? stick_.start_x : kStickHomeX;
    float base_y = stick_.active ? stick_.start_y : kStickHomeY;
    const float size = kStickRadius * 2.0f;
    if (stick_base.valid()) {
        batch.draw(stick_base, base_x - size * 0.5f, base_y - size * 0.5f, size, size,
                   Color{1.0f, 1.0f, 1.0f, 0.55f});
    }
    float icon_x = base_x;
    float icon_y = base_y;
    if (stick_.active) {
        icon_x += stick_dir_x_ * kStickRadius * 0.55f;
        icon_y += stick_dir_y_ * kStickRadius * 0.55f;
    }
    const float icon_size = 64.0f;
    if (stick_icon.valid()) {
        batch.draw(stick_icon, icon_x - icon_size * 0.5f, icon_y - icon_size * 0.5f, icon_size,
                   icon_size, Color{1.0f, 1.0f, 1.0f, 0.75f});
    } else {
        batch.draw(renderer_.white(), icon_x - 20.0f, icon_y - 20.0f, 40.0f, 40.0f,
                   Color{1.0f, 1.0f, 1.0f, 0.25f});
    }

    const float red_size = kRedRadius * 2.0f;
    const float blue_size = kBlueRadius * 2.0f;
    const Color red_tint = red_down_ ? Color{1.0f, 0.55f, 0.55f, 0.95f}
                                     : Color{1.0f, 1.0f, 1.0f, 0.6f};
    const Color blue_tint = blue_down_ ? Color{0.55f, 0.65f, 1.0f, 0.95f}
                                       : Color{1.0f, 1.0f, 1.0f, 0.6f};
    if (red.valid()) {
        batch.draw(red, kRedX - red_size * 0.5f, kRedY - red_size * 0.5f, red_size, red_size,
                   red_tint);
    }
    if (blue.valid()) {
        batch.draw(blue, kBlueX - blue_size * 0.5f, kBlueY - blue_size * 0.5f, blue_size,
                   blue_size, blue_tint);
    }
}

}  // namespace wrestlefest
