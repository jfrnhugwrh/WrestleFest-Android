#ifndef WRESTLEFEST_DATA_GAME_DATA_H
#define WRESTLEFEST_DATA_GAME_DATA_H

// Typed view over the shipped .sml tables.
//
// Every struct below mirrors one row of one file; the field names are the SML
// field names so the mapping can be checked line by line against the data. All
// values that drive combat come from here - the simulation never hard-codes a
// damage number, a collision radius or a state id (see GAMEPLAY_IMPLEMENTATION
// sections 3-5).

#include "asset_source.h"
#include "sml_parser.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace wrestlefest {

// ---------------------------------------------------------------------------
// StateTable.sml - the 117 wrestler states.
// ---------------------------------------------------------------------------
struct StateInfo {
    int id = 0;
    std::string name;
    int anim_id = 0;            // -1 for USE_*_WS: the move table supplies the anim
    int opponent_state = 0xff;  // 0xff = no paired state
    float collision_range_x = -1.0f;  // negative = the state does not collide
    bool check_rope_hit = false;
};

// Well-known state ids (StateTable.sml). Named so the sim reads like the original.
namespace state {
enum : int {
    IDLE = 1,
    WALK = 2,
    RUN = 3,
    PUNCH = 4,
    KICK = 5,
    GRAPPLE_IDLE = 6,
    GRAPPLE_HIGH = 7,
    GRAPPLE_LOW = 8,
    HEADLOCK = 9,
    HEADLOCKED = 10,
    HEADLOCK_WAIT = 13,
    SLAMMED = 14,
    ONGROUND = 15,
    HITREACT_MID = 16,
    HITREACT_HIGH = 17,
    CLIMBTOP = 18,
    CLIMBOUT = 20,
    CLIMBIN = 21,
    JUMP_FOR_PIN = 22,
    PIN = 23,
    ROPEFLING = 24,
    IRISH_WHIP = 25,
    IRISH_WHIPPED = 26,
    AIRBORNE = 27,
    PICKUP = 29,
    PICKUPED = 30,
    PUSHOFF = 31,
    STANDUP = 32,
    RESTRAIN = 36,
    RESTRAINED = 37,
    BREAK_SLEEPHOLD = 38,
    BROKEN_SLEEPHOLD = 39,
    BREAK_RESTRAIN = 40,
    BROKEN_RESTRAIN = 41,
    TAUNT = 42,
    STAND_TIRED = 43,
    DOGFIGHT_STANDS = 44,
    DOGFIGHT_HIGHER = 45,
    DOGFIGHT_LOWER = 46,
    DOGFIGHT_PUNCH = 47,
    DOGFIGHT_PUNCHED = 48,
    KNOCKED_BACK = 49,
    USE_CL_WS = 51,
    USE_CL_BEGIN = 52,
    USE_CL_HANG = 53,
    USE_CL_DROP = 54,
    BE_USED_CL_HANG = 55,
    BE_USED_CL_DROP = 56,
    USE_RAM_WS = 57,
    BE_USED_RAM_WS = 58,
    USE_F_WS_BEGIN = 59,
    USE_F_WS = 60,
    USE_F_WS_PIN = 61,
    BE_USED_F_WS = 62,
    BE_USED_F_WS_PIN = 63,
    USE_S_WS = 64,
    BE_USED_S_WS = 65,
    USE_HT_WS = 66,
    USE_SB_WS = 70,
    USE_TB_WS = 74,
    USE_TA_WS = 77,
    LO_TH_PRESS = 83,      // bear-hug lift (low-tech press family)
    LO_TH_PRESSED = 84,    // victim of the bear-hug lift
    PINNING = 85,
    BE_PINNED = 86,
    ONGROUND_HURT = 87,
    CLIMB_DOWN_F_TO = 88,
    TAG_IN = 90,
    TAG_OUT = 91,
    W_F_M_RINGSIDE = 92,
    RINGSIDE_GRAB = 93,
    USE_WEAPON = 100,
    BOUNCE_OFF_ROPE = 102,
    LOSE = 103,
    VICTORY = 104,
    ENTRANCE = 105,
    USE_RFWS_BEGIN = 110,
    USE_RFWS_RUSH = 111,
    USE_RFWS = 113,
    BE_USED_RFWS = 114,
};
constexpr int kNoState = 0xff;
constexpr int kStateCount = 117;
}  // namespace state

