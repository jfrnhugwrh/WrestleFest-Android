// Host test for the Road to WrestleMania ladder (game/road.h).
//
//   g++ -std=c++17 -O1 -Wall -Wextra -I<cpp root> game/road_test.cpp game/road.cpp
//       -o /tmp/road_test
//
// The ladder is data, so what is worth guarding is that it stays consistent:
// every card points at a rule and an arena the tables actually have, and a run
// walks the whole ladder exactly once and then stops.

#include "road.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
    if (ok) {
        std::printf("  ok   %s\n", what);
    } else {
        std::printf("  FAIL %s\n", what);
        ++failures;
    }
}

// The InGameTable ids the ladder is allowed to use (see menu::kRule*).
bool ruleExists(int rule) { return rule >= 0 && rule <= 4; }
// AllArenaTable ships five records.
bool arenaExists(int arena) { return arena >= 0 && arena <= 4; }

}  // namespace

int main() {
    std::printf("[road ladder]\n");
    const std::vector<wrestlefest::road::Level> &levels = wrestlefest::road::levels();
    check(levels.size() == 5, "the road has five levels");

    int stages = 0;
    int rumble_cards = 0;
    int tag_cards = 0;
    int multi_man_cards = 0;
    bool all_valid = true;
    for (const wrestlefest::road::Level &level : levels) {
        if (level.name == nullptr || level.stages.empty()) all_valid = false;
        for (const wrestlefest::road::Stage &stage : level.stages) {
            ++stages;
            if (stage.art == nullptr || stage.label == nullptr) all_valid = false;
            if (!ruleExists(stage.rule)) all_valid = false;
            if (!arenaExists(stage.arena)) all_valid = false;
            if (stage.wrestlers < 2 || stage.wrestlers > 6) all_valid = false;
            if (stage.rule == 2) ++rumble_cards;
            if (stage.rule == 1) ++tag_cards;
            // A multi-man card needs a distinct team per man, so the field has
            // to be at least three and no more than four.
            if (stage.rule == 3) {
                ++multi_man_cards;
                if (stage.wrestlers < 3 || stage.wrestlers > 4) all_valid = false;
            }
        }
    }
    check(all_valid, "every card names art, a label, a shipped rule and arena");
    check(stages == 32, "the ladder runs 32 cards");
    check(rumble_cards == 1, "exactly one Royal Rumble card");
    check(tag_cards == 6, "level three is all tag team");
    // TRIPLE THREAT and FATAL 4 WAY: one each in levels one and two, two each
    // in levels four and five.
    check(multi_man_cards == 6, "six multi-man cards");

    std::printf("[road progress]\n");
    wrestlefest::road::Progress progress;
    check(progress.level == 0 && progress.stage == 0, "a new run starts on card one");
    check(progress.currentStage() != nullptr, "there is a card to wrestle");
    check(std::string(progress.currentStage()->label) == "EXHIBITION",
          "card one is an exhibition");

    // Walk the whole road: every card, in order, without skipping.
    int visited = 0;
    for (int level = 0; level < 5; ++level) {
        const int count = progress.stageCount();
        check(count > 0, "the level has cards");
        for (int card = 0; card < count; ++card) {
            check(progress.level == level && progress.stage == card, "the run is in order");
            ++visited;
            progress.advance();
        }
    }
    check(visited == 32, "the run visits all 32 cards");
    check(progress.complete(), "the run finishes after the last card");
    check(progress.currentStage() == nullptr, "a finished run has no current card");

    progress.reset();
    check(progress.level == 0 && progress.stage == 0 && !progress.complete(),
          "reset puts the run back on card one");

    std::printf("[road titles]\n");
    check(wrestlefest::road::levelName(0) == "LEVEL 1", "level one is named");
    check(wrestlefest::road::levelName(99) == "ROAD TO WRESTLEMANIA",
          "an unknown level falls back to the mode name");
    wrestlefest::road::Progress titled;
    check(wrestlefest::road::stageTitle(titled).find("EXHIBITION") != std::string::npos,
          "the stage title names the match");

    if (failures == 0) {
        std::printf("\nroad_test: PASS\n");
        return 0;
    }
    std::printf("\nroad_test: %d failure(s)\n", failures);
    return 1;
}
