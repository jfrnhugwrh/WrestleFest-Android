#ifndef WRESTLEFEST_SIM_WRESTLER_H
#define WRESTLEFEST_SIM_WRESTLER_H

// CWrestler: the 4-deep state stack, animation clock, combat resolution.
//
// The structure mirrors the original:
//   * InputFrame in, state changes out - the wrestler never reads the AI.
//   * Every state change goes through changeState() so the stack, the facing
//     multiply and the paired-animation adjust all happen in one place.
//   * Damage/timings come from GameData; constants that live in the binary are
//     in tuning.h.

#include "actor.h"
#include "collision.h"

#include <string>
#include <vector>

namespace wrestlefest {

class Match;

// Events are the only way the simulation talks to presentation (SFX, HUD).
enum class EventType {
    Hit,
    Impact,
    GrappleEngaged,
    GrappleWon,
    GrappleLost,
    Whip,
    RopeBounce,
    PinStarted,
    RefCount,
    KickOut,
    SubmissionStarted,
    SubmissionGiveUp,
    SubmissionBroken,
    CollisionDogfight,
    KnockDown,
    KnockedOut,
    Bell,
    Victory,
    Lose,
    Taunt,
    EnergyMeter,
    Announcer,
};

struct SimEvent {
    EventType type = EventType::Hit;
    int actor = -1;
    int other = -1;
    float value = 0.0f;
    int state = 0;
};

struct Rng {
    explicit Rng(std::uint32_t seed = 0x1234567u) : state_(seed == 0 ? 0x1234567u : seed) {}
    std::uint32_t next() {
        // xorshift32: deterministic and identical on every platform.
        std::uint32_t x = state_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state_ = x;
        return x;
    }
    int range(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<std::uint32_t>(n)); }
    void seed(std::uint32_t value) { state_ = value == 0 ? 0x1234567u : value; }

private:
    std::uint32_t state_;
};

class Wrestler : public Actor {
public:
    void setup(const WrestlerProfile *profile, int id, int team, const GameData *data,
               const RingGeometry *ring);

    // Single per-tick entry point. `match` supplies the opponent, the clock and
    // the event sink.
    void tick(float dt, Match &match, const InputFrame &input);

    // --- state machine -------------------------------------------------------
    // chain == 0 replaces the top of the stack, chain != 0 pushes (depth <= 4).
    void changeState(int new_state, int chain, Match &match);
    void nextState(Match &match);

    // --- combat helpers ------------------------------------------------------
    bool isDown() const;
    bool isBusy() const;   // in a move/reaction that input must not interrupt
    bool canAct() const;   // in a state that accepts movement input
    bool isDead() const { return region == region::KNOCKED_OUT; }
    void knockOut(Match &match);

    // Runs the currently selected move against the opponent.
    void useWrestleStyle(const MoveInfo &move, Match &match);
    // Selects the move the wrestler is about to execute (move_id + sub type).
    void beginMove(int move_id);

    // Animation state, exposed for the renderer.
    int animId() const { return anim_id_; }
    int spriteId() const;
    bool hidden() const { return subState() == tuning::kNoState; }
    float animFraction() const;

    int id() const { return id_; }
    int strength() const { return strength_; }
    const WrestlerProfile *profile() const { return profile_; }
    const GameData *data() const { return data_; }

    int opponentId() const { return opponent_; }
    void setOpponent(int index) { opponent_ = index; }

    // Driver of the "game clock" used by power-up timing; pushed by Match.
    void setGameClock(float seconds) { game_clock_ = seconds; }
    float gameClock() const { return game_clock_; }

    // --- flags from the original --------------------------------------------
    bool pinned = false;       // +0xa0
    bool held = false;         // +0xa1
    bool play_dead = false;    // +0xcf64
    int pin_attempts = 0;      // +0xcf6c
    float suffocation = 0.0f;  // +0xcf68
    float gbh = 0.0f;

    // Mash counter (TickButtonCounter).
    bool button_counter_active = false;
    int button_count = 0;
    int button_goal = 0;

    // Move bookkeeping.
    int move_id = -1;
    int move_sub_type = 0;
    bool move_hit_applied = false;
    int score = 0;

    // Counts down while a state owns the clock (reaction beats, anim timers).
    float state_timer = 0.0f;
    float dogfight_beat = 0.0f;

    // Rope bounce bookkeeping for IRISH_WHIPPED / KNOCKED_BACK.
    bool whip_bounced = false;

private:
    void tickState(float dt, Match &match, const InputFrame &input);
    void tickLocomotion(float dt, Match &match, const InputFrame &input);
    void tickStrike(float dt, Match &match);
    void tickReaction(float dt, Match &match);
    void tickGround(float dt, Match &match, const InputFrame &input);
    void tickWhip(float dt, Match &match);
    void tickGrapple(float dt, Match &match, const InputFrame &input);
    void tickMoveStates(float dt, Match &match);
    void tickPinStates(float dt, Match &match, const InputFrame &input);
    void tickSubmission(float dt, Match &match, const InputFrame &input);
    void tickDogfight(float dt, Match &match);

    void tryStartAttack(Match &match, const InputFrame &input);
    void startStrike(int state, Match &match);
    void startGrapple(Match &match, bool high);
    void advanceAnimation(float dt);
    // Locomotion/hold states repeat their animation; everything else plays once.
    bool loopsAnimation() const;
    void applyMoveHit(Match &match);
    void finishMove(Match &match);
    void updatePowerUp(float dt, Match &match, bool can_power_up);
    void moveInput(float dt, const InputFrame &input);

    // CollisionRangeX for a state, straight from StateTable.sml. A negative row
    // means the state does not collide; `fallback` is used only in that case.
    float collisionRange(int state_id, float fallback) const;
    // The xOffset of the animation a state entry plays (paired-animation snap).
    float stateSnapOffset(int state_id, float fallback) const;

    const WrestlerProfile *profile_ = nullptr;
    const GameData *data_ = nullptr;
    const RingGeometry *ring_ = nullptr;
    int id_ = 0;
    int opponent_ = -1;
    int strength_ = 1;
    int anim_id_ = 0;
    bool anim_finished_ = false;
    float game_clock_ = 0.0f;
    float powerup_started_at_ = 0.0f;
    float duration_ = 0.0f;  // length of the current state's animation
    float impact_time_ = -1.0f;
    float hurt_timing_ = -1.0f;
    bool hurt_done_ = false;
    bool in_submission_ = false;
    bool whip_owner_ = false;  // this wrestler performed the whip
    bool used_move_this_state_ = false;
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_SIM_WRESTLER_H