// Wrestler region state (actor +0x6c). Only these five are referenced by the
// engine; 3/5/7/8/9 stay reserved rather than guessed at.
namespace region {
enum : int {
    IN_RING = 0,
    RINGSIDE = 1,
    RINGSIDE_TRANSITION = 2,
    OUTSIDE = 4,
    IN_RING_ALONE = 6,
    KNOCKED_OUT = 10,
};
}  // namespace region

// ---------------------------------------------------------------------------
// AnimationTable/WrestlerAT.sml - 269 animations shared by every wrestler.
// ---------------------------------------------------------------------------
struct AnimInfo {
    int id = 0;
    std::string name;
    int frame_count = 1;
    std::array<int, 24> frames{};  // animation frame -> sprime (sst) index
    float x_offset = 0.0f;         // applied to the opponent on entry
    int opposing_anim = -1;
    float facing = 1.0f;           // multiplies the actor's facing on entry
    float opposing_facing = -1.0f;
    int bottom_layer = 0;
    int upper_layer = 0;
    float hurt_timing = -1.0f;     // >= 0 only on the 18 signature moves

    bool hasHurtTiming() const { return hurt_timing >= 0.0f; }
};

// ---------------------------------------------------------------------------
// WrestleStyleList.sml - the 95 moves.
// ---------------------------------------------------------------------------
struct MoveInfo {
    int id = 0;
    std::string name;
    int state_id = 0;        // authoring hint: the status slot the move belongs to
    int type_id = 0;         // move family (0..13) -> entry state, see moveFamilyEntryState
    int sub_type_id = 0;     // index into the family's own table
    int energy_damage = 0;
    int motion_damage = 0;   // signed: negative override is legal
    int rare = 0;
    int attack_position = 0;  // 0 standing, 1 pickup on exit, 2 aerial, 3 ground
    int entry_state = state::IDLE;  // TypeID resolved through kMoveFamilyToEntryState
};

// _stateMaping: TypeID -> StateTable state (see GAMEPLAY_IMPLEMENTATION 3.2).
// TODO(verify): families 1 and 12 are ambiguous between USE_RAM_WS and USE_CL_WS.
int moveFamilyEntryState(int type_id);

// ---------------------------------------------------------------------------
// WSTable/*.sml - the per-family tables the USE_* states read.
// ---------------------------------------------------------------------------
struct ForceMove {  // ForceWSTable.sml - the throws (family 2)
    int id = 0;
    std::string name;
    int need_begin = 0;
    float show_percentage = 0.0f;
    float boost_percentage = 0.0f;
    int show_anim_id = -1;
    int anim_id = -1;
    int opponent_anim_id = -1;
    bool adjust_opponent = false;
    int jump_id = -1;
    int on_exit_self_state = 1;
    float on_exit_self_x_offset = 0.0f;
    int on_exit_opponent_state = state::IDLE;
    float on_exit_opponent_x_offset = 0.0f;
    bool opponent_reverse = false;
    float hurt_opponent_timing = -1.0f;
};

struct RamMove {  // RamWSTable.sml
    int id = 0;
    int anim_id = -1;
    int opponent_rammed_state = state::HITREACT_MID;
    bool is_running = false;
};

struct ClotheslineMove {  // ClotheslineWSTable.sml
    int id = 0;
    int begin_anim_id = -1;
    int hang_anim_id = -1;
    int drop_anim_id = -1;
    float jump_y = 0.0f;
    int slow_down_timing = 0;
    int stop_timing = 0;
    bool can_bounce_rope = false;
};

struct SubmissionMove {  // SubmissionWSTable.sml
    int id = 0;
    std::string name;
    int anim_id = -1;
    int opponent_anim_id = -1;
    int one_shot_frame = -1;
    int pain_frame = 0;
    int repeat_frame = 0;
    bool is_give_up = false;
    int break_state = state::PUSHOFF;
    int be_broken_state = -1;
    int be_rescued_state = -1;
    bool have_struggle_sfx = false;
    float one_shot_damage = 0.0f;
    float sec_per_hurt = 0.2f;
};

struct HitTargetMove {  // HitTargetWSTable.sml
    int id = 0;
    int anim_id = -1;
    bool is_force_finish = false;
    bool is_boost = false;
    int show_anim_id = -1;
    int hit_target_anim_id = -1;
    float jump_height = 0.0f;
    int success_state = state::IDLE;
    int fail_state = state::IDLE;
    bool camera_shake = false;
};

