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

    // The renderer comes up first so a data failure can still be drawn on the
    // loading card instead of leaving a black screen.
    if (!renderer_.initialize(assets, &data_)) {
        error_ = "renderer init failed";
        return false;
    }
    // From here on the app can render: a data failure is reported on the
    // loading card instead of leaving a black screen.
    ready_ = true;
    menu_.attach(&renderer_, &data_);

    std::string error;
    if (!data_.load(&source_, "data/Data", &error)) {
        error_ = error;
        data_ok_ = false;
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "data load failed: %s", error.c_str());
        return true;
    }
    data_ok_ = true;

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
    // Open on the selected wrestler's page of the grid and on the first arena,
    // like the original does when it builds the scenes.
    superstar_page_ = pageOfSelection(selected_player_, menu::kGridPageSize);
    arena_page_ = pageOfSelection(arena_index_, menu::kArenaPageSize);
    scene_ = Scene::Loading;
    loading_timer_ = 0.0f;
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
    applyArena();
    const RingGeometry ring;
    match_.configure(&data_, ring, 0,
                     0x20120423u ^ static_cast<std::uint32_t>(clock_ * 1000.0f) ^ 0x9e3779b9u);
    match_.addWrestler(player, 0, ring.centerX() - 110.0f, ring.centerZ() + 20.0f, 1.0f);
    match_.addWrestler(opponent, 1, ring.centerX() + 110.0f, ring.centerZ() + 20.0f, -1.0f);
    controlled_ = 0;
    match_.setAiControlled(0, false);  // the human
    match_.setAiControlled(1, true);
    beginMatchFlow(player, opponent);
}

void GameApp::beginMatchFlow(const WrestlerProfile *player, const WrestlerProfile *opponent) {
    // The flow owns the bell: entrance first, then the announcer, then FIGHT.
    match_.allowAutoStart(false);
    match_.playEntrance();

    inputs_.assign(static_cast<std::size_t>(match_.wrestlerCount()), InputFrame{});
    accumulator_ = 0.0;
    finish_hold_ = 0.0f;
    red_edge_ = blue_edge_ = false;
    red_down_ = blue_down_ = false;
    stick_.active = false;
    intro_skip_ = false;
    entrance_stage_ = nullptr;

    // CGameEntrance: every man walks out on the arena's entrance stage before
    // the ring announcer introduces him. The stage is the arena's own - it is
    // picked by AllArenaTable.EnrtanceSceneId into AllEntranceSceneTable.
    setupEntranceShows();
    entrance_show_ = 0;
    beginEntranceShow();

    scene_ = Scene::Match;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "match start: %s vs %s", player->name.c_str(),
                        opponent->name.c_str());
}

// ---------------------------------------------------------------------------
// Entrance cutscene (CGameEntrance)
// ---------------------------------------------------------------------------
void GameApp::setupEntranceShows() {
    entrance_shows_.clear();
    const int count = match_.wrestlerCount();
    if (count < 2) return;
    if (match_.isRumble()) {
        // A Rumble only shows the player: five more men walk in during the
        // match, and the card is not known until they do.
        entrance_shows_.push_back({0, -1, 0});
        return;
    }
    if (match_.tagEnabled()) {
        // Tag: both partners walk out together (SetupWrestler(partner, true)).
        entrance_shows_.push_back({0, 1, 0});
        entrance_shows_.push_back({2, 3, 1});
        return;
    }
    entrance_shows_.push_back({0, -1, 0});
    entrance_shows_.push_back({1, -1, 1});
}

const EntranceStage &GameApp::entranceStage() {
    if (entrance_stage_ != nullptr) return *entrance_stage_;
    int scene_id = 0;
    if (!data_.arenas().empty()) {
        const int index = std::clamp(arena_index_, 0, static_cast<int>(data_.arenas().size()) - 1);
        scene_id = data_.arenas()[static_cast<std::size_t>(index)].entrance_scene_id;
    }
    entrance_stage_ = &renderer_.entranceFor(scene_id);
    return *entrance_stage_;
}

void GameApp::beginEntranceShow() {
    if (entrance_show_ >= entrance_shows_.size()) {
        beginAnnouncePlayer();
        return;
    }
    const EntranceStage &stage = entranceStage();
    intro_stage_ = entrance_show_ == 0 ? IntroStage::EntrancePlayer : IntroStage::EntranceOpponent;
    intro_timer_ = 0.0f;
    intro_skip_ = false;
    // MaxShowTime from AllEntranceSceneTable, 6 s for the modern stages and 8 s
    // for the basic one. A tap on the screen cuts it short.
    intro_stage_length_ = stage.valid ? std::max(2.0f, stage.max_show_time) : 6.0f;
    match_.setAnnouncerCue(AnnouncerCue::Entrance);
    // Entrance music: the original streams a per-superstar track (CMusic 4 for
    // the legends, 5 for the moderns); this port only has the WAV cue set, so
    // the crowd and the pyro sting stand in for it.
    playCue("audio/sfx/crowd_entrance_1shot_pos_lev1.wav");
    playCue("audio/sfx/9011_Large_Crowd_F01.wav");
}

