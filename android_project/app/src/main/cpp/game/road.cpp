#include "road.h"

namespace wrestlefest {
namespace road {

// Ladders transcribed from the original:
//
//   StageScene::StageScene loads the stage art per level -
//     0: raw, smack_down, raw, smack_down, night_champions
//     1: smack_down, raw, smack_down, fatal, wrestlemania
//     2: raw, smack_down, saturday, raw, smack_down, summer
//     3: smack_down, raw, smack_down, hell, raw, smack_down, raw, survivor
//     4: raw, smack_down, raw, royal_rumble, smack_down, raw, smack_down,
//        wrestlemania
//
//   StageScene::Draw names each card -
//     0: EXHIBITION, EXHIBITION, EXHIBITION, TRIPLE THREAT, EXHIBITION
//     1: EXHIBITION, EXHIBITION, EXHIBITION, FATAL 4 WAY, EXHIBITION
//     2: TAG TEAM x6
//     3: EXHIBITION, EXHIBITION, EXHIBITION, FATAL 4 WAY, EXHIBITION,
//        EXHIBITION, TRIPLE THREAT, EXHIBITION
//     4: EXHIBITION, EXHIBITION, TRIPLE THREAT, ROYAL RUMBLE, EXHIBITION,
//        EXHIBITION, FATAL 4 WAY, EXHIBITION
//
// The rule each label maps to is the InGameTable id: EXHIBITION is RULE_1VS1,
// TAG TEAM is RULE_2VS2, ROYAL RUMBLE is RULE_ROYAL_RUMBLE and the multi-man
// cards are RULE_BASIC_NON_ELIMINATION_MATCHES (3) with as many men as the
// label says.

static const Level kLevel0 = {"LEVEL 1",
                              {
                                  {"stage_raw", "EXHIBITION", 0, 2, kArenaRaw},
                                  {"stage_smack_down", "EXHIBITION", 0, 2, kArenaSmackDown},
                                  {"stage_raw", "EXHIBITION", 0, 2, kArenaRaw},
                                  {"stage_smack_down", "TRIPLE THREAT", 3, 3, kArenaSmackDown},
                                  {"stage_night_champions", "EXHIBITION", 0, 2,
                                   kArenaSmackDown},
                              }};

static const Level kLevel1 = {"LEVEL 2",
                              {
                                  {"stage_smack_down", "EXHIBITION", 0, 2, kArenaSmackDown},
                                  {"stage_raw", "EXHIBITION", 0, 2, kArenaRaw},
                                  {"stage_smack_down", "EXHIBITION", 0, 2, kArenaSmackDown},
                                  {"stage_fatal", "FATAL 4 WAY", 3, 4, kArenaSmackDown},
                                  {"stage_wrestlemania", "EXHIBITION", 0, 2,
                                   kArenaWrestleMania},
                              }};

static const Level kLevel2 = {"LEVEL 3",
                              {
                                  {"stage_raw", "TAG TEAM", 1, 4, kArenaRaw},
                                  {"stage_smack_down", "TAG TEAM", 1, 4, kArenaSmackDown},
                                  {"stage_saturday", "TAG TEAM", 1, 4, kArenaSaturdayNight},
                                  {"stage_raw", "TAG TEAM", 1, 4, kArenaRaw},
                                  {"stage_smack_down", "TAG TEAM", 1, 4, kArenaSmackDown},
                                  {"stage_summer", "TAG TEAM", 1, 4, kArenaSmackDown},
                              }};

static const Level kLevel3 = {"LEVEL 4",
                              {
                                  {"stage_smack_down", "EXHIBITION", 0, 2, kArenaSmackDown},
                                  {"stage_raw", "EXHIBITION", 0, 2, kArenaRaw},
                                  {"stage_smack_down", "EXHIBITION", 0, 2, kArenaSmackDown},
                                  {"stage_hell", "FATAL 4 WAY", 3, 4, kArenaSmackDown},
                                  {"stage_raw", "EXHIBITION", 0, 2, kArenaRaw},
                                  {"stage_smack_down", "EXHIBITION", 0, 2, kArenaSmackDown},
                                  {"stage_raw", "TRIPLE THREAT", 3, 3, kArenaRaw},
                                  {"stage_survivor", "EXHIBITION", 0, 2, kArenaSmackDown},
                              }};

static const Level kLevel4 = {"LEVEL 5",
                              {
                                  {"stage_raw", "EXHIBITION", 0, 2, kArenaRaw},
                                  {"stage_smack_down", "EXHIBITION", 0, 2, kArenaSmackDown},
                                  {"stage_raw", "TRIPLE THREAT", 3, 3, kArenaRaw},
                                  {"stage_royal_rumble", "ROYAL RUMBLE", 2, 6,
                                   kArenaRoyalRumble},
                                  {"stage_smack_down", "EXHIBITION", 0, 2, kArenaSmackDown},
                                  {"stage_raw", "EXHIBITION", 0, 2, kArenaRaw},
                                  {"stage_smack_down", "FATAL 4 WAY", 3, 4, kArenaSmackDown},
                                  {"stage_wrestlemania", "EXHIBITION", 0, 2,
                                   kArenaWrestleMania},
                              }};

const std::vector<Level> &levels() {
    static const std::vector<Level> table = {kLevel0, kLevel1, kLevel2, kLevel3, kLevel4};
    return table;
}

const Level *Progress::current() const {
    const std::vector<Level> &table = levels();
    if (level < 0 || level >= static_cast<int>(table.size())) return nullptr;
    return &table[static_cast<std::size_t>(level)];
}

const Stage *Progress::currentStage() const {
    const Level *l = current();
    if (l == nullptr) return nullptr;
    if (stage < 0 || stage >= static_cast<int>(l->stages.size())) return nullptr;
    return &l->stages[static_cast<std::size_t>(stage)];
}

int Progress::stageCount() const {
    const Level *l = current();
    return l != nullptr ? static_cast<int>(l->stages.size()) : 0;
}

void Progress::advance() {
    const Level *l = current();
    if (l == nullptr) return;
    if (stage + 1 < static_cast<int>(l->stages.size())) {
        ++stage;
        return;
    }
    if (level + 1 < static_cast<int>(levels().size())) {
        ++level;
        stage = 0;
        return;
    }
    // The road ends at WrestleMania: step off the end of the ladder so the run
    // reports itself complete.
    ++level;
    stage = 0;
}

bool Progress::complete() const {
    return level >= static_cast<int>(levels().size());
}

std::string levelName(int level) {
    const std::vector<Level> &table = levels();
    if (level < 0 || level >= static_cast<int>(table.size())) return "ROAD TO WRESTLEMANIA";
    return table[static_cast<std::size_t>(level)].name;
}

std::string stageTitle(const Progress &progress) {
    const Stage *s = progress.currentStage();
    if (s == nullptr) return "";
    return std::string("MATCH ") + std::to_string(progress.stage + 1) + " - " + s->label;
}

}  // namespace road
}  // namespace wrestlefest