struct TurnbuckleMove {  // TurnbuckleWSTable.sml
    int id = 0;
    int anim_id = -1;
    int hit_target_anim_id = -1;
    bool need_take_off = false;
    bool on_ground_attack = false;
    float jump_height = 0.0f;
    bool need_face = false;
    float attack_timing = 0.0f;
    int success_state = state::IDLE;
    int fail_state = state::IDLE;
};

struct SpringboardMove {  // SpringboardWSTable.sml
    int id = 0;
    int anim_id = -1;
    int end_anim_id = -1;
    bool on_ground_attack = false;
    bool reverse = false;
    bool need_face = false;
    float jump_height = 0.0f;
    int success_state = state::IDLE;
    int fail_state = state::IDLE;
    int opponent_hurt_state = state::HITREACT_MID;
};

struct TagAttackMove {  // TagAttackWSTable.sml
    int id = 0;
    int turnbuckle_ws_id = -1;
};

struct RushForceMove {  // RushForceWSTable.sml
    int id = 0;
    int begin_anim_id = -1;
    int run_anim_id = -1;
    int end_anim_id = -1;
    int act_anim_id = -1;
    int on_exit_state_for_end = state::IDLE;
    float jump_height = 0.0f;
    int opponent_anim_id = -1;
    bool adjust_opponent = false;
    int on_exit_self_state = state::IDLE;
    float on_exit_self_x_offset = 0.0f;
    int on_exit_opponent_state = state::IDLE;
    float on_exit_opponent_x_offset = 0.0f;
    bool opponent_reverse = false;
    float hurt_opponent_timing = -1.0f;
};

struct ForceJump {  // ForceWSJumpTable.sml
    int id = 0;
    float jump_percentage = 0.0f;
    int start_frame = 0;
    int frame_count = 0;
    float total_time = 0.0f;
    float jump_height = 0.0f;
    float x_factor = 0.0f;
    float extra_energy_damage = 0.0f;
    float extra_motion_damage = 0.0f;
};

struct AirborneInfo {  // AirborneTable.sml
    int id = 0;
    int opponent_state = 0xff;
    int opponent_use_ws = -1;
    bool can_fly_out = false;
    float start_y = 0.0f;
    float fly_height = 0.0f;
};

struct FallAwayInfo {  // FallAwayTable.sml
    int id = 0;
    int opponent_use_ws = -1;
    int anim_id = -1;
    float start_y = 0.0f;
    float delta_z = 0.0f;
    bool need_reverse = false;
};

// ---------------------------------------------------------------------------
// Per-wrestler tables.
// ---------------------------------------------------------------------------
struct WrestlerBaseInfo {  // Wrestler/<name>/BaseInfo.sml
    float energy = 100.0f;
    float motion_power = 100.0f;
    float max_power_up_value = 100.0f;
    float heal_eps = 1.4f;
    float heal_mps = 5.0f;
    float heal_pups = 2.0f;
    float walk_factor_x = 315.0f;
    float walk_factor_z = 270.0f;
    float run_factor_x = 600.0f;
    float speed_down_factor_tired = 0.3f;
    float speed_down_factor_weapon = 0.6f;
    float max_headlocked_time = 2.0f;
    float max_on_ground_time_min = 0.4f;
    float max_on_ground_time_max = 8.0f;
    float panting_value = 20.0f;
    float tired_value = 30.0f;
    float gbh_value = 24.0f;
    float power_ws_value = 50.0f;
    float bounce_off_rope_timing = 150.0f;
    float free_from_headlock_timing = 0.2f;
    float max_power_up_duration = 20.0f;
    float max_power_up_interval = 40.0f;
    float power_up_bonus = 10.0f;
};

// One of the 9 status slots (0 running attack .. 8 tag).
struct StatusSlot {
    int status_id = 0;
    int list_count = 0;
    std::array<int, 4> attack{{-99, -99, -99, -99}};
    std::array<int, 4> e_damage{{0, 0, 0, 0}};
    std::array<int, 4> m_damage{{0, 0, 0, 0}};
    std::array<int, 4> score{{0, 0, 0, 0}};
};

struct SpriteEntry {  // wrestlerData.sml - SpriteOffset rows
    std::string name;   // "idle.sst"
    float x_offset = 0.0f;
    float y_offset = 0.0f;
};

