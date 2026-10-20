#ifndef WRESTLEFEST_SIM_REFEREE_H
#define WRESTLEFEST_SIM_REFEREE_H

// CRef: four master states and a handful of sub-states (GAMEPLAY_IMPLEMENTATION
// 4.2). The referee is a full actor: it walks to a hand-placed spot 280 px from
// the action, then runs the 3-count / give-up / tag-break routine.

#include "actor.h"
#include "collision.h"

namespace wrestlefest {

class Match;

enum class RefMaster : int {
    Idle = 0,
    Watch = 1,
    Move = 2,
    Outside = 3,
};

enum class RefSub : int {
    None = 0,
    MoveToPosition = 2,
    PinCount = 3,
    GiveUp = 4,
    TagBreak = 5,
};

class Referee : public Actor {
public:
    void setup(const GameData *data, const RingGeometry *ring, int max_count);

    void tick(float dt, Match &match);

    RefMaster master() const { return master_; }
    RefSub sub() const { return sub_; }
    int count() const { return count_; }
    int maxCount() const { return max_count_; }
    bool counting() const { return master_ == RefMaster::Move && sub_ == RefSub::PinCount; }
    float reactionDelay() const { return reaction_delay_; }

private:
    void watch(Match &match);
    void moveTo(float target_x, float target_z, float dt);

    const GameData *data_ = nullptr;
    const RingGeometry *ring_ = nullptr;
    RefMaster master_ = RefMaster::Watch;
    RefSub sub_ = RefSub::None;
    int count_ = 0;
    int max_count_ = 3;
    float count_timer_ = 0.0f;
    float reaction_delay_ = 0.0f;
    int watched_actor_ = -1;
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_SIM_REFEREE_H