void GameApp::beginAnnouncePlayer() {
    match_.endEntrance();  // out of the aisle, into the ring, waiting for the bell
    intro_stage_ = IntroStage::AnnouncePlayer;
    intro_timer_ = 0.0f;
    intro_skip_ = false;
    match_.setAnnouncerCue(AnnouncerCue::AnnounceLeft);
    intro_stage_length_ = announce(playerProfile());
}

void GameApp::beginAnnounceOpponent() {
    intro_stage_ = IntroStage::AnnounceOpponent;
    intro_timer_ = 0.0f;
    intro_skip_ = false;
    match_.setAnnouncerCue(AnnouncerCue::AnnounceRight);
    intro_stage_length_ = announce(opponentProfile());
}

void GameApp::beginFight() {
    intro_stage_ = IntroStage::Fight;
    intro_timer_ = 0.0f;
    intro_skip_ = false;
    match_.setAnnouncerCue(AnnouncerCue::FightExit);
    intro_stage_length_ = 1.1f;
    playCue("audio/sfx/9011_Large_Crowd_F01.wav");
}

void GameApp::advanceIntro() {
    switch (intro_stage_) {
        case IntroStage::EntrancePlayer:
        case IntroStage::EntranceOpponent:
            ++entrance_show_;
            if (entrance_show_ < entrance_shows_.size()) {
                beginEntranceShow();
                return;
            }
            beginAnnouncePlayer();
            return;
        case IntroStage::AnnouncePlayer:
            beginAnnounceOpponent();
            return;
        case IntroStage::AnnounceOpponent:
            beginFight();
            return;
        case IntroStage::Fight:
            intro_stage_ = IntroStage::Done;
            intro_stage_length_ = 0.0f;
            match_.setAnnouncerCue(AnnouncerCue::Hidden);
            match_.start();  // the bell
            return;
        case IntroStage::Done:
            return;
    }
}

void GameApp::startRumble(const WrestlerProfile *player, const WrestlerProfile *opponent) {
    if (player == nullptr || opponent == nullptr) return;
    applyArena();
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
    controlled_ = 0;
    match_.setAiControlled(0, false);  // the human
    for (int i = 1; i < match_.wrestlerCount(); ++i) match_.setAiControlled(i, true);
    match_.enableStaggeredEntry(25.0f, 2);
    beginMatchFlow(player, opponent);
}