// SFXTable.sml - the per-wrestler voice cues (the ring announcer's name call).
struct SfxInfo {
    int id = 0;
    std::string title;      // "Intro", "Tag Intro"
    std::string file_name;  // "RA_JR_SSN_BIGSHOW_0.wav"
    float volume = 1.0f;
    float length = 0.0f;    // seconds
};

// EntranceInfo.sml - the entrance walk that plays before the bell.
struct EntranceInfo {
    float walk_factor = 24.0f;
    float run_factor = 150.0f;
    std::vector<int> anims;  // WrestlerAT ids played in order
};

struct WrestlerProfile {
    int id = 0;
    std::string name;
    std::string name_line1;
    std::string name_line2;
    std::string folder;
    int type = 0;  // AllSuperStarTable "Type" (0 = legend, 1 = superstar)
    WrestlerBaseInfo base;
    std::array<StatusSlot, 9> status{};
    std::vector<SpriteEntry> sprites;                 // indexed by sst id
    std::unordered_map<std::string, int> sprite_index;  // name -> id
    std::vector<SfxInfo> sfx;                         // SFXTable.sml
    EntranceInfo entrance;                            // EntranceInfo.sml

    // The ring announcer's call for this wrestler, e.g.
    // "audio/sfx/RA_JR_SSN_JOHNCENA_0.wav". Empty when the table has no row.
    const SfxInfo *introCue() const {
        for (const SfxInfo &cue : sfx) {
            if (cue.title == "Intro" && !cue.file_name.empty()) return &cue;
        }
        return sfx.empty() ? nullptr : &sfx.front();
    }

    const SpriteEntry *spriteFor(int sst_id) const {
        if (sst_id < 0 || sst_id >= static_cast<int>(sprites.size())) return nullptr;
        return &sprites[static_cast<std::size_t>(sst_id)];
    }
    // A stable 1..11 "strength" rating used by the AI reaction beats. The
    // original loads it from a byte we could not map; deriving it from max
    // energy keeps the ordering (big men react slower) and is documented in
    // GAMEPLAY_IMPLEMENTATION 11.4.
    int strength() const {
        const int s = static_cast<int>(base.energy / 100.0f * 10.0f);
        return s < 1 ? 1 : (s > 11 ? 11 : s);
    }
};

// ---------------------------------------------------------------------------
// Match rules and shared tuning tables.
// ---------------------------------------------------------------------------
struct RuleInfo {  // InGameTable.sml
    int id = 0;
    std::string name;
    float max_match_time = 360.0f;
    float max_outside_time = 30.0f;
    int max_outside_count = 20;
    int max_referee_count = 3;
    float toss_out_percentage = 100.0f;
    int toss_out_dynamic_by = 0;
    bool can_power_up = true;
    bool can_pin = true;
    bool can_submission = true;
    bool have_referee = true;
    bool heal_by_free_pin = true;
    bool heal_by_kick_out = true;
};

struct CommonVariables {  // CommonVariableInfo.sml
    float dogfight_percentage = 75.0f;
    float win_grapple_percentage = 50.0f;
    float tag_area_width = 80.0f;
    float tag_area_height = 80.0f;
    float airborne_factor_x = 550.0f;
    float use_toss_out_area_width = 50.0f;
    float collision_range_z = 40.0f;
    float collision_feet_factor_x = 80.0f;
    float collision_head_factor_x = 60.0f;
    float slammed_height = 100.0f;
    float turnbuckle_height = 180.0f;
    float stage_offset_height = 25.0f;
    float outside_back_to_in_ring_width = 300.0f;
    float kick_out_lose_time = 0.75f;
    float kick_out_jump_time = 0.5f;
    float kick_out_jump_factor_x = 400.0f;
    float kick_out_jump_height = 180.0f;
    float kick_out_disappear_time = 1.0f;
    float max_suffocation_time = 1.0f;
    float heal_energy_by_free_pin = 30.0f;
    float heal_energy_by_kick_out_opponent = 15.0f;
    float add_counter_per_no_energy_pin = 5.0f;
};

struct CommonDamage {  // CommonDamageInfo.sml - every entry is (energy, motion)
    float ws_fail_energy = 2.0f, ws_fail_motion = 0.0f;
    float hit_rail_energy = 4.0f, hit_rail_motion = 0.0f;
    float run_ram_energy = 2.0f, run_ram_motion = 0.0f;
    float be_run_rammed_energy = 5.0f, be_run_rammed_motion = 0.0f;
    float be_pushed_off_energy = 3.0f, be_pushed_off_motion = 0.0f;
    float be_broken_sleep_hold_energy = 3.0f, be_broken_sleep_hold_motion = 0.0f;
    float be_broken_restrain_energy = 3.0f, be_broken_restrain_motion = 0.0f;
};

