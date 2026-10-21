#ifndef WRESTLEFEST_SIM_AI_H
#define WRESTLEFEST_SIM_AI_H

// tyCAIEntity: the 12 AI states of the original.
//
// The AI is stateless apart from a target and a timer: every tick it re-derives
// what to do from the wrestler's own state, then *presses buttons* - it writes
// into an InputFrame, never into the wrestler (GAMEPLAY_IMPLEMENTATION 5.2).

#include "actor.h"

namespace wrestlefest {

class Match;

enum class AiState : int {
    SearchEnemy = 0,
    Walk = 1,
    Dogfight = 2,
    GrappleIdle = 3,
    Attack = 4,
    BeAttacked = 5,
    OnGroundAttack = 6,
    BeHeld = 7,
    BePin = 8,
    RingSide = 9,
    OutSide = 10,
    InRingAlone = 11,
    None = 12,
};

struct AiEntity {
    int actor = -1;
    int target = -1;
    AiState state = AiState::SearchEnemy;
    float timer = 0.0f;      // time left in the current state's beat
    float beat = 0.0f;       // length of that beat
    float mash_timer_ = 0.0f;  // countdown to the next mash edge press
    // Pause between attacks, mimicking the original's per-state timing beats
    // (without it the AI re-presses the button on every idle frame).
    float attack_delay = 0.6f;

    // Per-tick decision: fills `input` for the actor this entity owns.
    void tick(float dt, Match &match, InputFrame *input);

private:
    void setState(AiState next, Match &match);
    void searchEnemy(Match &match);
    bool checkTarget(Match &match);
    void tickWalk(float dt, Match &match, InputFrame *input);
    void tickAttack(float dt, Match &match, InputFrame *input);
    void tickAttackVsOpponent(Match &match, InputFrame *input, int opponent_state);
    // Press a button edge every kAiMashPeriod seconds (get-up / escape mash).
    void emitMash(float dt, Match &match, InputFrame *input, bool also_blue);
    void tickGrapple(float dt, Match &match);
    void tickReaction(float dt, Match &match);
    void tickRingSide(Match &match, InputFrame *input);
    // 2 VS 2: walk to the corner and tag out when the partner is fresher.
    bool tickTag(Match &match, InputFrame *input);
    int partnerOf(Match &match) const;
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_SIM_AI_H