void GameApp::startTagMatch(const WrestlerProfile *player, const WrestlerProfile *opponent) {
    if (player == nullptr || opponent == nullptr) return;
    applyArena();
    const RingGeometry ring;
    match_.configure(&data_, ring, 1,
                     0x20120423u ^ static_cast<std::uint32_t>(clock_ * 1000.0f) ^ 0x9e3779b9u);

    const int count = static_cast<int>(data_.wrestlers().size());
    auto next_distinct = [&](int from, const std::string &a, const std::string &b,
                             const std::string &c) {
        for (int step = 1; step <= count; ++step) {
            const int index = (from + step) % count;
            const std::string &folder = data_.wrestlers()[static_cast<std::size_t>(index)].folder;
            if (folder != a && folder != b && folder != c) return index;
        }
        return (from + 1) % count;
    };

    const int partner_index = next_distinct(selected_player_, player->folder, opponent->folder, "");
    const WrestlerProfile &partner = data_.wrestlers()[static_cast<std::size_t>(partner_index)];
    const int rival_index =
        next_distinct(selected_opponent_, player->folder, opponent->folder, partner.folder);
    const WrestlerProfile &rival = data_.wrestlers()[static_cast<std::size_t>(rival_index)];

    // Team 0: the player and his partner. Team 1: the CPU pair. The partner
    // waits on the apron until he is tagged in.
    match_.addWrestler(player, 0, ring.centerX() - 120.0f, ring.centerZ() + 20.0f, 1.0f);
    match_.addWrestler(&partner, 0, ring.centerX() - 60.0f, ring.centerZ() + 20.0f, 1.0f);
    match_.addWrestler(opponent, 1, ring.centerX() + 120.0f, ring.centerZ() + 20.0f, -1.0f);
    match_.addWrestler(&rival, 1, ring.centerX() + 60.0f, ring.centerZ() + 20.0f, -1.0f);
    controlled_ = 0;
    match_.setAiControlled(0, false);  // the human
    for (int i = 1; i < match_.wrestlerCount(); ++i) match_.setAiControlled(i, true);
    match_.enableTagTeams(true);
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
        case Scene::Loading:
            loading_timer_ += static_cast<float>(frame_seconds);
            if (data_ok_ && (loading_timer_ > 1.6f || tap_pending_)) {
                tap_pending_ = false;
                enterScene(Scene::Title);
            }
            break;
        case Scene::Title:
            if (tap_pending_) {
                tap_pending_ = false;
                enterScene(Scene::MainMenu);
            }
            break;
        case Scene::MainMenu:
        case Scene::Modes:
        case Scene::Select:
        case Scene::Arena:
        case Scene::Road:
        case Scene::Options:
            updateMenus(static_cast<float>(frame_seconds));
            break;
        case Scene::Match: {
            if (paused_) break;
            if (intro_stage_ != IntroStage::Done) {
                updateIntro(static_cast<float>(frame_seconds));
                // The sim still ticks so the entrance animation plays.
                accumulator_ += frame_seconds * tuning::kGameSpeedScale;
                int intro_steps = 0;
                while (accumulator_ >= tuning::kFixedDelta &&
                       intro_steps < tuning::kMaxStepsPerFrame) {
                    match_.update(tuning::kFixedDelta, inputs_);
                    accumulator_ -= tuning::kFixedDelta;
                    ++intro_steps;
                }
                break;
            }
            accumulator_ += frame_seconds * tuning::kGameSpeedScale;
            int steps = 0;
            while (accumulator_ >= tuning::kFixedDelta && steps < tuning::kMaxStepsPerFrame) {
                for (InputFrame &frame : inputs_) frame = InputFrame{};
                // Hysteresis on the analogue stick: a direction engages at the
                // deadzone and releases only well inside it. A finger hovering
                // at the 0.35 boundary used to flicker IDLE<->WALK every frame,
                // restarting the walk animation each time (visible jitter).
                constexpr float kStickRelease = kStickDeadzone * 0.6f;
                if (stick_dir_x_ <= -kStickDeadzone) stick_axis_x_ = -1;
                else if (stick_dir_x_ >= kStickDeadzone) stick_axis_x_ = 1;
                else if (std::fabs(stick_dir_x_) <= kStickRelease) stick_axis_x_ = 0;
                if (stick_dir_y_ <= -kStickDeadzone) stick_axis_z_ = -1;
                else if (stick_dir_y_ >= kStickDeadzone) stick_axis_z_ = 1;
                else if (std::fabs(stick_dir_y_) <= kStickRelease) stick_axis_z_ = 0;
                InputFrame &human = inputs_[static_cast<std::size_t>(controlled_)];
                human.left = stick_axis_x_ < 0;
                human.right = stick_axis_x_ > 0;
                human.up = stick_axis_z_ < 0;
                human.down = stick_axis_z_ > 0;
                human.btn0 = red_down_;
                human.btn1 = blue_down_;
                human.btn0_edge = red_edge_;
                human.btn1_edge = blue_edge_;
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
                if (on_road_) {
                    returnFromRoadMatch();
                } else {
                    enterScene(Scene::MainMenu);
                }
            }
            break;
    }
}

// ---------------------------------------------------------------------------
// Front end
// ---------------------------------------------------------------------------
int GameApp::pageCount(int item_count, int per_page) const {
    if (per_page <= 0) return 1;
    return std::max(1, (item_count + per_page - 1) / per_page);
}

int GameApp::pageOfSelection(int selection, int per_page) const {
    if (per_page <= 0) return 0;
    return std::max(0, selection) / per_page;
}

int GameApp::clampPage(int page, int page_count) {
    return std::clamp(page, 0, std::max(0, page_count - 1));
}

const WrestlerProfile *GameApp::playerProfile() const {
    if (data_.wrestlers().empty()) return nullptr;
    const int index = std::clamp(selected_player_, 0, static_cast<int>(data_.wrestlers().size()) - 1);
    return &data_.wrestlers()[static_cast<std::size_t>(index)];
}

const WrestlerProfile *GameApp::opponentProfile() const {
    if (data_.wrestlers().empty()) return nullptr;
    const int index =
        std::clamp(selected_opponent_, 0, static_cast<int>(data_.wrestlers().size()) - 1);
    return &data_.wrestlers()[static_cast<std::size_t>(index)];
}

void GameApp::enterScene(Scene scene) {
    scene_ = scene;
    // Leaving the match by any route other than the result screen abandons the
    // road run, so the next result does not jump back to the ladder.
    if (scene != Scene::Result) on_road_ = false;
    confirm_timer_ = 0.0f;
    confirm_action_ = false;
    notice_timer_ = 0.0f;
    tap_pending_ = false;
    if (scene == Scene::Road) {
        road_result_.clear();
        road_cursor_ = road_.stage;
    }
    if (scene == Scene::Select) {
        superstar_page_ = clampPage(pageOfSelection(selected_player_, menu::kGridPageSize),
                                    pageCount(static_cast<int>(data_.wrestlers().size()),
                                              menu::kGridPageSize));
        superstar_pick_ = -1;
    } else if (scene == Scene::Arena) {
        arena_page_ = clampPage(pageOfSelection(arena_index_, menu::kArenaPageSize),
                                pageCount(static_cast<int>(data_.arenas().size()),
                                          menu::kArenaPageSize));
        arena_pick_ = -1;
    }
}