struct ButtonCountRow {  // ButtonCountTable.sml
    int id = 0;
    int name = 0;  // unused, kept for alignment with the data
    std::string label;
    int state_table_id = 0;
    int ws_table_id = -1;
    std::array<int, 12> goals{};  // energy bands, see goalForEnergy()
};

struct ArenaInfo {  // Contents/General/AllArenaTable.sml
    int id = 0;
    std::string name;
    std::string folder;
    int entrance_scene_id = 0;
};

// ---------------------------------------------------------------------------
// GameData - owns everything above.
// ---------------------------------------------------------------------------
class GameData {
public:
    // Loads every table the simulation needs. `data_root` is the directory that
    // contains StateTable.sml (i.e. "data/Data").
    bool load(AssetSource *source, const std::string &data_root, std::string *error);

    const StateInfo *stateInfo(int id) const;
    const AnimInfo *animInfo(int id) const;
    const MoveInfo *moveInfo(int id) const;
    const ForceMove *forceMove(int id) const;
    const RamMove *ramMove(int id) const;
    const ClotheslineMove *clotheslineMove(int id) const;
    const SubmissionMove *submissionMove(int id) const;
    const HitTargetMove *hitTargetMove(int id) const;
    const TurnbuckleMove *turnbuckleMove(int id) const;
    const SpringboardMove *springboardMove(int id) const;
    const RushForceMove *rushForceMove(int id) const;
    const ForceJump *forceJump(int id) const;
    const AirborneInfo *airborneInfo(int id) const;
    const FallAwayInfo *fallAwayInfo(int id) const;
    const RuleInfo *rule(int id) const;
    const WrestlerProfile *wrestler(int id) const;
    const WrestlerProfile *wrestlerByFolder(const std::string &folder) const;

    const std::vector<StateInfo> &states() const { return states_; }
    const std::vector<AnimInfo> &anims() const { return anims_; }
    const std::vector<MoveInfo> &moves() const { return moves_; }
    const std::vector<WrestlerProfile> &wrestlers() const { return wrestlers_; }
    const std::vector<RuleInfo> &rules() const { return rules_; }
    const std::vector<ArenaInfo> &arenas() const { return arenas_; }

    const CommonVariables &common() const { return common_; }
    const CommonDamage &damage() const { return damage_; }

    // Mash goal for `state_id` at `energy`, including the per-failed-pin bonus
    // (GetButtonCountGoal, GAMEPLAY_IMPLEMENTATION 3.8). Returns 0 when the
    // state has no mash row.
    int buttonCountGoal(int state_id, float energy, int pin_attempts) const;

    // Number of tables that failed to load, for diagnostics/tests.
    const std::vector<std::string> &warnings() const { return warnings_; }

private:
    bool loadOne(AssetSource *source, const std::string &path, SmlDocument *doc, std::string *error);
    bool loadCommonTables(AssetSource *source, const std::string &root, std::string *error);
    bool loadWrestlers(AssetSource *source, const std::string &root, std::string *error);

    std::vector<StateInfo> states_;
    std::vector<AnimInfo> anims_;
    std::vector<MoveInfo> moves_;
    std::vector<ForceMove> force_moves_;
    std::vector<RamMove> ram_moves_;
    std::vector<ClotheslineMove> clothesline_moves_;
    std::vector<SubmissionMove> submission_moves_;
    std::vector<HitTargetMove> hit_target_moves_;
    std::vector<TurnbuckleMove> turnbuckle_moves_;
    std::vector<SpringboardMove> springboard_moves_;
    std::vector<TagAttackMove> tag_attack_moves_;
    std::vector<RushForceMove> rush_force_moves_;
    std::vector<ForceJump> force_jumps_;
    std::vector<AirborneInfo> airborne_;
    std::vector<FallAwayInfo> fall_away_;
    std::vector<ButtonCountRow> button_counts_;
    std::vector<RuleInfo> rules_;
    std::vector<WrestlerProfile> wrestlers_;
    std::vector<ArenaInfo> arenas_;
    CommonVariables common_;
    CommonDamage damage_;
    std::vector<std::string> warnings_;
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_DATA_GAME_DATA_H
