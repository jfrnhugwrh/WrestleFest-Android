#include "game_data.h"

#include <algorithm>
#include <cstring>

namespace wrestlefest {
namespace {

// The shipped roster folders (Contents/**/AllSuperStarTable*.sml is incomplete:
// 11 of the 39 folders only exist on disk, so the list is authored here).
// The order is the select screen's order: SuperstarScene fills its grid column
// by column (slot n is column n / 2, row n % 2), and the reference page 1 shows
// Jake Roberts over Randy Orton, John Cena over Randy Savage, Rey Mysterio over
// Stone Cold and The Rock over The Undertaker - a legend paired with a modern
// superstar in each column.
const char *const kRosterFolders[] = {
    "00_Jake the Snake", "00_Randy Orton", "00_John Cena", "00_Randy Savage",
    "00_Rey Mysterio", "00_Stone Cold", "00_The Rock", "00_Undertaker",
    "01_Big_Bossman", "01_CM punk", "01_Shawn_Michaels", "01_Sheamus", "01_TripleH",
    "02_Alberto Del Rio", "02_Sgt. Slaughter", "02_The Miz", "02_Yokozuna", "02_Zack Ryder",
    "03_Animal", "03_BigShow", "03_Christian", "03_Hawk", "03_R-truth",
    "04_DanielBryan", "04_Eddie Guerrero", "04_Edge", "04_Mark Henry", "04_MrPerfect",
    "05_Cody Rhodes", "05_Kane", "05_Million Dollar Man", "05_RowdyRoddyPiper", "05_Sin Cara",
    "06_Bartista", "06_Dolph Ziggler", "06_DustyRhodes", "06_KevinNash", "06_Kofi Kingston",
};

std::string folderToDisplayName(const std::string &folder) {
    std::string name = folder;
    // Strip the "NN_" ordering prefix.
    const std::size_t underscore = name.find('_');
    if (underscore != std::string::npos) name = name.substr(underscore + 1);
    // "Kofi Kingston" style folders already read as names; "Big_Bossman" does not.
    for (char &c : name) {
        if (c == '_') c = ' ';
    }
    return name;
}

template <typename T>
const T *findById(const std::vector<T> &rows, int id) {
    if (id < 0) return nullptr;
    for (const T &row : rows) {
        if (row.id == id) return &row;
    }
    return nullptr;
}

}  // namespace

int moveFamilyEntryState(int type_id) {
    // GAMEPLAY_IMPLEMENTATION 3.2 - the reconstructed _stateMaping table.
    switch (type_id) {
        case 0: return state::USE_CL_WS;     // clothesline family
        case 1: return state::USE_RAM_WS;    // TODO(verify): ram vs clothesline
        case 2: return state::USE_F_WS;      // throws (code tests 0x3c)
        case 3: return state::USE_S_WS;      // submissions (code tests 0x40)
        case 4: return state::USE_HT_WS;     // ground strikes
        case 5: return state::USE_SB_WS;     // springboard
        case 6: return state::USE_TB_WS;     // turnbuckle dives
        case 7: return state::RESTRAIN;      // WS_RESTRAIN
        case 8: return state::USE_RFWS_RUSH;  // Lou Thesz press
        case 9: return state::PUNCH;
        case 10: return state::KICK;
        case 11: return state::USE_TA_WS;    // tag attack
        case 12: return state::USE_RAM_WS;   // TODO(verify): spear family
        case 13: return state::USE_WEAPON;
        default: return state::IDLE;
    }
}

bool GameData::loadOne(AssetSource *source, const std::string &path, SmlDocument *doc,
                       std::string *error) {
    std::vector<std::uint8_t> bytes;
    if (!source->read(path, &bytes)) {
        warnings_.push_back("missing asset: " + path);
        if (error != nullptr) *error = "missing asset: " + path;
        return false;
    }
    std::string parse_error;
    if (!doc->parse(reinterpret_cast<const char *>(bytes.data()), bytes.size(), &parse_error)) {
        warnings_.push_back("parse failed: " + path + " (" + parse_error + ")");
        if (error != nullptr) *error = "parse failed: " + path + " (" + parse_error + ")";
        return false;
    }
    return true;
}

bool GameData::loadCommonTables(AssetSource *source, const std::string &root, std::string *error) {
    SmlDocument doc;

    // StateTable.sml -> 117 states.
    if (loadOne(source, root + "/StateTable.sml", &doc, error)) {
        states_.clear();
        states_.reserve(doc.records.size());
        for (const SmlRecord &record : doc.records) {
            StateInfo info;
            info.id = record.integer("Id");
            info.name = record.string("Name");
            info.anim_id = record.integer("AnimId", 0);
            info.opponent_state = record.integer("OpponentState", 0xff);
            info.collision_range_x = static_cast<float>(record.number("CollisionRangeX", -1.0));
            info.check_rope_hit = record.integer("CheckRopeHit") != 0;
            states_.push_back(std::move(info));
        }
    }

    // AnimationTable/WrestlerAT.sml -> 269 animations.
    if (loadOne(source, root + "/AnimationTable/WrestlerAT.sml", &doc, error)) {
        anims_.clear();
        anims_.reserve(doc.records.size());
        for (const SmlRecord &record : doc.records) {
            AnimInfo info;
            info.id = record.integer("ID");
            info.name = record.string("AnimName");
            info.frame_count = record.integer("FrameCount", 1);
            for (int i = 0; i < 24; ++i) {
                info.frames[static_cast<std::size_t>(i)] =
                    static_cast<int>(record.numberAt("FrameArray", i, -1.0));
            }
            info.x_offset = static_cast<float>(record.number("xOffset", 0.0));
            info.opposing_anim = record.integer("OpposingAnim", -1);
            info.facing = static_cast<float>(record.number("Facing", 1.0));
            info.opposing_facing = static_cast<float>(record.number("OpposingFacing", -1.0));
            info.bottom_layer = record.integer("BottomLayerPrority", 0);
            info.upper_layer = record.integer("UpperLayerPrority", 0);
            info.hurt_timing = static_cast<float>(record.number("HurtTimming", -1.0));
            anims_.push_back(std::move(info));
        }
    }

    // WrestleStyleList.sml -> 95 moves, with TypeID resolved to an entry state.
    if (loadOne(source, root + "/WSTable/WrestleStyleList.sml", &doc, error)) {
        moves_.clear();
        moves_.reserve(doc.records.size());
        for (const SmlRecord &record : doc.records) {
            MoveInfo info;
            info.id = record.integer("ID");
            info.name = record.string("MoveName");
            info.state_id = record.integer("StateID");
            info.type_id = record.integer("TypeID");
            info.sub_type_id = record.integer("SubTypeID");
            info.energy_damage = record.integer("EnergyDamage");
            info.motion_damage = record.integer("MotionPowerDamage");
            info.rare = record.integer("Rare");
            info.attack_position = record.integer("AttackPosition");
            info.entry_state = moveFamilyEntryState(info.type_id);
            moves_.push_back(std::move(info));
        }
    }

    // The family tables read by the USE_* states.
    if (loadOne(source, root + "/WSTable/ForceWSTable.sml", &doc, error)) {
        force_moves_.clear();
        for (const SmlRecord &record : doc.records) {
            ForceMove move;
            move.id = record.integer("ID");
            move.name = record.string("MoveName");
            move.need_begin = record.integer("NeedBegin");
            move.show_percentage = static_cast<float>(record.number("ShowPercentage"));
            move.boost_percentage = static_cast<float>(record.number("BoostPercentage"));
            move.show_anim_id = record.integer("ShowAnimID", -1);
            move.anim_id = record.integer("AnimID", -1);
            move.opponent_anim_id = record.integer("OpppnentAnimID", -1);
            move.adjust_opponent = record.integer("IsForceAdjustOpponent") != 0;
            move.jump_id = record.integer("JumpID", -1);
            move.on_exit_self_state = record.integer("OnExitSelfState", state::IDLE);
            move.on_exit_self_x_offset = static_cast<float>(record.number("OnExitSelfXOffset"));
            move.on_exit_opponent_state = record.integer("OnExitOpponentState", state::IDLE);
            move.on_exit_opponent_x_offset = static_cast<float>(record.number("OnExitOpponentXOffset"));
            move.opponent_reverse = record.integer("IsOpponentReverse") != 0;
            move.hurt_opponent_timing = static_cast<float>(record.number("HurtOpponentTimming", -1.0));
            force_moves_.push_back(std::move(move));
        }
    }
    if (loadOne(source, root + "/WSTable/RamWSTable.sml", &doc, error)) {
        ram_moves_.clear();
        for (const SmlRecord &record : doc.records) {
            RamMove move;
            move.id = record.integer("ID");
            move.anim_id = record.integer("AnimID", -1);
            move.opponent_rammed_state = record.integer("OpponentRammedState", state::HITREACT_MID);
            move.is_running = record.integer("IsRunning") != 0;
            ram_moves_.push_back(move);
        }
    }
    if (loadOne(source, root + "/WSTable/ClotheslineWSTable.sml", &doc, error)) {
        clothesline_moves_.clear();
        for (const SmlRecord &record : doc.records) {
            ClotheslineMove move;
            move.id = record.integer("ID");
            move.begin_anim_id = record.integer("BeginAnimID", -1);
            move.hang_anim_id = record.integer("HangAnimID", -1);
            move.drop_anim_id = record.integer("DropAnimID", -1);
            move.jump_y = static_cast<float>(record.number("JumpY"));
            move.slow_down_timing = record.integer("SlowDownTimming");
            move.stop_timing = record.integer("StopTimming");
            move.can_bounce_rope = record.integer("CanBounceRope") != 0;
            clothesline_moves_.push_back(move);
        }
    }
    if (loadOne(source, root + "/WSTable/SubmissionWSTable.sml", &doc, error)) {
        submission_moves_.clear();
        for (const SmlRecord &record : doc.records) {
            SubmissionMove move;
            move.id = record.integer("ID");
            move.name = record.string("MoveName");
            move.anim_id = record.integer("AnimID", -1);
            move.opponent_anim_id = record.integer("OppnentAnimID", -1);
            move.one_shot_frame = record.integer("OneShotFrame", -1);
            move.pain_frame = record.integer("PainFrame");
            move.repeat_frame = record.integer("RepeatFrame");
            move.is_give_up = record.integer("IsGiveUpWS") != 0;
            move.break_state = record.integer("BreakSWSState", state::PUSHOFF);
            move.be_broken_state = record.integer("BeBrokenSWSState", -1);
            move.be_rescued_state = record.integer("BeRescuedState", -1);
            move.have_struggle_sfx = record.integer("HaveStruggleSFX") != 0;
            move.one_shot_damage = static_cast<float>(record.number("OneShotDamage"));
            move.sec_per_hurt = static_cast<float>(record.number("SecPerHurt", 0.2));
            submission_moves_.push_back(std::move(move));
        }
    }
    if (loadOne(source, root + "/WSTable/HitTargetWSTable.sml", &doc, error)) {
        hit_target_moves_.clear();
        for (const SmlRecord &record : doc.records) {
            HitTargetMove move;
            move.id = record.integer("ID");
            move.anim_id = record.integer("AnimID", -1);
            move.is_force_finish = record.integer("IsForceFinish") != 0;
            move.is_boost = record.integer("IsBoost") != 0;
            move.show_anim_id = record.integer("ShowAnimID", -1);
            move.hit_target_anim_id = record.integer("HitTargetAnimID", -1);
            move.jump_height = static_cast<float>(record.number("JumpHeight"));
            move.success_state = record.integer("SuccessState", state::IDLE);
            move.fail_state = record.integer("FailState", state::IDLE);
            move.camera_shake = record.integer("IsCameraShake") != 0;
            hit_target_moves_.push_back(move);
        }
    }
    if (loadOne(source, root + "/WSTable/TurnbuckleWSTable.sml", &doc, error)) {
        turnbuckle_moves_.clear();
        for (const SmlRecord &record : doc.records) {
            TurnbuckleMove move;
            move.id = record.integer("ID");
            move.anim_id = record.integer("AnimID", -1);
            move.hit_target_anim_id = record.integer("HitTargetAnimID", -1);
            move.need_take_off = record.integer("IsNeedTakeOff") != 0;
            move.on_ground_attack = record.integer("IsOnGroundAttack") != 0;
            move.jump_height = static_cast<float>(record.number("JumpHeight"));
            move.need_face = record.integer("IsNeedFace") != 0;
            move.attack_timing = static_cast<float>(record.number("AttackTimming"));
            move.success_state = record.integer("SuccessState", state::IDLE);
            move.fail_state = record.integer("FailState", state::IDLE);
            turnbuckle_moves_.push_back(move);
        }
    }
    if (loadOne(source, root + "/WSTable/SpringboardWSTable.sml", &doc, error)) {
        springboard_moves_.clear();
        for (const SmlRecord &record : doc.records) {
            SpringboardMove move;
            move.id = record.integer("ID");
            move.anim_id = record.integer("AnimID", -1);
            move.end_anim_id = record.integer("EndAnimID", -1);
            move.on_ground_attack = record.integer("IsOnGroundAttack") != 0;
            move.reverse = record.integer("IsReverse") != 0;
            move.need_face = record.integer("IsNeedFace") != 0;
            move.jump_height = static_cast<float>(record.number("JumpHeight"));
            move.success_state = record.integer("SuccessState", state::IDLE);
            move.fail_state = record.integer("FailState", state::IDLE);
            move.opponent_hurt_state = record.integer("OppponentHurtState", state::HITREACT_MID);
            springboard_moves_.push_back(move);
        }
    }
    if (loadOne(source, root + "/WSTable/TagAttackWSTable.sml", &doc, error)) {
        tag_attack_moves_.clear();
        for (const SmlRecord &record : doc.records) {
            TagAttackMove move;
            move.id = record.integer("ID");
            move.turnbuckle_ws_id = record.integer("TurnbuckleWSID", -1);
            tag_attack_moves_.push_back(move);
        }
    }
    if (loadOne(source, root + "/WSTable/RushForceWSTable.sml", &doc, error)) {
        rush_force_moves_.clear();
        for (const SmlRecord &record : doc.records) {
            RushForceMove move;
            move.id = record.integer("ID");
            move.begin_anim_id = record.integer("BegAnimId", -1);
            move.run_anim_id = record.integer("RunAnimId", -1);
            move.end_anim_id = record.integer("EndAnimId", -1);
            move.act_anim_id = record.integer("ActAnimId", -1);
            move.on_exit_state_for_end = record.integer("OnExitStateForEnd", state::IDLE);
            move.jump_height = static_cast<float>(record.number("JumpHeight"));
            move.opponent_anim_id = record.integer("OpppnentAnimID", -1);
            move.adjust_opponent = record.integer("IsForceAdjustOpponent") != 0;
            move.on_exit_self_state = record.integer("OnExitSelfState", state::IDLE);
            move.on_exit_self_x_offset = static_cast<float>(record.number("OnExitSelfXOffset"));
            move.on_exit_opponent_state = record.integer("OnExitOpponentState", state::IDLE);
            move.on_exit_opponent_x_offset =
                static_cast<float>(record.number("OnExitOpponentXOffset"));
            move.opponent_reverse = record.integer("IsOpponentReverse") != 0;
            move.hurt_opponent_timing = static_cast<float>(record.number("HurtOpponentTimming", -1.0));
            rush_force_moves_.push_back(std::move(move));
        }
    }
    if (loadOne(source, root + "/WSTable/ForceWSJumpTable.sml", &doc, error)) {
        force_jumps_.clear();
        for (const SmlRecord &record : doc.records) {
            ForceJump jump;
            jump.id = record.integer("ID");
            jump.jump_percentage = static_cast<float>(record.number("JumpPercentage"));
            jump.start_frame = record.integer("StartFrame");
            jump.frame_count = record.integer("FrameCount");
            jump.total_time = static_cast<float>(record.number("TotalTime"));
            jump.jump_height = static_cast<float>(record.number("JumpHeight"));
            jump.x_factor = static_cast<float>(record.number("XFactor"));
            jump.extra_energy_damage = static_cast<float>(record.number("ExtraEnergyDamage"));
            jump.extra_motion_damage = static_cast<float>(record.number("ExtraMotionPowerDamage"));
            force_jumps_.push_back(jump);
        }
    }
    if (loadOne(source, root + "/AirborneTable.sml", &doc, error)) {
        airborne_.clear();
        for (const SmlRecord &record : doc.records) {
            AirborneInfo info;
            info.id = record.integer("ID");
            info.opponent_state = record.integer("OpponentState", 0xff);
            info.opponent_use_ws = record.integer("OpponentUseWS", -1);
            info.can_fly_out = record.integer("CanFlyOut") != 0;
            info.start_y = static_cast<float>(record.number("StartY"));
            info.fly_height = static_cast<float>(record.number("FlyHeight"));
            airborne_.push_back(info);
        }
    }
    if (loadOne(source, root + "/FallAwayTable.sml", &doc, error)) {
        fall_away_.clear();
        for (const SmlRecord &record : doc.records) {
            FallAwayInfo info;
            info.id = record.integer("ID");
            info.opponent_use_ws = record.integer("OpponentUseWS", -1);
            info.anim_id = record.integer("AnimId", -1);
            info.start_y = static_cast<float>(record.number("StartY"));
            info.delta_z = static_cast<float>(record.number("DeltaZ"));
            info.need_reverse = record.integer("IsNeedReverse") != 0;
            fall_away_.push_back(info);
        }
    }
    if (loadOne(source, root + "/ButtonCountTable.sml", &doc, error)) {
        button_counts_.clear();
        static const char *const kBands[12] = {
            "Energy95Up",     "Energy95To90", "Energy90To80", "Energy80To70",
            "Energy70To60",   "Energy60To50", "Energy50To40", "Energy40To30",
            "Energy30To20",   "Energy20To10", "Energy10To5",  "Energy5Down"};
        for (const SmlRecord &record : doc.records) {
            ButtonCountRow row;
            row.id = record.integer("ID");
            row.label = record.string("Name");
            row.state_table_id = record.integer("StateTableID");
            row.ws_table_id = record.integer("WSTableID", -1);
            for (int i = 0; i < 12; ++i) {
                row.goals[static_cast<std::size_t>(i)] = record.integer(kBands[i]);
            }
            button_counts_.push_back(std::move(row));
        }
    }
    if (loadOne(source, root + "/InGameTable.sml", &doc, error)) {
        rules_.clear();
        for (const SmlRecord &record : doc.records) {
            RuleInfo info;
            info.id = record.integer("ID");
            info.name = record.string("RuleName");
            info.max_match_time = static_cast<float>(record.number("MaxMatchTime", 360.0));
            info.max_outside_time = static_cast<float>(record.number("MaxOutsideTime", 30.0));
            info.max_outside_count = record.integer("MaxOutsideCount", 20);
            info.max_referee_count = record.integer("MaxRefereeCount", 3);
            info.toss_out_percentage = static_cast<float>(record.number("TossOutPercentage", 100.0));
            info.toss_out_dynamic_by = record.integer("TossOutDynamicBy");
            info.can_power_up = record.integer("CanPowerUp", 1) != 0;
            info.can_pin = record.integer("CanPin", 1) != 0;
            info.can_submission = record.integer("CanSubmission", 1) != 0;
            info.have_referee = record.integer("HaveReferee", 1) != 0;
            info.heal_by_free_pin = record.integer("CanHealEnergyByFreePin", 1) != 0;
            info.heal_by_kick_out = record.integer("CanHealEnergyByKickOutOpponent", 1) != 0;
            rules_.push_back(std::move(info));
        }
    }
    if (loadOne(source, root + "/CommonVariableInfo.sml", &doc, error) && !doc.records.empty()) {
        const SmlRecord &record = doc.records.front();
        common_.dogfight_percentage = static_cast<float>(record.number("DogfightPercentage", 75.0));
        common_.win_grapple_percentage = static_cast<float>(record.number("WinGrapplePercentage", 50.0));
        common_.tag_area_width = static_cast<float>(record.number("TagAreaWidth", 80.0));
        common_.tag_area_height = static_cast<float>(record.number("TagAreaHeight", 80.0));
        common_.airborne_factor_x = static_cast<float>(record.number("AirborneFactorX", 550.0));
        common_.use_toss_out_area_width = static_cast<float>(record.number("UseToseOutAreaWidth", 50.0));
        common_.collision_range_z = static_cast<float>(record.number("CollisionRangeZ", 40.0));
        common_.collision_feet_factor_x = static_cast<float>(record.number("CollisionFeetFactorX", 80.0));
        common_.collision_head_factor_x = static_cast<float>(record.number("CollisionHeadFactorX", 60.0));
        common_.slammed_height = static_cast<float>(record.number("SlammedHeight", 100.0));
        common_.turnbuckle_height = static_cast<float>(record.number("TurnbuckleHeight", 180.0));
        common_.stage_offset_height = static_cast<float>(record.number("StageOffsetHeight", 25.0));
        common_.outside_back_to_in_ring_width =
            static_cast<float>(record.number("OutsideBackToInRingWidth", 300.0));
        common_.kick_out_lose_time = static_cast<float>(record.number("KickOutLoseTime", 0.75));
        common_.kick_out_jump_time = static_cast<float>(record.number("KickOutJumpTime", 0.5));
        common_.kick_out_jump_factor_x = static_cast<float>(record.number("KickOutJumpFactorX", 400.0));
        common_.kick_out_jump_height = static_cast<float>(record.number("KickOutJumpHeight", 180.0));
        common_.kick_out_disappear_time = static_cast<float>(record.number("KickOutDisappearTime", 1.0));
        common_.max_suffocation_time = static_cast<float>(record.number("MaxSuffocationTime", 1.0));
        common_.heal_energy_by_free_pin =
            static_cast<float>(record.number("HealEnergyByFreePin", 30.0));
        common_.heal_energy_by_kick_out_opponent =
            static_cast<float>(record.number("HealEnergyByKickOutOpponent", 15.0));
        common_.add_counter_per_no_energy_pin =
            static_cast<float>(record.number("AddCounterPerNoEnergyPin", 5.0));
    }
    if (loadOne(source, root + "/CommonDamageInfo.sml", &doc, error) && !doc.records.empty()) {
        const SmlRecord &record = doc.records.front();
        auto pair = [&record](const char *name, float *energy, float *motion) {
            *energy = static_cast<float>(record.numberAt(name, 0));
            *motion = static_cast<float>(record.numberAt(name, 1));
        };
        pair("DamageWSFail", &damage_.ws_fail_energy, &damage_.ws_fail_motion);
        pair("DamageHitRail", &damage_.hit_rail_energy, &damage_.hit_rail_motion);
        pair("DamageRunRam", &damage_.run_ram_energy, &damage_.run_ram_motion);
        pair("DamageBeRunRammed", &damage_.be_run_rammed_energy, &damage_.be_run_rammed_motion);
        pair("DamageBePushedOff", &damage_.be_pushed_off_energy, &damage_.be_pushed_off_motion);
        pair("DamageBeBrokenSleepHold", &damage_.be_broken_sleep_hold_energy,
             &damage_.be_broken_sleep_hold_motion);
        pair("DamageBeBrokenRestrain", &damage_.be_broken_restrain_energy,
             &damage_.be_broken_restrain_motion);
    }

    // Arenas.
    arenas_.clear();
    for (const char *file : {"Contents/General/AllArenaTable.sml",
                             "Contents/General/AllArenaTableDLC01.sml",
                             "Contents/General/AllArenaTableDLC02.sml",
                             "Contents/General/AllArenaTableDLC03.sml",
                             "Contents/General/AllArenaTableDLC04.sml"}) {
        SmlDocument arena_doc;
        std::string arena_error;
        if (!loadOne(source, root + "/" + file, &arena_doc, &arena_error)) continue;
        for (const SmlRecord &record : arena_doc.records) {
            const std::string folder = record.string("FolderName");
            if (folder.empty()) continue;
            const bool duplicate =
                std::any_of(arenas_.begin(), arenas_.end(),
                            [&folder](const ArenaInfo &arena) { return arena.folder == folder; });
            if (duplicate) continue;
            ArenaInfo info;
            info.id = record.integer("Id");
            info.name = record.string("Name");
            info.folder = folder;
            info.entrance_scene_id = record.integer("EnrtanceSceneId");
            arenas_.push_back(std::move(info));
        }
    }
    return true;
}

bool GameData::loadWrestlers(AssetSource *source, const std::string &root, std::string *error) {
    SmlDocument doc;
    if (!loadOne(source, root + "/Wrestler/00_John Cena/BaseInfo.sml", &doc, error)) {
        return false;
    }

    wrestlers_.clear();
    int id = 0;
    for (const char *folder : kRosterFolders) {
        WrestlerProfile profile;
        profile.id = id++;
        profile.folder = folder;
        profile.name = folderToDisplayName(folder);
        profile.name_line1 = profile.name;
        profile.type = (folder[0] == '0' && folder[1] == '0') ? 0 : 1;

        // BaseInfo.sml
        std::string path = root + "/Wrestler/" + folder + "/BaseInfo.sml";
        std::vector<std::uint8_t> bytes;
        if (source->read(path, &bytes)) {
            std::string parse_error;
            if (doc.parse(reinterpret_cast<const char *>(bytes.data()), bytes.size(), &parse_error) &&
                !doc.records.empty()) {
                const SmlRecord &record = doc.records.front();
                WrestlerBaseInfo &base = profile.base;
                base.energy = static_cast<float>(record.number("Energy", 100.0));
                base.motion_power = static_cast<float>(record.number("MotionPower", 100.0));
                base.max_power_up_value = static_cast<float>(record.number("MaxPowerUpValue", 100.0));
                base.heal_eps = static_cast<float>(record.number("HealEPS", 1.0));
                base.heal_mps = static_cast<float>(record.number("HealMPPS", 5.0));
                base.heal_pups = static_cast<float>(record.number("HealPUPS", 2.0));
                base.walk_factor_x = static_cast<float>(record.number("WalkFactorX", 315.0));
                base.walk_factor_z = static_cast<float>(record.number("WalkFactorZ", 270.0));
                base.run_factor_x = static_cast<float>(record.number("RunFactorX", 600.0));
                base.speed_down_factor_tired =
                    static_cast<float>(record.number("SpeedDownFactorTired", 0.3));
                base.speed_down_factor_weapon =
                    static_cast<float>(record.number("SpeedDownFactorWithWeapon", 0.5));
                base.max_headlocked_time = static_cast<float>(record.number("MaxBeHeadlockedTime", 2.0));
                base.max_on_ground_time_min = static_cast<float>(record.numberAt("MaxOnGroundTime", 0, 0.4));
                base.max_on_ground_time_max = static_cast<float>(record.numberAt("MaxOnGroundTime", 1, 8.0));
                base.panting_value = static_cast<float>(record.number("PantingValue", 20.0));
                base.tired_value = static_cast<float>(record.number("TiredValue", 30.0));
                base.gbh_value = static_cast<float>(record.number("GBHValue", 24.0));
                base.power_ws_value = static_cast<float>(record.number("PowerWSValue", 50.0));
                base.bounce_off_rope_timing =
                    static_cast<float>(record.number("BounceOffRopeTimming", 150.0));
                base.free_from_headlock_timing =
                    static_cast<float>(record.number("FreeFromHeadlockTimming", 0.2));
                base.max_power_up_duration =
                    static_cast<float>(record.number("MaxPowerUpDuration", 20.0));
                base.max_power_up_interval =
                    static_cast<float>(record.number("MaxPowerUpInterval", 40.0));
                base.power_up_bonus = static_cast<float>(record.number("PowerUpBonus", 10.0));
            } else {
                warnings_.push_back(std::string("BaseInfo parse failed: ") + folder);
            }
        }

        // SuperStarWSTable.sml - the 9 status slots.
        path = root + "/Wrestler/" + folder + "/SuperStarWSTable.sml";
        if (source->read(path, &bytes)) {
            std::string parse_error;
            if (doc.parse(reinterpret_cast<const char *>(bytes.data()), bytes.size(), &parse_error)) {
                for (const SmlRecord &record : doc.records) {
                    const int status = record.integer("StatusID");
                    if (status < 0 || status >= 9) continue;
                    StatusSlot &slot = profile.status[static_cast<std::size_t>(status)];
                    slot.status_id = status;
                    slot.list_count = record.integer("ListCount");
                    static const char *const kAttacks[4] = {"Attack1", "Attack2", "Attack3", "Attack4"};
                    static const char *const kE[4] = {"EDamage1", "EDamage2", "EDamage3", "EDamage4"};
                    static const char *const kM[4] = {"MDamage1", "MDamage2", "MDamage3", "MDamage4"};
                    static const char *const kS[4] = {"Score1", "Score2", "Score3", "Score4"};
                    for (int i = 0; i < 4; ++i) {
                        slot.attack[static_cast<std::size_t>(i)] =
                            record.integer(kAttacks[i], -99);
                        slot.e_damage[static_cast<std::size_t>(i)] = record.integer(kE[i]);
                        slot.m_damage[static_cast<std::size_t>(i)] = record.integer(kM[i]);
                        slot.score[static_cast<std::size_t>(i)] = record.integer(kS[i]);
                    }
                }
            }
        }

        // wrestlerData.sml - sprite names + per-sprite draw offsets.
        path = root + "/Wrestler/" + folder + "/wrestlerData.sml";
        if (source->read(path, &bytes)) {
            std::string parse_error;
            if (doc.parse(reinterpret_cast<const char *>(bytes.data()), bytes.size(), &parse_error)) {
                int max_index = -1;
                for (const SmlRecord &record : doc.records) {
                    max_index = std::max(max_index, record.integer("id"));
                }
                profile.sprites.assign(static_cast<std::size_t>(max_index + 1), SpriteEntry{});
                for (const SmlRecord &record : doc.records) {
                    const int sprite_id = record.integer("id");
                    if (sprite_id < 0) continue;
                    SpriteEntry entry;
                    entry.name = record.string("name");
                    entry.x_offset = static_cast<float>(record.number("xoffset"));
                    entry.y_offset = static_cast<float>(record.number("yoffset"));
                    profile.sprites[static_cast<std::size_t>(sprite_id)] = entry;
                    if (!entry.name.empty()) {
                        profile.sprite_index[entry.name] = sprite_id;
                    }
                }
            }
        }

        // SFXTable.sml - the announcer calls.
        path = root + "/Wrestler/" + folder + "/SFXTable.sml";
        if (source->read(path, &bytes)) {
            SmlDocument sfx_doc;
            std::string parse_error;
            if (sfx_doc.parse(reinterpret_cast<const char *>(bytes.data()), bytes.size(),
                              &parse_error)) {
                for (const SmlRecord &record : sfx_doc.records) {
                    SfxInfo cue;
                    cue.id = record.integer("ID");
                    cue.title = record.string("Title");
                    cue.file_name = record.string("FileName");
                    cue.volume = static_cast<float>(record.number("Volume", 1.0));
                    cue.length = static_cast<float>(record.number("Length", 0.0));
                    if (!cue.file_name.empty()) profile.sfx.push_back(std::move(cue));
                }
            }
        }

        // EntranceInfo.sml - which WrestlerAT animations the entrance plays.
        path = root + "/Wrestler/" + folder + "/EntranceInfo.sml";
        if (source->read(path, &bytes)) {
            SmlDocument entrance_doc;
            std::string parse_error;
            if (entrance_doc.parse(reinterpret_cast<const char *>(bytes.data()), bytes.size(),
                                   &parse_error) &&
                !entrance_doc.records.empty()) {
                const SmlRecord &record = entrance_doc.records.front();
                profile.entrance.walk_factor =
                    static_cast<float>(record.number("WalkFactor", 24.0));
                profile.entrance.run_factor =
                    static_cast<float>(record.number("RunFactor", 150.0));
                const SmlField *list = record.find("AnimList");
                if (list != nullptr) {
                    for (double value : list->numbers) {
                        const int anim = static_cast<int>(value);
                        if (anim >= 0) profile.entrance.anims.push_back(anim);
                    }
                }
            }
        }

        // A wrestler with no EntranceInfo still gets a usable intro.
        if (profile.entrance.anims.empty()) {
            for (const AnimInfo &anim : anims_) {
                if (anim.name == "Entrance" || anim.name == "Entrance 1") {
                    profile.entrance.anims.push_back(anim.id);
                    break;
                }
            }
        }

        wrestlers_.push_back(std::move(profile));
    }
    return true;
}

bool GameData::load(AssetSource *source, const std::string &data_root, std::string *error) {
    if (source == nullptr) {
        if (error != nullptr) *error = "null asset source";
        return false;
    }
    warnings_.clear();
    // "." keeps the joined paths relative (an AssetSource may already be rooted
    // at the data directory, e.g. DirectoryAssetSource).
    const std::string root = data_root.empty() ? std::string(".") : data_root;
    bool ok = loadCommonTables(source, root, error);
    ok = loadWrestlers(source, root, error) && ok;

    // Display names from the roster table override the folder-derived ones.
    for (const char *file : {"Contents/General/AllSuperStarTable.sml",
                             "Contents/General/AllSuperStarTableDLC01.sml",
                             "Contents/General/AllSuperStarTableDLC02.sml",
                             "Contents/General/AllSuperStarTableDLC03.sml",
                             "Contents/General/AllSuperStarTableDLC04.sml"}) {
        SmlDocument doc;
        std::string ignored;
        if (!loadOne(source, root + "/" + file, &doc, &ignored)) continue;
        for (const SmlRecord &record : doc.records) {
            const std::string folder = record.string("FolderName");
            if (folder.empty()) continue;
            for (WrestlerProfile &profile : wrestlers_) {
                if (profile.folder != folder) continue;
                profile.name = record.string("Name");
                profile.name_line1 = record.string("NameLine1");
                profile.name_line2 = record.string("NameLine2");
                profile.type = record.integer("Type");
                if (profile.name_line1.empty()) profile.name_line1 = profile.name;
                break;
            }
        }
    }
    return ok;
}

const StateInfo *GameData::stateInfo(int id) const { return findById(states_, id); }
const AnimInfo *GameData::animInfo(int id) const { return findById(anims_, id); }
const MoveInfo *GameData::moveInfo(int id) const { return findById(moves_, id); }
const ForceMove *GameData::forceMove(int id) const { return findById(force_moves_, id); }
const RamMove *GameData::ramMove(int id) const { return findById(ram_moves_, id); }
const ClotheslineMove *GameData::clotheslineMove(int id) const {
    return findById(clothesline_moves_, id);
}
const SubmissionMove *GameData::submissionMove(int id) const {
    return findById(submission_moves_, id);
}
const HitTargetMove *GameData::hitTargetMove(int id) const { return findById(hit_target_moves_, id); }
const TurnbuckleMove *GameData::turnbuckleMove(int id) const {
    return findById(turnbuckle_moves_, id);
}
const SpringboardMove *GameData::springboardMove(int id) const {
    return findById(springboard_moves_, id);
}
const RushForceMove *GameData::rushForceMove(int id) const { return findById(rush_force_moves_, id); }
const ForceJump *GameData::forceJump(int id) const { return findById(force_jumps_, id); }
const AirborneInfo *GameData::airborneInfo(int id) const { return findById(airborne_, id); }
const FallAwayInfo *GameData::fallAwayInfo(int id) const { return findById(fall_away_, id); }
const RuleInfo *GameData::rule(int id) const { return findById(rules_, id); }
const WrestlerProfile *GameData::wrestler(int id) const { return findById(wrestlers_, id); }

const WrestlerProfile *GameData::wrestlerByFolder(const std::string &folder) const {
    for (const WrestlerProfile &profile : wrestlers_) {
        if (profile.folder == folder) return &profile;
    }
    return nullptr;
}

int GameData::buttonCountGoal(int state_id, float energy, int pin_attempts) const {
    // Energy bands, GetButtonCountGoal: >95, 95-90, 90-80, ... 10-5, <5.
    int band = 11;
    if (energy > 95.0f) {
        band = 0;
    } else if (energy > 90.0f) {
        band = 1;
    } else if (energy > 80.0f) {
        band = 2;
    } else if (energy > 70.0f) {
        band = 3;
    } else if (energy > 60.0f) {
        band = 4;
    } else if (energy > 50.0f) {
        band = 5;
    } else if (energy > 40.0f) {
        band = 6;
    } else if (energy > 30.0f) {
        band = 7;
    } else if (energy > 20.0f) {
        band = 8;
    } else if (energy > 10.0f) {
        band = 9;
    } else if (energy > 5.0f) {
        band = 10;
    }

    for (const ButtonCountRow &row : button_counts_) {
        if (row.state_table_id != state_id) continue;
        int goal = row.goals[static_cast<std::size_t>(band)];
        if (state_id == state::BE_PINNED && pin_attempts > 0) {
            goal += static_cast<int>(common_.add_counter_per_no_energy_pin * pin_attempts);
        }
        return goal;
    }
    return 0;
}

}  // namespace wrestlefest