void GameApp::showNotice(const std::string &text) {
    notice_ = text;
    notice_timer_ = 2.2f;
    playCue("audio/sfx/Ui01.wav");
}

void GameApp::updateMenus(float dt) {
    if (notice_timer_ > 0.0f) notice_timer_ -= dt;
    if (confirm_timer_ > 0.0f) {
        confirm_timer_ -= dt;
        if (confirm_timer_ <= 0.0f && confirm_action_) {
            confirm_action_ = false;
            if (scene_ == Scene::Select) {
                // Road to WrestleMania picks the arena per card, so there is
                // no arena screen on the way - straight to the ladder.
                enterScene(mode_ == menu::kRuleRoadToWrestleMania ? Scene::Road : Scene::Arena);
            } else if (scene_ == Scene::Arena) {
                startSelectedMatch();
            }
        }
    }
    if (tap_pending_) {
        tap_pending_ = false;
        handleMenuTap(tap_x_, tap_y_);
    }
}

void GameApp::handleMenuTap(float x, float y) {
    switch (scene_) {
        case Scene::MainMenu:
            handleMainMenuTap(x, y);
            break;
        case Scene::Modes:
            handleModesTap(x, y);
            break;
        case Scene::Select:
            handleSuperstarTap(x, y);
            break;
        case Scene::Arena:
            handleArenaTap(x, y);
            break;
        case Scene::Road:
            handleRoadTap(x, y);
            break;
        case Scene::Options:
            handleOptionsTap(x, y);
            break;
        default:
            break;
    }
}

void GameApp::handleMainMenuTap(float x, float y) {
    // The back arrow on the top screen has nowhere to go: it is drawn faded and
    // ignored, exactly like the original's inert info bar.
    if (menu::backHit(x, y)) return;
    const int entry = menu::mainEntryAt(x, y);
    if (entry < 0) return;
    main_entry_ = entry;
    if (!menu::mainEntryEnabled(entry)) {
        showNotice("MULTIPLAYER NEEDS XBOX LIVE");
        return;
    }
    playCue("audio/sfx/Ui01.wav");
    switch (entry) {
        case static_cast<int>(menu::MainEntry::GameModes):
            enterScene(Scene::Modes);
            break;
        case static_cast<int>(menu::MainEntry::Shop):
            showNotice("THE SHOP IS NOT AVAILABLE IN THIS BUILD");
            break;
        case static_cast<int>(menu::MainEntry::Options):
            enterScene(Scene::Options);
            break;
        default:
            break;
    }
}

void GameApp::handleModesTap(float x, float y) {
    if (menu::backHit(x, y)) {
        playCue("audio/sfx/Ui01.wav");
        enterScene(Scene::MainMenu);
        return;
    }
    const int entry = menu::modeEntryAt(x, y);
    if (entry < 0) return;
    mode_entry_ = entry;
    const int rule = menu::modeGameRule(entry);
    if (rule < 0 && !menu::modeHasOwnFlow(entry)) {
        showNotice(std::string(menu::modeLabel(entry)) + " IS NOT AVAILABLE IN THIS BUILD");
        return;
    }
    playCue("audio/sfx/Ui01.wav");
    // Same numbering as Match::configure (see menu::kRule*), except the modes
    // with their own flow: Road to WrestleMania has no single rule, every card
    // on its ladder carries one.
    mode_ = menu::modeHasOwnFlow(entry) ? menu::kRuleRoadToWrestleMania : rule;
    // Road to WrestleMania runs its own ladder, but it still starts by picking
    // the superstar you take down the road.
    enterScene(Scene::Select);
}

void GameApp::handleOptionsTap(float x, float y) {
    if (menu::backHit(x, y)) {
        playCue("audio/sfx/Ui01.wav");
        enterScene(Scene::MainMenu);
        return;
    }
    // Row 0 toggles the sound, row 1 leaves the screen.
    if (menu::insideRect(x, y, menu::kMainStripX, menu::mainRowY(0), menu::kMainStripWidth,
                         menu::kMainStripHeight)) {
        sound_on_ = !sound_on_;
        if (audio_ != nullptr) audio_->setMuted(!sound_on_);
        playCue("audio/sfx/Ui01.wav");
    } else if (menu::insideRect(x, y, menu::kMainStripX, menu::mainRowY(1),
                                menu::kMainStripWidth, menu::kMainStripHeight)) {
        playCue("audio/sfx/Ui01.wav");
        enterScene(Scene::MainMenu);
    }
}

