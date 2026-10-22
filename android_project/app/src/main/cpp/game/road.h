#ifndef WRESTLEFEST_GAME_ROAD_H
#define WRESTLEFEST_GAME_ROAD_H

// Road to WrestleMania: the stage ladder (StageScene in the original).
//
// The mode is a run of five "levels", each a ladder of pay-per-view cards you
// have to win in order; the last card of every level is the event itself. Every
// card is one match with its own rule and its own arena, and losing sends you
// back to the ladder to try again.
//
// The ladder below is transcribed from StageScene::StageScene (which loads the
// stage art per level) and StageScene::Draw (which names each card). The art is
// shipped: ui/Sprites/UI/stage/<art>.png.
//
// This is the skeleton of the mode - it plays, and it tracks progress for the
// session, but there is no save file, no profile screen and no belt/progress
// screen yet. Those hang off the same table.

#include <string>
#include <vector>

namespace wrestlefest {
namespace road {

// One card on the ladder.
struct Stage {
    const char *art = nullptr;    // ui/Sprites/UI/stage/<art>.png
    const char *label = nullptr;  // printed under the card
    int rule = 0;                 // InGameTable id
    int wrestlers = 2;            // how many men the card puts in the ring
    int arena = 0;                // AllArenaTable id (see kArenaFor below)
};

// One level: a named run of cards ending in a pay-per-view.
struct Level {
    const char *name = nullptr;
    std::vector<Stage> stages;
};

// The five levels, in order. Index into this is gameData[0x140] in the C.
const std::vector<Level> &levels();

// AllArenaTable ids, discovered by folder at load time (the table ships five
// records: SmackDown, Raw, WrestleMania, Royal Rumble, Saturday Night's Main
// Event). -1 means "keep the arena the player picked".
enum : int {
    kArenaSmackDown = 0,
    kArenaRaw = 1,
    kArenaWrestleMania = 2,
    kArenaRoyalRumble = 3,
    kArenaSaturdayNight = 4,
};

// Progress through the run, kept in memory for the session.
struct Progress {
    int level = 0;
    int stage = 0;  // index within the current level

    const Level *current() const;
    const Stage *currentStage() const;
    int stageCount() const;
    // Advances to the next card, or to the next level when the run is done.
    void advance();
    // Every level cleared: the road ends at WrestleMania.
    bool complete() const;
    void reset() {
        level = 0;
        stage = 0;
    }
};

std::string levelName(int level);
std::string stageTitle(const Progress &progress);

}  // namespace road
}  // namespace wrestlefest

#endif  // WRESTLEFEST_GAME_ROAD_H
