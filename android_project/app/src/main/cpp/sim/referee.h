#ifndef WRESTLEFEST_SIM_REFEREE_H
#define WRESTLEFEST_SIM_REFEREE_H

// CRef & CAnnouncer: data-driven ring officials (GAMEPLAY_IMPLEMENTATION 4.2).
//
// Sprite frame indices come straight from RefereeAT.sml / refData.sml (0..13)
// and AnnouncerAT.sml / announcerData.sml (0..12), so the simulation and the
// renderer never drift out of phase:
//   refData.sml:
//     0..3   mike_1..4.png       (Ref Idle = [0], Ref Walk = [0,0,1,1,2,2,3,3])
//     4      countdown (1).png   (canvas drop transition onto knees)
//     5..6   countdown (2..3).png(Ref Countdown = [5x10, 6x6], 6 = mat slap)
//     7..8   Ask give up (1..2)  (Ref Ask Give Up = [7,7,7,8,8,8])
//     9..11  Judgment (1..3).png (Ref Judgment = [9,9,9,9,9,10,10,11,11])
//     12..13 Warn (1..2).png     (Ref Warn = [12,12,12,13,13,13])

#include "actor.h"
#include "collision.h"

namespace wrestlefest {

class Match;
class Wrestler;

enum class RefMaster : int {
    Idle = 0,
    Watch = 1,
    Move = 2,
    Outside = 3,
    Judgment = 4,
};

enum class RefSub : int {
    None = 0,
    Walk = 1,
    MoveToPosition = 2,
    PinCount = 3,
    GiveUp = 4,
    TagBreak = 5,
    Judgment = 6,
};

class Referee : public Actor {
public:
    void setup(const GameData *data, const RingGeometry *ring, int max_count);

    void tick(float dt, Match &match);
    void onMatchFinished(const Match &match);

    RefMaster master() const { return master_; }
    RefSub sub() const { return topState(); }
    int stateDepth() const { return stack_depth_; }
    int count() const { return count_; }
    int maxCount() const { return max_count_; }

    // Exact 0..13 index into graphics/Sprites/Character/Ref/refData.sml.
    int spriteId() const { return sprite_id_; }
    int animId() const { return anim_id_; }

    // Physical pin-count queries used to keep simulation and UI strictly locked:
    //   * approachingPin(): walking toward the cover, still standing, count == 0
    //   * droppedToCanvas(): physically adjacent to cover and on knees/counting
    //   * showingPinCount(): only true AFTER the first physical mat-slap (count >= 1)
    //   * pinAwarded(): true once the 3rd slap finishes and Judgment begins
    bool approachingPin() const {
        return master_ == RefMaster::Move && topState() == RefSub::MoveToPosition &&
               hasQueuedSub(RefSub::PinCount);
    }
    bool droppedToCanvas() const {
        return master_ == RefMaster::Move && topState() == RefSub::PinCount && canvas_dropped_;
    }
    bool counting() const {
        return master_ == RefMaster::Move &&
               (topState() == RefSub::PinCount ||
                (topState() == RefSub::Judgment && count_ > 0)) &&
               canvas_dropped_ && count_ >= 1;
    }
    bool showingPinCount() const { return counting(); }
    float countPopScale() const;
    bool pinAwarded() const { return pin_awarded_; }

    // HandleBePinned gate (WrestleFest Premium.c:17440): the pinned wrestler's
    // kick-out mash only runs once the referee arrives at the cover and drops
    // onto the canvas to count.
    bool watchingPinFor(int wrestler) const {
        return master_ == RefMaster::Move && topState() == RefSub::PinCount &&
               canvas_dropped_ && watched_actor_ == wrestler;
    }
    int watchedActor() const { return watched_actor_; }
    float targetX() const { return target_x_; }
    float targetZ() const { return target_z_; }
    float reactionDelay() const { return reaction_delay_; }

private:
    RefSub topState() const { return sub_stack_[static_cast<std::size_t>(stack_depth_)]; }
    bool hasQueuedSub(RefSub target) const;
    void setBaseSub(RefSub base);
    void pushMoveToAction(RefSub action, float tx, float tz);
    void popSub();

    bool stepTowardTarget(float dt);
    void computeWatchTarget(const Match &match, float *out_x, float *out_z, float *out_focus_x) const;
    void computePinTarget(const Match &match, const Wrestler &pinned, float *out_x,
                          float *out_z) const;
    void scanActionTriggers(Match &match);
    void setRefAnim(int anim_id, bool loop, float rate = 1.0f);
    int advanceRefAnim(float dt);

    const GameData *data_ = nullptr;
    const RingGeometry *ring_ = nullptr;
    RefMaster master_ = RefMaster::Watch;
    std::array<RefSub, 4> sub_stack_{RefSub::None, RefSub::None, RefSub::None, RefSub::None};
    int stack_depth_ = 0;

    float target_x_ = 480.0f;
    float target_z_ = 410.0f;
    int anim_id_ = 0;
    float anim_time_ = 0.0f;
    float anim_rate_ = 1.0f;
    bool anim_loop_ = true;
    bool anim_finished_ = false;
    int sprite_id_ = 0;

    int count_ = 0;
    int max_count_ = 3;
    float drop_timer_ = 0.0f;
    float pop_timer_ = 0.0f;
    bool canvas_dropped_ = false;
    bool pin_awarded_ = false;
    float count_timer_ = 0.0f;
    float reaction_delay_ = 0.0f;
    int watched_actor_ = -1;
};

// Pre-match ring announcer ("Robert" / AnnouncerAT.sml). Runs a linear,
// non-snapping intro sequence: center-ring idle -> announce left wrestler ->
// announce right wrestler -> walk cleanly out to the upper apron before bell.
enum class AnnouncerCue : int {
    Entrance = 0,
    AnnounceLeft = 1,
    AnnounceRight = 2,
    FightExit = 3,
    Hidden = 4,
};

class RingAnnouncer : public Actor {
public:
    void setup(const GameData *data, const RingGeometry *ring);
    void setCue(AnnouncerCue cue);
    void tick(float dt, const Match &match);

    bool visible() const { return visible_; }
    float alpha() const { return alpha_; }
    int spriteId() const { return sprite_id_; }
    AnnouncerCue cue() const { return cue_; }

private:
    void setAnim(int anim_id, bool loop);
    void advanceAnim(float dt);

    const GameData *data_ = nullptr;
    const RingGeometry *ring_ = nullptr;
    AnnouncerCue cue_ = AnnouncerCue::Hidden;
    bool visible_ = false;
    float alpha_ = 1.0f;
    float target_x_ = 480.0f;
    float target_z_ = 368.0f;
    int anim_id_ = 0;
    float anim_time_ = 0.0f;
    bool anim_loop_ = true;
    int sprite_id_ = 0;
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_SIM_REFEREE_H