void GameApp::handleSuperstarTap(float x, float y) {
    const int count = static_cast<int>(data_.wrestlers().size());
    const int pages = pageCount(count, menu::kGridPageSize);
    if (menu::backHit(x, y)) {
        playCue("audio/sfx/Ui01.wav");
        enterScene(Scene::Modes);
        return;
    }
    if (menu::nextArrowHit(x, y)) {
        if (superstar_page_ + 1 < pages) {
            ++superstar_page_;
            superstar_pick_ = -1;
            playCue("audio/sfx/Ui01.wav");
        }
        return;
    }
    if (menu::previousArrowHit(x, y)) {
        if (superstar_page_ > 0) {
            --superstar_page_;
            superstar_pick_ = -1;
            playCue("audio/sfx/Ui01.wav");
        }
        return;
    }
    const int slot = menu::superstarCardAt(x, y);
    if (slot < 0 || confirm_timer_ > 0.0f) return;
    const int index = superstar_page_ * menu::kGridPageSize + slot;
    if (index >= count) return;
    selected_player_ = index;
    // The CPU takes a different man; the mode's extra entrants are picked in
    // startRumble()/startTagMatch() from these two.
    chooseOpponent();
    superstar_pick_ = slot;
    playCue("audio/sfx/Ui01.wav");
    confirm_timer_ = 0.35f;
    confirm_action_ = true;
}

void GameApp::handleArenaTap(float x, float y) {
    const int count = static_cast<int>(data_.arenas().size());
    const int pages = pageCount(count, menu::kArenaPageSize);
    if (menu::backHit(x, y)) {
        playCue("audio/sfx/Ui01.wav");
        enterScene(Scene::Select);
        return;
    }
    if (menu::nextArrowHit(x, y)) {
        if (arena_page_ + 1 < pages) {
            ++arena_page_;
            arena_pick_ = -1;
            playCue("audio/sfx/Ui01.wav");
        }
        return;
    }
    if (menu::previousArrowHit(x, y)) {
        if (arena_page_ > 0) {
            --arena_page_;
            arena_pick_ = -1;
            playCue("audio/sfx/Ui01.wav");
        }
        return;
    }
    const int slot = menu::arenaCardAt(x, y);
    if (slot < 0 || confirm_timer_ > 0.0f) return;
    const int index = arena_page_ * menu::kArenaPageSize + slot;
    if (index >= count) return;
    arena_index_ = index;
    arena_pick_ = slot;
    playCue("audio/sfx/Ui01.wav");
    confirm_timer_ = 0.35f;
    confirm_action_ = true;
}

void GameApp::handleRoadTap(float x, float y) {
    const road::Level *level = road_.current();
    const int count = level != nullptr ? static_cast<int>(level->stages.size()) : 0;
    if (menu::backHit(x, y)) {
        playCue("audio/sfx/Ui01.wav");
        enterScene(Scene::Modes);
        return;
    }
    if (menu::nextArrowHit(x, y)) {
        if (road_cursor_ + 1 < count) {
            ++road_cursor_;
            road_result_.clear();
            playCue("audio/sfx/Ui01.wav");
        }
        return;
    }
    if (menu::previousArrowHit(x, y)) {
        if (road_cursor_ > 0) {
            --road_cursor_;
            road_result_.clear();
            playCue("audio/sfx/Ui01.wav");
        }
        return;
    }
    road_result_.clear();
    if (!menu::roadCardHit(x, y)) return;
    if (road_.complete()) return;  // the road is finished; nothing left to play
    if (road_cursor_ > road_.stage) {
        showNotice("WIN THE EARLIER MATCHES FIRST");
        return;
    }
    playCue("audio/sfx/Ui01.wav");
    startRoadMatch();
}

// Road to WrestleMania: the card under the ladder's cursor decides the rule,
// the field and the arena; everything else is the ordinary match flow.
void GameApp::startRoadMatch() {
    const road::Level *level = road_.current();
    if (level == nullptr) return;
    const WrestlerProfile *player = playerProfile();
    if (player == nullptr || data_.wrestlers().empty()) return;
    const int index = std::clamp(road_cursor_, 0, static_cast<int>(level->stages.size()) - 1);
    const road::Stage &stage = level->stages[static_cast<std::size_t>(index)];

    // The card picks the arena, not the player.
    if (!data_.arenas().empty()) {
        arena_index_ = std::clamp(stage.arena, 0, static_cast<int>(data_.arenas().size()) - 1);
    }
    applyArena();

    const RingGeometry ring;
    match_.configure(&data_, ring, stage.rule,
                     0x20120423u ^ static_cast<std::uint32_t>(clock_ * 1000.0f) ^ 0x9e3779b9u);

    // Distinct men off the card, the player first.
    const int total = static_cast<int>(data_.wrestlers().size());
    const int wanted = std::clamp(stage.wrestlers, 2, 6);
    std::vector<const WrestlerProfile *> card;
    card.push_back(player);
    for (int slot = 1; slot < wanted && static_cast<int>(card.size()) < total; ++slot) {
        for (int step = 0; step < total; ++step) {
            const int pick = (selected_player_ + slot * 7 + step) % total;
            const WrestlerProfile &candidate = data_.wrestlers()[static_cast<std::size_t>(pick)];
            bool taken = false;
            for (const WrestlerProfile *chosen : card) {
                if (chosen->folder == candidate.folder) taken = true;
            }
            if (taken) continue;
            card.push_back(&candidate);
            break;
        }
    }
    if (card.size() < 2) return;

    const int count = static_cast<int>(card.size());
    // Spread the field evenly across the ring, the player on the left.
    for (int i = 0; i < count; ++i) {
        const float t = count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0.5f;
        const float x = ring.centerX() - 180.0f + 360.0f * t;
        const float z = ring.centerZ() + (i % 2 == 0 ? -20.0f : 20.0f);
        // Tag is the only card with sides; everything else is every man for
        // himself, so each man gets his own team.
        const int team = stage.rule == menu::kRuleTagTeam ? (i < 2 ? 0 : 1) : i;
        match_.addWrestler(card[static_cast<std::size_t>(i)], team, x, z, i == 0 ? 1.0f : -1.0f);
    }
    controlled_ = 0;
    match_.setAiControlled(0, false);  // the human
    for (int i = 1; i < match_.wrestlerCount(); ++i) match_.setAiControlled(i, true);
    if (stage.rule == menu::kRuleTagTeam) match_.enableTagTeams(true);
    if (stage.rule == menu::kRuleRoyalRumble) match_.enableStaggeredEntry(25.0f, 2);

    on_road_ = true;
    beginMatchFlow(player, card[1]);
}

