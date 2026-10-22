#include "referee.h"

#include "match.h"

#include <algorithm>
#include <cmath>

namespace wrestlefest {
namespace {

// RefereeAT.sml animation table IDs.
constexpr int kRefAnimIdle = 0;       // [0]
constexpr int kRefAnimWalk = 1;       // [0,0,1,1,2,2,3,3]
constexpr int kRefAnimCountdown = 2;  // [5x10, 6x6]
constexpr int kRefAnimGiveUp = 3;     // [7,7,7,8,8,8]
constexpr int kRefAnimJudgment = 4;   // [9,9,9,9,9,10,10,11,11]
constexpr int kRefAnimWarn = 5;       // [12,12,12,13,13,13]

// Explicit sprite indices inside refData.sml.
constexpr int kRefSpriteIdle = 0;
constexpr int kRefSpriteCanvasDrop = 4;  // countdown (1).png - kneeling drop onto mat
constexpr int kRefSpriteMatSlap = 6;     // countdown (3).png - palm flat on canvas

// Ring margin so the referee never clips over the ropes at the edge of the mat.
constexpr float kRefRopeMarginX = 28.0f;
constexpr float kRefRopeMarginZ = 18.0f;

// RefereeAT Countdown is 16 frames (0.533s at 30 fps); running it at 0.68x
// produces a natural ~0.78s pro-wrestling cadence per count ("1... 2... 3!").
constexpr float kRefCountdownRate = 0.68f;
constexpr float kRefJudgmentRate = 0.65f;

// AnnouncerAT.sml animation table IDs (6 rows: 0..5).
constexpr int kAnnouncerAnimIdle = 0;      // [0] announcer (6).png
constexpr int kAnnouncerAnimBow = 1;       // [1] announcer (1).png
constexpr int kAnnouncerAnimTalk = 2;      // [2x4, 3x4] speaking into microphone
constexpr int kAnnouncerAnimAnnounce = 3;  // [4] arm sweep toward superstar
constexpr int kAnnouncerAnimWalk = 4;      // [5x4, 6x4, 7x4, 8x4] walk cycle
constexpr int kAnnouncerAnimRopeOut = 5;   // [9x4, 10x4, 11x4, 12x4] climb through ropes

}  // namespace

// ===========================================================================
// Referee
// ===========================================================================

void Referee::setup(const GameData *data, const RingGeometry *ring, int max_count) {
    data_ = data;
    ring_ = ring;
    max_count_ = std::max(1, max_count);
    master_ = RefMaster::Watch;
    sub_stack_ = {RefSub::None, RefSub::None, RefSub::None, RefSub::None};
    stack_depth_ = 0;
    count_ = 0;
    drop_timer_ = 0.0f;
    pop_timer_ = 0.0f;
    canvas_dropped_ = false;
    pin_awarded_ = false;
    count_timer_ = 0.0f;
    reaction_delay_ = 0.0f;
    watched_actor_ = -1;
    if (ring != nullptr) {
        x = ring->centerX() + 72.0f;
        z = ring->top_z + 28.0f;
        target_x_ = x;
        target_z_ = z;
    }
    facing = -1.0f;
    setRefAnim(kRefAnimIdle, true);
    sprite_id_ = kRefSpriteIdle;
}

float Referee::countPopScale() const {
    if (pop_timer_ <= 0.0f) return 1.0f;
    const float t = std::clamp(pop_timer_ / 0.30f, 0.0f, 1.0f);
    return 1.0f + 0.28f * t;
}

bool Referee::hasQueuedSub(RefSub target) const {
    for (int i = 0; i <= stack_depth_; ++i) {
        if (sub_stack_[static_cast<std::size_t>(i)] == target) return true;
    }
    return false;
}

void Referee::setBaseSub(RefSub base) {
    stack_depth_ = 0;
    sub_stack_[0] = base;
    for (int i = 1; i < 4; ++i) sub_stack_[static_cast<std::size_t>(i)] = RefSub::None;
}

void Referee::pushMoveToAction(RefSub action, float tx, float tz) {
    stack_depth_ = 1;
    sub_stack_[0] = action;
    sub_stack_[1] = RefSub::MoveToPosition;
    sub_stack_[2] = RefSub::None;
    sub_stack_[3] = RefSub::None;
    if (ring_ != nullptr) {
        target_z_ = std::clamp(tz, ring_->top_z + 10.0f, ring_->bottom_z - 10.0f);
        target_x_ = std::clamp(tx, ring_->leftX(target_z_) + kRefRopeMarginX,
                               ring_->rightX(target_z_) - kRefRopeMarginX);
    } else {
        target_x_ = tx;
        target_z_ = tz;
    }
    setRefAnim(kRefAnimWalk, true);
}

void Referee::popSub() {
    if (stack_depth_ > 0) {
        sub_stack_[static_cast<std::size_t>(stack_depth_)] = RefSub::None;
        --stack_depth_;
    } else {
        sub_stack_[0] = RefSub::None;
    }
}

void Referee::setRefAnim(int anim_id, bool loop, float rate) {
    if (anim_id_ == anim_id && anim_loop_ == loop && std::fabs(anim_rate_ - rate) < 1.0e-3f &&
        !anim_finished_) {
        return;
    }
    anim_id_ = anim_id;
    anim_time_ = 0.0f;
    anim_rate_ = rate;
    anim_loop_ = loop;
    anim_finished_ = false;
    if (data_ != nullptr) {
        const AnimInfo *anim = data_->refereeAnimInfo(anim_id_);
        if (anim != nullptr && anim->frame_count > 0 && anim->frames[0] >= 0) {
            sprite_id_ = anim->frames[0];
        }
    }
}

int Referee::advanceRefAnim(float dt) {
    const int prev_sprite = sprite_id_;
    if (data_ == nullptr) return prev_sprite;
    const AnimInfo *anim = data_->refereeAnimInfo(anim_id_);
    if (anim == nullptr || anim->frame_count <= 0) return prev_sprite;

    const float duration = static_cast<float>(anim->frame_count) / tuning::kSourceFps;
    anim_time_ += dt * anim_rate_;
    if (duration > 0.0f && anim_time_ >= duration) {
        if (anim_loop_) {
            anim_time_ = std::fmod(anim_time_, duration);
            anim_finished_ = false;
        } else {
            anim_time_ = duration;
            anim_finished_ = true;
        }
    }

    int frame = static_cast<int>(anim_time_ * tuning::kSourceFps);
    frame = std::clamp(frame, 0, anim->frame_count - 1);
    const int next_sprite = anim->frames[static_cast<std::size_t>(frame)];
    if (next_sprite >= 0) sprite_id_ = next_sprite;
    return prev_sprite;
}

bool Referee::stepTowardTarget(float dt) {
    const float dx = target_x_ - x;
    const float dz = target_z_ - z;
    const bool arrived_x = std::fabs(dx) <= tuning::kRefArriveEpsilon;
    const bool arrived_z = std::fabs(dz) <= tuning::kRefArriveEpsilon;
    if (arrived_x && arrived_z) {
        x = target_x_;
        z = target_z_;
        if (ring_ != nullptr) {
            z = ring_->clampZ(z);
            x = ring_->clampX(z, x);
        }
        return true;
    }

    if (!arrived_x) {
        const float step_x = std::min(std::fabs(dx), tuning::kRefWalkSpeedX * dt);
        x += (dx >= 0.0f ? 1.0f : -1.0f) * step_x;
        if (std::fabs(dx) > 1.5f) facing = dx >= 0.0f ? 1.0f : -1.0f;
    }
    if (!arrived_z) {
        const float step_z = std::min(std::fabs(dz), tuning::kRefWalkSpeedZ * dt);
        z += (dz >= 0.0f ? 1.0f : -1.0f) * step_z;
    }

    if (ring_ != nullptr) {
        z = ring_->clampZ(z);
        x = ring_->clampX(z, x);
    }
    return std::fabs(target_x_ - x) <= tuning::kRefArriveEpsilon &&
           std::fabs(target_z_ - z) <= tuning::kRefArriveEpsilon;
}

void Referee::computeWatchTarget(const Match &match, float *out_x, float *out_z,
                                 float *out_focus_x) const {
    if (ring_ == nullptr) {
        *out_x = x;
        *out_z = z;
        *out_focus_x = x;
        return;
    }

    float min_x = 1.0e9f;
    float max_x = -1.0e9f;
    float min_z = 1.0e9f;
    float max_z = -1.0e9f;
    int in_ring_count = 0;

    for (int i = 0; i < match.wrestlerCount(); ++i) {
        const Wrestler *w = match.wrestler(i);
        if (w == nullptr || !w->active || w->isDead()) continue;
        if (w->region != region::IN_RING && w->region != region::IN_RING_ALONE) continue;
        min_x = std::min(min_x, w->x);
        max_x = std::max(max_x, w->x);
        min_z = std::min(min_z, w->z);
        max_z = std::max(max_z, w->z);
        ++in_ring_count;
    }

    if (in_ring_count == 0) {
        *out_z = ring_->top_z + 34.0f;
        *out_x = ring_->centerX();
        *out_focus_x = ring_->centerX();
        return;
    }

    // Faithful to FindTargetPosition (WrestleFest Premium.c:5769-5865): pick the
    // vertical side of the ring with more open canvas so the referee orbits the
    // action rather than standing between the combatants.
    const float top_room = min_z - ring_->top_z;
    const float bottom_room = ring_->bottom_z - max_z;
    float desired_z = ring_->top_z + kRefRopeMarginZ;
    if (top_room >= bottom_room - 10.0f) {
        desired_z = ring_->top_z + std::max(kRefRopeMarginZ, top_room * 0.35f);
    } else {
        desired_z = ring_->bottom_z - std::max(kRefRopeMarginZ, bottom_room * 0.35f);
    }
    desired_z = std::clamp(desired_z, ring_->top_z + kRefRopeMarginZ,
                           ring_->bottom_z - kRefRopeMarginZ);

    const float mid_x = 0.5f * (min_x + max_x);
    *out_focus_x = mid_x;

    // Slight lateral offset toward whichever side has more room so the referee
    // frames the action cleanly at a 3/4 angle instead of eclipsing a wrestler.
    const float left_edge = ring_->leftX(desired_z) + kRefRopeMarginX;
    const float right_edge = ring_->rightX(desired_z) - kRefRopeMarginX;
    const float room_left = mid_x - left_edge;
    const float room_right = right_edge - mid_x;
    const float flank = (room_right >= room_left ? 1.0f : -1.0f) * 52.0f;
    *out_z = desired_z;
    *out_x = std::clamp(mid_x + flank, left_edge, right_edge);
}

void Referee::computePinTarget(const Match &match, const Wrestler &pinned, float *out_x,
                               float *out_z) const {
    const float desired_z = ring_ != nullptr
                                ? std::clamp(pinned.z + 8.0f, ring_->top_z + 12.0f,
                                             ring_->bottom_z - 12.0f)
                                : pinned.z;
    const Wrestler *pinner = match.wrestler(pinned.opponentId());
    const float anchor_x = pinner != nullptr ? 0.5f * (pinner->x + pinned.x) : pinned.x;
    const float pref_dir = pinner != nullptr ? (pinner->facing >= 0.0f ? 1.0f : -1.0f) : 1.0f;

    const float cand_primary = anchor_x - pref_dir * tuning::kRefPinOffsetX;
    const float cand_secondary = anchor_x + pref_dir * tuning::kRefPinOffsetX;
    *out_z = desired_z;

    if (ring_ == nullptr) {
        *out_x = cand_primary;
        return;
    }

    const float lo = ring_->leftX(desired_z) + kRefRopeMarginX;
    const float hi = ring_->rightX(desired_z) - kRefRopeMarginX;
    const bool primary_inside = cand_primary >= lo && cand_primary <= hi;
    const bool secondary_inside = cand_secondary >= lo && cand_secondary <= hi;

    if (primary_inside && secondary_inside) {
        // Choose whichever valid side requires less running across the pile.
        *out_x = std::fabs(cand_primary - x) <= std::fabs(cand_secondary - x) ? cand_primary
                                                                               : cand_secondary;
    } else if (primary_inside) {
        *out_x = cand_primary;
    } else if (secondary_inside) {
        *out_x = cand_secondary;
    } else {
        *out_x = std::clamp(cand_primary, lo, hi);
    }
}

void Referee::scanActionTriggers(Match &match) {
    // 1. Highest priority: grounded or cradle pinfall in progress.
    if (match.rule().can_pin) {
        for (int i = 0; i < match.wrestlerCount(); ++i) {
            Wrestler *w = match.wrestler(i);
            if (w == nullptr || w->isDead() || !w->pinned) continue;
            watched_actor_ = i;
            count_ = 0;
            drop_timer_ = 0.0f;
            pop_timer_ = 0.0f;
            canvas_dropped_ = false;
            pin_awarded_ = false;
            master_ = RefMaster::Move;
            float tx = w->x;
            float tz = w->z;
            computePinTarget(match, *w, &tx, &tz);
            pushMoveToAction(RefSub::PinCount, tx, tz);
            return;
        }
    }

    // 2. Submission hold: referee runs up to ask the trapped man if he gives up.
    if (match.rule().can_submission) {
        for (int i = 0; i < match.wrestlerCount(); ++i) {
            Wrestler *w = match.wrestler(i);
            if (w == nullptr || w->isDead()) continue;
            if (w->subState() == state::BE_USED_S_WS) {
                watched_actor_ = i;
                count_ = 0;
                canvas_dropped_ = false;
                pin_awarded_ = false;
                master_ = RefMaster::Move;
                const float tz = ring_ != nullptr
                                     ? std::min(ring_->bottom_z - 12.0f, w->z + 28.0f)
                                     : w->z + 28.0f;
                const float tx = w->x - (w->facing >= 0.0f ? 1.0f : -1.0f) * 76.0f;
                pushMoveToAction(RefSub::GiveUp, tx, tz);
                return;
            }
        }
    }

    // 3. Rule enforcement: wrestler outside the ropes -> referee walks over to
    //    the near rope and administers the warning count.
    for (int i = 0; i < match.wrestlerCount(); ++i) {
        Wrestler *w = match.wrestler(i);
        if (w == nullptr || w->isDead() || !w->active) continue;
        if (w->region == region::OUTSIDE) {
            watched_actor_ = i;
            master_ = RefMaster::Move;
            const float tz = ring_ != nullptr ? ring_->clampZ(w->z) : w->z;
            const float tx = ring_ != nullptr
                                 ? (w->x < ring_->centerX() ? ring_->leftX(tz) + kRefRopeMarginX
                                                            : ring_->rightX(tz) - kRefRopeMarginX)
                                 : w->x;
            pushMoveToAction(RefSub::TagBreak, tx, tz);
            return;
        }
    }
}

void Referee::onMatchFinished(const Match &match) {
    master_ = RefMaster::Judgment;
    setBaseSub(RefSub::Judgment);
    canvas_dropped_ = false;
    const Wrestler *winner = match.wrestler(match.winnerIndex());
    if (winner != nullptr && std::fabs(winner->x - x) > 1.0f) {
        facing = winner->x >= x ? 1.0f : -1.0f;
    }
    setRefAnim(kRefAnimJudgment, false, kRefJudgmentRate);
}

void Referee::tick(float dt, Match &match) {
    if (!match.rule().have_referee) return;

    if (pop_timer_ > 0.0f) pop_timer_ = std::max(0.0f, pop_timer_ - dt);

    // Pre-match introduction: hold a steady, linear standing pose in the upper
    // ring without snapping or teleporting across animation states.
    if (match.phase() == MatchPhase::Intro) {
        master_ = RefMaster::Watch;
        setBaseSub(RefSub::None);
        count_ = 0;
        canvas_dropped_ = false;
        pin_awarded_ = false;
        setRefAnim(kRefAnimIdle, true);
        advanceRefAnim(dt);
        sprite_id_ = kRefSpriteIdle;
        return;
    }

    // Post-match bell/decision: play Ref Judgment once linearly and hold the
    // raised-arm bell pose until the result card transitions away.
    if (match.phase() == MatchPhase::Finished) {
        if (master_ != RefMaster::Judgment) {
            onMatchFinished(match);
        }
        advanceRefAnim(dt);
        return;
    }

    switch (master_) {
        case RefMaster::Idle:
            master_ = RefMaster::Watch;
            setBaseSub(RefSub::None);
            setRefAnim(kRefAnimIdle, true);
            break;

        case RefMaster::Watch: {
            watched_actor_ = -1;
            count_ = 0;
            canvas_dropped_ = false;
            pin_awarded_ = false;

            // Immediate rule check on every tick so pinfalls, submissions, and
            // outside-ring violations never miss a frame before the ref reacts.
            scanActionTriggers(match);
            if (master_ != RefMaster::Watch) break;

            float desired_x = x;
            float desired_z = z;
            float focus_x = x;
            computeWatchTarget(match, &desired_x, &desired_z, &focus_x);

            // Check if an active combatant walked right into the referee's lane.
            bool crowded = false;
            for (int i = 0; i < match.wrestlerCount(); ++i) {
                const Wrestler *w = match.wrestler(i);
                if (w == nullptr || !w->active || w->isDead()) continue;
                if (std::fabs(w->x - x) < 62.0f && std::fabs(w->z - z) < 34.0f) {
                    crowded = true;
                    break;
                }
            }

            if (topState() == RefSub::Walk) {
                target_x_ = desired_x;
                target_z_ = desired_z;
                setRefAnim(kRefAnimWalk, true);
                advanceRefAnim(dt);
                if (stepTowardTarget(dt)) {
                    setBaseSub(RefSub::None);
                    if (std::fabs(focus_x - x) > 1.0f) facing = focus_x >= x ? 1.0f : -1.0f;
                    setRefAnim(kRefAnimIdle, true);
                    sprite_id_ = kRefSpriteIdle;
                }
            } else {
                if (std::fabs(focus_x - x) > 2.0f) facing = focus_x >= x ? 1.0f : -1.0f;
                const bool far_x = std::fabs(desired_x - x) >= tuning::kRefTrackDeadbandX;
                const bool far_z = std::fabs(desired_z - z) >= tuning::kRefTrackDeadbandZ;
                if (crowded || far_x || far_z) {
                    target_x_ = desired_x;
                    target_z_ = desired_z;
                    setBaseSub(RefSub::Walk);
                    setRefAnim(kRefAnimWalk, true);
                    stepTowardTarget(dt);
                    advanceRefAnim(dt);
                } else {
                    setRefAnim(kRefAnimIdle, true);
                    advanceRefAnim(dt);
                    sprite_id_ = kRefSpriteIdle;
                }
            }
            break;
        }

        case RefMaster::Move: {
            Wrestler *target = match.wrestler(watched_actor_);
            if (target == nullptr || target->isDead()) {
                master_ = RefMaster::Watch;
                setBaseSub(RefSub::None);
                setRefAnim(kRefAnimIdle, true);
                sprite_id_ = kRefSpriteIdle;
                break;
            }

            // Validate the active action before walking or counting further:
            // if a pin cover breaks early, return cleanly to active Watch!
            if (hasQueuedSub(RefSub::PinCount) && !target->pinned &&
                topState() != RefSub::Judgment) {
                count_ = 0;
                canvas_dropped_ = false;
                pin_awarded_ = false;
                master_ = RefMaster::Watch;
                setBaseSub(RefSub::None);
                setRefAnim(kRefAnimIdle, true);
                sprite_id_ = kRefSpriteIdle;
                break;
            }
            if (hasQueuedSub(RefSub::GiveUp) &&
                target->subState() != state::BE_USED_S_WS && !target->play_dead) {
                master_ = RefMaster::Watch;
                setBaseSub(RefSub::None);
                setRefAnim(kRefAnimIdle, true);
                sprite_id_ = kRefSpriteIdle;
                break;
            }
            if (hasQueuedSub(RefSub::TagBreak) && target->region != region::OUTSIDE) {
                master_ = RefMaster::Watch;
                setBaseSub(RefSub::None);
                setRefAnim(kRefAnimIdle, true);
                sprite_id_ = kRefSpriteIdle;
                break;
            }

            // Keep action destination locked to moving wrestlers while approaching.
            if (topState() == RefSub::MoveToPosition) {
                if (hasQueuedSub(RefSub::PinCount)) {
                    computePinTarget(match, *target, &target_x_, &target_z_);
                }
                setRefAnim(kRefAnimWalk, true);
                advanceRefAnim(dt);
                if (stepTowardTarget(dt)) {
                    popSub();
                    facing = target->x >= x ? 1.0f : -1.0f;
                    drop_timer_ = 0.0f;
                    if (topState() == RefSub::PinCount) {
                        // Drop onto knees first (countdown (1).png, index 4) before
                        // raising the arm for the first manual count.
                        canvas_dropped_ = false;
                        sprite_id_ = kRefSpriteCanvasDrop;
                    } else if (topState() == RefSub::GiveUp) {
                        setRefAnim(kRefAnimGiveUp, true, 0.75f);
                    } else if (topState() == RefSub::TagBreak) {
                        setRefAnim(kRefAnimWarn, true, 0.75f);
                    }
                }
                break;
            }

            if (topState() == RefSub::PinCount) {
                facing = target->x >= x ? 1.0f : -1.0f;
                if (!canvas_dropped_) {
                    sprite_id_ = kRefSpriteCanvasDrop;
                    drop_timer_ += dt;
                    if (drop_timer_ >= tuning::kRefCanvasDropTime) {
                        canvas_dropped_ = true;
                        setRefAnim(kRefAnimCountdown, true, kRefCountdownRate);
                    }
                    break;
                }

                // Manual mat-slap count loop (RefereeAT ID 2: [5x10, 6x6]).
                // Increment count & trigger UI/SFX strictly on the 5 -> 6 slap edge!
                if (count_ < max_count_) {
                    const int prev_sprite = advanceRefAnim(dt);
                    if (prev_sprite != kRefSpriteMatSlap && sprite_id_ == kRefSpriteMatSlap) {
                        ++count_;
                        pop_timer_ = 0.30f;
                        match.emit({EventType::RefCount, watched_actor_, target->opponentId(),
                                    static_cast<float>(count_), state::PINNING});
                        drop_timer_ = 0.0f;
                    }
                } else {
                    // Hold the 3rd palm slap visibly on the mat before standing up
                    // into Ref Judgment to call for the bell.
                    sprite_id_ = kRefSpriteMatSlap;
                    drop_timer_ += dt;
                    if (drop_timer_ >= 0.18f) {
                        pin_awarded_ = true;
                        setBaseSub(RefSub::Judgment);
                        setRefAnim(kRefAnimJudgment, false, kRefJudgmentRate);
                    }
                }
                break;
            }

            if (topState() == RefSub::GiveUp) {
                facing = target->x >= x ? 1.0f : -1.0f;
                if (target->play_dead) {
                    setBaseSub(RefSub::Judgment);
                    setRefAnim(kRefAnimJudgment, false, kRefJudgmentRate);
                } else {
                    setRefAnim(kRefAnimGiveUp, true, 0.75f);
                    advanceRefAnim(dt);
                }
                break;
            }

            if (topState() == RefSub::TagBreak) {
                facing = target->x >= x ? 1.0f : -1.0f;
                setRefAnim(kRefAnimWarn, true, 0.75f);
                advanceRefAnim(dt);
                break;
            }

            if (topState() == RefSub::Judgment) {
                advanceRefAnim(dt);
                break;
            }
            break;
        }

        case RefMaster::Outside: {
            setRefAnim(kRefAnimWarn, true, 0.75f);
            advanceRefAnim(dt);
            count_timer_ += dt;
            if (count_timer_ >= 1.0f) {
                count_timer_ -= 1.0f;
                if (count_ < match.rule().max_outside_count) {
                    ++count_;
                    match.emit({EventType::RefCount, -1, -1, static_cast<float>(count_),
                                state::CLIMBOUT});
                }
            }
            break;
        }

        case RefMaster::Judgment:
            advanceRefAnim(dt);
            break;
    }
}

// ===========================================================================
// RingAnnouncer
// ===========================================================================

void RingAnnouncer::setup(const GameData *data, const RingGeometry *ring) {
    data_ = data;
    ring_ = ring;
    cue_ = AnnouncerCue::Hidden;
    visible_ = false;
    alpha_ = 1.0f;
    sprite_id_ = 0;
    if (ring != nullptr) {
        x = ring->centerX();
        z = ring->centerZ() - 18.0f;
        target_x_ = x;
        target_z_ = ring->top_z + 6.0f;
    }
    facing = 1.0f;
    setAnim(kAnnouncerAnimIdle, true);
}

void RingAnnouncer::setAnim(int anim_id, bool loop) {
    if (anim_id_ == anim_id && anim_loop_ == loop) return;
    anim_id_ = anim_id;
    anim_time_ = 0.0f;
    anim_loop_ = loop;
    if (data_ != nullptr) {
        const AnimInfo *anim = data_->announcerAnimInfo(anim_id_);
        if (anim != nullptr && anim->frame_count > 0 && anim->frames[0] >= 0) {
            sprite_id_ = anim->frames[0];
        }
    }
}

void RingAnnouncer::advanceAnim(float dt) {
    if (data_ == nullptr) return;
    const AnimInfo *anim = data_->announcerAnimInfo(anim_id_);
    if (anim == nullptr || anim->frame_count <= 0) return;
    const float duration = static_cast<float>(anim->frame_count) / tuning::kSourceFps;
    anim_time_ += dt * 0.75f;
    if (duration > 0.0f && anim_time_ >= duration) {
        if (anim_loop_) {
            anim_time_ = std::fmod(anim_time_, duration);
        } else {
            anim_time_ = duration;
        }
    }
    int frame = static_cast<int>(anim_time_ * tuning::kSourceFps);
    frame = std::clamp(frame, 0, anim->frame_count - 1);
    const int next_sprite = anim->frames[static_cast<std::size_t>(frame)];
    if (next_sprite >= 0) sprite_id_ = next_sprite;
}

void RingAnnouncer::setCue(AnnouncerCue cue) {
    cue_ = cue;
    switch (cue) {
        case AnnouncerCue::Entrance:
            visible_ = true;
            alpha_ = 1.0f;
            facing = 1.0f;
            if (ring_ != nullptr) {
                x = ring_->centerX();
                z = ring_->centerZ() - 18.0f;
            }
            setAnim(kAnnouncerAnimTalk, true);
            break;
        case AnnouncerCue::AnnounceLeft:
            // announcer (9).png extends his arm to the left in native sprite space,
            // so facing = +1 (no flip) points straight at Corner 1 on the left!
            visible_ = true;
            alpha_ = 1.0f;
            facing = 1.0f;
            setAnim(kAnnouncerAnimAnnounce, false);
            break;
        case AnnouncerCue::AnnounceRight:
            visible_ = true;
            alpha_ = 1.0f;
            facing = -1.0f;
            anim_id_ = -1;
            setAnim(kAnnouncerAnimAnnounce, false);
            break;
        case AnnouncerCue::FightExit:
            visible_ = true;
            if (ring_ != nullptr) {
                target_x_ = ring_->centerX();
                target_z_ = ring_->top_z + 8.0f;
            }
            setAnim(kAnnouncerAnimWalk, true);
            break;
        case AnnouncerCue::Hidden:
            visible_ = false;
            alpha_ = 0.0f;
            break;
    }
}

void RingAnnouncer::tick(float dt, const Match &match) {
    if (match.phase() != MatchPhase::Intro || cue_ == AnnouncerCue::Hidden) {
        visible_ = false;
        alpha_ = 0.0f;
        return;
    }

    if (cue_ == AnnouncerCue::FightExit) {
        const float dz = target_z_ - z;
        if (std::fabs(dz) > 2.0f) {
            z += (dz >= 0.0f ? 1.0f : -1.0f) * std::min(std::fabs(dz), 130.0f * dt);
        } else {
            setAnim(kAnnouncerAnimRopeOut, false);
            alpha_ = std::max(0.0f, alpha_ - dt * 1.8f);
            if (alpha_ <= 0.02f) {
                visible_ = false;
            }
        }
    }
    advanceAnim(dt);
}

}  // namespace wrestlefest