void GameApp::returnFromRoadMatch() {
    on_road_ = false;
    const bool won = match_.winnerTeam() == 0;
    if (won) {
        // Only the card at the head of the run moves it on; replaying an
        // earlier one is just a rematch.
        if (road_cursor_ == road_.stage) road_.advance();
        road_result_ = road_.complete() ? "YOU ARE THE CHAMPION" : "YOU WON";
    } else {
        road_result_ = "YOU LOST - TRY AGAIN";
    }
    road_cursor_ = road_.stage;
    enterScene(Scene::Road);
}

void GameApp::chooseOpponent() {
    const int count = static_cast<int>(data_.wrestlers().size());
    if (count <= 1) return;
    // Arcade rule: the CPU fields someone else off the card, picked from the
    // frame clock so a rematch is not always the same pairing.
    const int step = 1 + static_cast<int>(clock_ * 7.0f) % (count - 1);
    selected_opponent_ = (selected_player_ + step) % count;
}

void GameApp::startSelectedMatch() {
    const WrestlerProfile *player = playerProfile();
    const WrestlerProfile *opponent = opponentProfile();
    if (player == nullptr || opponent == nullptr) return;
    // Dispatch on the rule id from menu::modeGameRule, which is also the
    // InGameTable id Match::configure takes - never on a local renumbering.
    switch (mode_) {
        case menu::kRuleTagTeam:
            startTagMatch(player, opponent);
            break;
        case menu::kRuleRoyalRumble:
            startRumble(player, opponent);
            break;
        default:
            startMatch(player, opponent);
            break;
    }
}

void GameApp::updateIntro(float dt) {
    intro_timer_ += dt;
    if (intro_skip_) {
        intro_skip_ = false;
        advanceIntro();
        return;
    }
    if (intro_timer_ < intro_stage_length_) return;
    advanceIntro();
}

// Ring announcer: plays the superstar's call and holds the beat for exactly the
// length the SFX table gives it, so the banner drops with the crowd clip.
float GameApp::announce(const WrestlerProfile *profile) {
    const SfxInfo *cue = profile != nullptr ? profile->introCue() : nullptr;
    if (cue == nullptr) return 1.2f;
    playCue(("audio/sfx/" + cue->file_name).c_str());
    return cue->length > 0.0f ? cue->length : 2.5f;
}

void GameApp::applyArena() {
    if (data_.arenas().empty()) return;
    const int index = std::clamp(arena_index_, 0, static_cast<int>(data_.arenas().size()) - 1);
    renderer_.setArena(data_.arenas()[static_cast<std::size_t>(index)].folder);
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
            case EventType::Tag:
                playCue("audio/sfx/Attack01.wav");
                // Control follows the man the player tagged in.
                if (event.other == controlled_ && event.actor >= 0) {
                    match_.setAiControlled(controlled_, true);
                    controlled_ = event.actor;
                    match_.setAiControlled(controlled_, false);
                    stick_dir_x_ = stick_dir_y_ = 0.0f;
                    red_edge_ = blue_edge_ = false;
                }
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
                // Quit walks back out the way the match came in: the main menu
                // (the original returns to the mode list; either is one tap
                // from a rematch).
                paused_ = false;
                enterScene(Scene::MainMenu);
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
    if (intro_stage_ != IntroStage::Done) {
        // CSkipButton: a tap anywhere cuts the pre-match staging short.
        intro_skip_ = true;
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
        case Scene::Loading:
            menu_.drawLoading(clock_, !error_.empty(), error_);
            break;
        case Scene::Title:
            menu_.drawTitle(clock_);
            break;
        case Scene::MainMenu:
            menu_.drawMainMenu(main_entry_);
            break;
        case Scene::Modes:
            menu_.drawModes(mode_entry_);
            break;
        case Scene::Select: {
            const int pages = pageCount(static_cast<int>(data_.wrestlers().size()),
                                        menu::kGridPageSize);
            menu_.drawSuperstarPage(clampPage(superstar_page_, pages), superstar_pick_, pages);
            break;
        }
        case Scene::Arena: {
            const int pages =
                pageCount(static_cast<int>(data_.arenas().size()), menu::kArenaPageSize);
            menu_.drawArenaPage(clampPage(arena_page_, pages), arena_pick_, pages);
            break;
        }
        case Scene::Road:
            menu_.drawRoad(road_, road_cursor_, road_result_);
            break;
        case Scene::Options:
            drawOptions();
            break;
        case Scene::Match:
            if (intro_stage_ == IntroStage::EntrancePlayer ||
                intro_stage_ == IntroStage::EntranceOpponent) {
                drawEntrance();
            } else {
                renderer_.drawMatch(match_, 0.0f);
                renderer_.drawHud(match_, controlled_);
                if (intro_stage_ != IntroStage::Done) {
                    drawIntroBanner();
                } else {
                    drawTouchControls();
                }
            }
            if (paused_) drawPauseOverlay();
            break;
        case Scene::Result:
            renderer_.drawMatch(match_, 0.0f);
            drawResult();
            break;
    }
    if (notice_timer_ > 0.0f && scene_ != Scene::Match && scene_ != Scene::Result) {
        menu_.drawNotice(notice_, std::min(1.0f, notice_timer_ * 2.0f));
    }
    renderer_.end();
}

// Options: the two switches this port can honour, laid out on the main menu's
// strips so the screen reads like the rest of the front end.
void GameApp::drawOptions() {
    SpriteBatch &batch = renderer_.batch();
    if (renderer_.uiBackground().valid()) {
        batch.draw(renderer_.uiBackground(), 0.0f, 0.0f, menu::kCanvasWidth, menu::kCanvasHeight,
                   Color{});
    }
    renderer_.drawMenuTextCentered(
        "OPTIONS", menu::kCanvasWidth * 0.5f,
        menu::headingInkTop(renderer_.menuFont().metrics()), menu::kHeadingSize, menu::titleRed(),
        menu::kHeadingTracking, menu::kHeadingShadow);

    const char *labels[2] = {"SOUND", "BACK"};
    for (int i = 0; i < 2; ++i) {
        const float y = menu::mainRowY(i);
        const Texture &strip = renderer_.mainButton(i);
        if (strip.valid()) {
            batch.draw(strip, menu::kMainStripX, y, menu::kMainStripWidth, menu::kMainStripHeight,
                       Color{});
        }
        std::string text = labels[i];
        if (i == 0) text += sound_on_ ? ": ON" : ": OFF";
        const FontMetrics &metrics = renderer_.menuFont().metrics();
        renderer_.drawMenuTextCentered(
            text, menu::kCanvasWidth * 0.5f,
            y + menu::centreInBox(metrics, 0.0f, menu::kMainStripHeight, menu::kMainLabelSize),
            menu::kMainLabelSize, menu::normalText(), menu::kLabelTracking, menu::kLabelShadow);
    }
    if (renderer_.backButton().valid()) {
        batch.draw(renderer_.backButton(), menu::kBackX, menu::kBackY, menu::kBackSize,
                   menu::kBackSize, Color{});
    }
}

void GameApp::drawResult() {
    SpriteBatch &batch = renderer_.batch();
    const bool player_won = match_.winnerTeam() == 0;
    const int winner_index = match_.winnerIndex();
    const Wrestler *winner = match_.wrestler(winner_index);
    const Wrestler *controlled = match_.wrestler(controlled_);

    // The shipped report card: frame.png is a mask (the sheet is transparent in
    // its middle), so the interview backdrop goes underneath, then the winner,
    // then the frame on top.
    batch.draw(renderer_.white(), 0.0f, 0.0f, 960.0f, 640.0f, Color{0.22f, 0.23f, 0.26f, 1.0f});
    const float card_x = 63.0f;
    const float card_y = 64.0f;
    const float card_w = 664.0f;
    const float card_h = 439.0f;
    if (renderer_.reportBackground().valid()) {
        batch.draw(renderer_.reportBackground(), card_x, card_y, card_w, card_h, Color{});
    } else {
        batch.draw(renderer_.white(), card_x, card_y, card_w, card_h, Color{0.12f, 0.16f, 0.28f, 1.0f});
    }

    const Color light{1.0f, 0.97f, 0.88f, 1.0f};
    const Color accent = player_won ? Color{1.0f, 0.84f, 0.3f, 1.0f}
                                    : Color{1.0f, 0.45f, 0.4f, 1.0f};
    renderer_.drawTextCentered("MATCH RESULT", 395.0f, 84.0f, 0.85f, Color{0.9f, 0.94f, 1.0f, 1.0f});

    if (winner != nullptr && winner->profile() != nullptr) {
        const WrestlerProfile &profile = *winner->profile();
        const int sst = profile.sprite_index.count("idle.sst") ? profile.sprite_index.at("idle.sst")
                                                              : -1;
        renderer_.drawActorPortrait(profile, sst, 395.0f, 420.0f, 1.0f, 0.8f);
        renderer_.drawTextCentered(profile.name_line1, 395.0f, 436.0f, 0.85f, light);
        if (!profile.name_line2.empty()) {
            renderer_.drawTextCentered(profile.name_line2, 395.0f, 464.0f, 0.55f, light);
        }
    }
    renderer_.drawTextCentered(player_won ? "WINNER" : "DEFEATED", 395.0f, 478.0f, 0.65f, accent);

    // The little panel on the frame carries the numbers.
    const Color ink{0.1f, 0.11f, 0.13f, 1.0f};
    char line[64];
    const int total_seconds = static_cast<int>(match_.clock());
    std::snprintf(line, sizeof(line), "TIME %d:%02d", total_seconds / 60, total_seconds % 60);
    renderer_.drawTextCentered(line, 830.0f, 508.0f, 0.4f, ink);
    const int score = controlled != nullptr ? controlled->score : 0;
    std::snprintf(line, sizeof(line), "SCORE %d", score);
    renderer_.drawTextCentered(line, 830.0f, 534.0f, 0.4f, ink);
    renderer_.drawTextCentered("tap to continue", 830.0f, 562.0f, 0.36f, ink);

    if (renderer_.reportFrame().valid()) {
        batch.draw(renderer_.reportFrame(), 0.0f, 0.0f, 960.0f, 640.0f, Color{});
    }
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
    row("QUIT TO MAIN MENU", kQuitY);
    renderer_.drawTextCentered("RESUME or the pause button to keep playing", 480.0f, 560.0f, 0.4f,
                               Color{0.12f, 0.15f, 0.2f, 1.0f});
}

// CGameEntrance::onDraw: the stage, the crowd behind it, the men walking out
// on it, and the name plate along the bottom.
void GameApp::drawEntrance() {
    const EntranceStage &stage = entranceStage();
    renderer_.drawEntranceBackground(stage);
    // The crowd stands in front of the stage and fills the bottom of the shot.
    renderer_.drawCrowd(stage, 1.0f / 60.0f);

    // Fade up from black over the first half second of the show.
    if (intro_timer_ < 0.5f) {
        renderer_.batch().draw(renderer_.white(), 0.0f, 0.0f, 960.0f, 640.0f,
                               Color{0.0f, 0.0f, 0.0f, 1.0f - intro_timer_ / 0.5f});
    }

    if (entrance_show_ >= entrance_shows_.size()) return;
    const EntranceShow &show = entrance_shows_[entrance_show_];
    if (show.first < 0) return;
    const bool pair = show.second >= 0;
    const float feet_y = stage.toScreenY(stage.start_z);
    // SetupWrestler: the man himself stands at the show's X and his partner
    // one gap away, both on the same depth line.
    const float main_x = stage.toScreenX(pair ? stage.start_x - stage.gap : stage.start_x);
    drawEntranceWrestler(show.first, main_x, feet_y, stage.actor_scale);
    if (pair) {
        drawEntranceWrestler(show.second, stage.toScreenX(stage.start_x + stage.gap), feet_y,
                             stage.actor_scale);
    }

    // The name plate along the bottom: the man's name, his partner's above it,
    // the double rule and the WWE mark at the end of it.
    const Wrestler *main = match_.wrestler(show.first);
    const Wrestler *other = pair ? match_.wrestler(show.second) : nullptr;
    renderer_.drawEntrancePlate(main != nullptr && main->profile() != nullptr
                                    ? main->profile()->name
                                    : std::string(),
                                other != nullptr && other->profile() != nullptr
                                    ? other->profile()->name
                                    : std::string());

    if (intro_timer_ > 1.0f) {
        renderer_.drawTextCentered("TAP TO SKIP", 880.0f, 470.0f, 0.34f,
                                   Color{1.0f, 1.0f, 1.0f, 0.35f});
    }
}

void GameApp::drawEntranceWrestler(int index, float feet_x, float feet_y, float scale) {
    const Wrestler *w = match_.wrestler(index);
    if (w == nullptr || w->profile() == nullptr) return;
    // He plays his own entrance animation (EntranceInfo.AnimList) while he is
    // out here; the hard camera looks straight at him, so he is never mirrored.
    renderer_.drawEntranceActor(*w->profile(), w->spriteId(), feet_x, feet_y, 1.0f, 1.0f, scale);
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
        case IntroStage::EntrancePlayer:
        case IntroStage::EntranceOpponent:
            // The banner is drawn by drawEntrance(); it has the name plate.
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
