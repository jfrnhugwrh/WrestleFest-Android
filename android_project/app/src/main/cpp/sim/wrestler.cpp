#include "wrestler.h"

#include "match.h"

#include <algorithm>
#include <cmath>

namespace wrestlefest {
namespace {

float animDuration(const GameData &data, int anim_id) {
    const AnimInfo *anim = data.animInfo(anim_id);
    if (anim == nullptr || anim->frame_count <= 0) return 0.15f;
    return static_cast<float>(anim->frame_count) / tuning::kSourceFps;
}

float clampf(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }

bool isClinchState(int s) {
    return s == state::GRAPPLE_IDLE || s == state::GRAPPLE_HIGH || s == state::GRAPPLE_LOW ||
           s == state::HEADLOCK || s == state::HEADLOCKED;
}

}  // namespace

// ---------------------------------------------------------------------------
// Actor meter model
// ---------------------------------------------------------------------------
void Actor::applyDamage(float energy_damage, float motion_damage) {
    // SetEnergyDamage: snap the display to the pre-hit value, then subtract.
    energy_display = energy;
    energy = clampf(energy - energy_damage, energy_floor, energy_max);
    if (energy_display <= 0.0f) energy_display = energy;
    if (motion_damage != 0.0f) addMotionPower(-motion_damage);  // MDamage is signed
}

void Actor::tickEnergyDisplay(float dt) {
    // CWrestler::Tick. The bar only ever lags *behind a drop*: it drains at
    // 10/s toward the real value and is clamped up instantly when healing.
    if (energy_display > energy) {
        energy_display += dt * tuning::kEnergyBarDrainRate;
    }
    if (energy_display < energy) energy_display = energy;
    if (energy_display < 0.0f) energy_display = 0.0f;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void Wrestler::setup(const WrestlerProfile *profile, int id, int team_id, const GameData *data,
                     const RingGeometry *ring) {
    profile_ = profile;
    data_ = data;
    ring_ = ring;
    id_ = id;
    team = team_id;
    strength_ = profile != nullptr ? profile->strength() : 1;

    if (profile != nullptr) {
        energy_max = profile->base.energy;
        motion_power_max = profile->base.motion_power;
        powerup_max = profile->base.max_power_up_value;
    }
    energy = energy_max;
    energy_display = energy_max;
    motion_power = motion_power_max;
    powerup_value = 0.0f;
    state_stack[0] = state::IDLE;
    state_depth = 0;
    region = region::IN_RING;
    anim_id_ = 0;
    entrance_anim_index_ = 0;
    neutral_combo_hits = 0;
    clinch_timer = 0.0f;
    clinch_strikes = 0;
}

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
void Wrestler::changeState(int new_state, int chain, Match &match) {
    if (new_state <= 0) new_state = state::IDLE;
    const int prev_state = subState();

    if (chain == 0) {
        state_depth = 0;
        state_stack[0] = new_state;
    } else if (state_depth + 1 < tuning::kStateStackDepth) {
        ++state_depth;
        state_stack[static_cast<std::size_t>(state_depth)] = new_state;
    } else {
        // Stack overflow: replace the top instead of asserting (the original
        // asserts in debug only).
        state_stack[static_cast<std::size_t>(state_depth)] = new_state;
    }

    if (!isClinchState(new_state)) {
        clinch_timer = 0.0f;
        clinch_strikes = 0;
    } else if (!isClinchState(prev_state)) {
        clinch_timer = 0.0f;
        clinch_strikes = 0;
    }

    if (new_state != state::PUNCH && new_state != state::KICK && new_state != state::IDLE &&
        new_state != state::STAND_TIRED && new_state != state::WALK) {
        neutral_combo_hits = 0;
    }

    if (new_state == tuning::kNoState) {
        anim_time = 0.0f;
        return;
    }

    const StateInfo *info = data_ != nullptr ? data_->stateInfo(new_state) : nullptr;
    if (info != nullptr) {
        // AnimId >= 0 multiplies the facing by the animation's Facing.
        if (info->anim_id >= 0) {
            const AnimInfo *anim = data_->animInfo(info->anim_id);
            if (anim != nullptr) {
                if (anim->facing != 0.0f) facing = (facing >= 0.0f ? 1.0f : -1.0f) * anim->facing;
                anim_id_ = info->anim_id;
                duration_ = animDuration(*data_, info->anim_id);
                // AdjustOpponentXZ: paired animations share one origin, so the
                // opponent is re-seated at anim.xOffset in front of the actor.
                // A man who is out of the match (thrown over the top rope, or
                // already posed for the result screen) is left where he fell.
                if (anim->x_offset != 0.0f) {
                    Wrestler *other = match.wrestler(opponent_);
                    if (other != nullptr && !other->isDead() && !isDead()) {
                        other->x = x + facing * anim->x_offset * tuning::kPairedOffsetScale;
                        if (other->profile_ != nullptr && other->ring_ != nullptr) {
                            other->x = other->ring_->clampX(other->z, other->x);
                        }
                    }
                }
            }
        }
        // OpponentState from StateTable.sml drives the paired state. The guard
        // against re-entering keeps self-paired rows (DOGFIGHT_STANDS -> 44)
        // from recursing between the two wrestlers.
        if (info->opponent_state != state::kNoState) {
            Wrestler *other = match.wrestler(opponent_);
            if (other != nullptr && other != this &&
                other->subState() != info->opponent_state) {
                other->changeState(info->opponent_state, chain, match);
            }
        }
    }

    // StateTable's generic ENTRANCE row points at animation 197, whose frame
    // IDs are deliberately empty for some wrestlers (Cena's resolve to the
    // shared none_sprite.sst placeholder). Use the wrestler-specific sequence
    // from EntranceInfo instead whenever it is available.
    if (new_state == state::ENTRANCE) {
        entrance_anim_index_ = 0;
        if (profile_ != nullptr && data_ != nullptr && !profile_->entrance.anims.empty()) {
            const int entrance_anim = profile_->entrance.anims.front();
            if (data_->animInfo(entrance_anim) != nullptr) {
                anim_id_ = entrance_anim;
                duration_ = animDuration(*data_, entrance_anim);
            }
        }
    }

    anim_time = 0.0f;
    state_timer = 0.0f;
    anim_finished_ = false;
    move_hit_applied = false;
    hurt_done_ = false;
    used_move_this_state_ = false;
    // A pending strike/move impact must not survive into an unrelated state -
    // otherwise an interrupted move lands its damage frames later, on whoever
    // happens to be close (one of the "random hit" animation desyncs).
    impact_time_ = -1.0f;
    hurt_timing_ = -1.0f;
    // NOTE: pinned/held are owned by Match - clearing them here would break a
    // pin the moment BE_PINNED is entered.
    button_counter_active = false;
    button_count = 0;

    match.emit({EventType::EnergyMeter, id_, opponent_, 0.0f, new_state});
}

void Wrestler::nextState(Match &match) {
    if (state_depth <= 0) {
        changeState(state::IDLE, 0, match);
        return;
    }
    --state_depth;
    state_stack[static_cast<std::size_t>(state_depth + 1)] = 0;
    anim_time = 0.0f;
    state_timer = 0.0f;
    anim_finished_ = false;
    impact_time_ = -1.0f;
    hurt_timing_ = -1.0f;
    const int current = subState();
    const StateInfo *info = data_ != nullptr ? data_->stateInfo(current) : nullptr;
    if (info != nullptr && info->anim_id >= 0) {
        anim_id_ = info->anim_id;
        duration_ = animDuration(*data_, info->anim_id);
    }
}

bool Wrestler::isDown() const {
    const int s = subState();
    return s == state::ONGROUND || s == state::SLAMMED || s == state::BE_PINNED ||
           s == state::ONGROUND_HURT || s == state::KNOCKED_BACK || s == state::BE_USED_F_WS ||
           s == state::BE_USED_RFWS;
}

bool Wrestler::isBusy() const {
    const int s = subState();
    if (s == state::IDLE || s == state::WALK || s == state::RUN || s == state::TAUNT ||
        s == state::STAND_TIRED) {
        return false;
    }
    return true;
}

bool Wrestler::canAct() const {
    if (play_dead || isDead()) return false;
    const int s = subState();
    return s == state::IDLE || s == state::WALK || s == state::RUN || s == state::STAND_TIRED;
}

bool Wrestler::isInClinch() const { return isClinchState(subState()); }

void Wrestler::knockOut(Match &match) {
    region = region::KNOCKED_OUT;
    energy = 0.0f;
    pinned = false;
    held = false;
    in_submission_ = false;
    play_dead = true;
    active = false;
    changeState(state::LOSE, 0, match);
    match.emit({EventType::KnockedOut, id_, opponent_, 0.0f, state::LOSE});
}

int Wrestler::spriteId() const {
    if (profile_ == nullptr || data_ == nullptr) return 0;
    const AnimInfo *anim = data_->animInfo(anim_id_);
    if (anim == nullptr) return 0;
    const int frame_count = std::max(1, anim->frame_count);
    int frame = static_cast<int>(anim_time * tuning::kSourceFps);
    frame = std::clamp(frame, 0, frame_count - 1);
    const int sst = anim->frames[static_cast<std::size_t>(frame)];
    if (sst >= 0) return sst;
    return anim->frames[0] >= 0 ? anim->frames[0] : 0;
}

float Wrestler::animFraction() const {
    if (duration_ <= 0.0f) return 1.0f;
    return clampf(anim_time / duration_, 0.0f, 1.0f);
}

bool Wrestler::loopsAnimation() const {
    // Only true idle/locomotion holds repeat their clip; directional grapple
    // stances (GRAPPLE_HIGH/LOW), headlocks (HEADLOCK/HEADLOCKED), and dogfight
    // follow-ups must reach anim_finished_ so their transitions cannot freeze!
    const int s = subState();
    return s == state::IDLE || s == state::WALK || s == state::RUN ||
           s == state::STAND_TIRED || s == state::TAUNT || s == state::GRAPPLE_IDLE ||
           s == state::DOGFIGHT_STANDS || s == state::PINNING || s == state::BE_PINNED ||
           s == state::VICTORY || s == state::LOSE ||
           (s == state::ENTRANCE &&
            (profile_ == nullptr || profile_->entrance.anims.empty()));
}

bool Wrestler::canLeaveRing() const {
    // The ropes only stop a man who is on his feet. A body that has been
    // thrown, whipped or knocked into them goes over the top.
    const int s = subState();
    if (flying_out) return true;
    if (s == state::KNOCKED_BACK || s == state::SLAMMED || s == state::ROPEFLING ||
        s == state::IRISH_WHIPPED || s == state::AIRBORNE || s == state::PUSHOFF ||
        s == state::BE_USED_F_WS || s == state::BE_USED_RFWS || s == state::BE_USED_RAM_WS) {
        return true;
    }
    return region == region::OUTSIDE || region == region::RINGSIDE;
}

float Wrestler::flyOutSpeed(const Match &match) const {
    return match.common().airborne_factor_x * tuning::kFlyOutSpeedFactor;
}

void Wrestler::startFlyOut(float dir, Match &match) {
    if (isDead()) return;
    flying_out = true;
    fly_dir = dir >= 0.0f ? 1.0f : -1.0f;
    whip_bounced = false;
    pinned = false;
    held = false;
    // CRoyalRumbleController::FallOut -> CWrestler::ChangeState(actor, 14):
    // the man tumbles over the rope and lands on the floor at ringside.
    changeState(state::SLAMMED, 0, match);
}

void Wrestler::tickFlyOut(float dt, Match &match) {
    if (!flying_out) return;
    x += fly_dir * flyOutSpeed(match) * dt;
    if (ring_ != nullptr) x = ring_->clampXArena(z, x);
}

void Wrestler::advanceAnimation(float dt) {
    anim_time += dt;
    if (duration_ <= 0.0f) return;
    if (anim_time < duration_) return;
    if (loopsAnimation()) {
        // Wrap instead of latching, so the cycle keeps running.
        anim_time = std::fmod(anim_time, duration_);
        anim_finished_ = false;
    } else {
        anim_finished_ = true;
    }
}

// ---------------------------------------------------------------------------
// Power-up (AutoReplyPowerUpValue / TickPowerUp / PowerUp)
// ---------------------------------------------------------------------------
void Wrestler::updatePowerUp(float dt, Match &match, bool can_power_up) {
    if (!can_power_up || profile_ == nullptr) return;
    const float duration = profile_->base.max_power_up_duration;
    const float interval = profile_->base.max_power_up_interval;

    if (!powered_up) {
        powerup_value = std::min(powerup_max, powerup_value + dt * profile_->base.heal_pups);
        if (powerup_value < powerup_max) return;
        if (game_clock_ <= powerup_started_at_ + interval) return;
        powered_up = true;
        powerup_started_at_ = game_clock_;
        match.emit({EventType::Impact, id_, opponent_, 1.0f, state::IDLE});
    } else if (game_clock_ > powerup_started_at_ + duration) {
        powerup_value = std::max(powerup_value - powerup_max, 0.0f);
        powered_up = false;
        powerup_started_at_ = game_clock_;
        match.emit({EventType::Impact, id_, opponent_, 0.0f, state::IDLE});
    }
}

void Wrestler::addPowerUpBonus(float amount) {
    if (powered_up || amount <= 0.0f) return;  // AutoReplyPowerUpValue guard
    powerup_value = std::clamp(powerup_value + amount, 0.0f, powerup_max);
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------
void Wrestler::tick(float dt, Match &match, const InputFrame &input) {
    setGameClock(match.clock());
    tickEnergyDisplay(dt);

    const bool alive = !play_dead && region != region::KNOCKED_OUT;
    if (alive && match.phase() == MatchPhase::Running) {
        // HealEPS / HealMPPS regeneration. MotionPower only regenerates while
        // tired when the wrestler is below the table's TiredValue.
        if (profile_ != nullptr && !powered_up) {
            addEnergy(dt * profile_->base.heal_eps);
            const bool tired = energy < profile_->base.tired_value;
            if (!tired) addMotionPower(dt * profile_->base.heal_mps);
        }
        updatePowerUp(dt, match, match.rule().can_power_up);
    }

    if (pinned) {
        // A pinned wrestler only runs the mash counter.
        tickPinStates(dt, match, input);
        advanceAnimation(dt);
        return;
    }

    tickState(dt, match, input);
    advanceAnimation(dt);
}

void Wrestler::tickState(float dt, Match &match, const InputFrame &input) {
    state_timer += dt;
    const int s = subState();
    switch (s) {
        case state::IDLE:
        case state::WALK:
        case state::RUN:
        case state::STAND_TIRED:
        case state::TAUNT:
            tickLocomotion(dt, match, input);
            break;
        case state::ENTRANCE:
            tickEntrance(match);
            break;
        case state::PUNCH:
        case state::KICK:
            tickStrike(dt, match);
            break;
        case state::HITREACT_MID:
        case state::HITREACT_HIGH:
        case state::SLAMMED:
        case state::KNOCKED_BACK:
        case state::PUSHOFF:
        case state::ROPEFLING:
        case state::BOUNCE_OFF_ROPE:
            tickReaction(dt, match);
            break;
        case state::ONGROUND:
        case state::ONGROUND_HURT:
            tickGround(dt, match, input);
            break;
        case state::IRISH_WHIP:
        case state::IRISH_WHIPPED:
            tickWhip(dt, match);
            break;
        case state::GRAPPLE_IDLE:
        case state::GRAPPLE_HIGH:
        case state::GRAPPLE_LOW:
        case state::HEADLOCK:
        case state::HEADLOCKED:
            tickGrapple(dt, match, input);
            break;
        case state::DOGFIGHT_STANDS:
        case state::DOGFIGHT_HIGHER:
        case state::DOGFIGHT_LOWER:
        case state::DOGFIGHT_PUNCH:
        case state::DOGFIGHT_PUNCHED:
            tickDogfight(dt, match);
            break;
        case state::JUMP_FOR_PIN:
        case state::PIN:
        case state::PINNING:
        case state::BE_PINNED:
            tickPinStates(dt, match, input);
            break;
        case state::USE_S_WS:
        case state::BE_USED_S_WS:
            tickSubmission(dt, match, input);
            break;
        case state::USE_F_WS_PIN:
        case state::BE_USED_F_WS_PIN:
            // Cradle moves (FISHERMAN_SUPLEX / PERFECT_PLEX): the throw itself
            // becomes the cover - the pin system owns this pair from here.
            tickPinStates(dt, match, input);
            break;
        case state::BE_USED_F_WS:
        case state::BE_USED_RFWS:
        case state::BE_USED_CL_HANG:
        case state::BE_USED_CL_DROP:
        case state::BE_USED_RAM_WS:
        case state::LO_TH_PRESSED:
        case state::PICKUPED:
            // The attacker owns these states: the victim must NOT self-advance
            // (BE_USED_* clips are often shorter than the attacker's, so the
            // old default popped the victim to idle in the middle of a throw).
            tickVictimHold(dt, match);
            break;
        case state::STANDUP:
        case state::PICKUP:
        case state::CLIMBTOP:
        case state::CLIMBIN:
        case state::CLIMBOUT:
        case state::TAG_IN:
        case state::TAG_OUT:
        case state::VICTORY:
        case state::LOSE:
            // Simple, self-timed states: play the animation then return to idle.
            if (anim_finished_) {
                if (s == state::VICTORY || s == state::LOSE) {
                    // Hold the pose; the match flow decides when to leave.
                } else {
                    changeState(state::IDLE, state_depth > 0 ? 1 : 0, match);
                    if (state_depth > 0) nextState(match);
                }
            }
            break;
        default:
            // USE_*_WS and everything else driven from a move table.
            tickMoveStates(dt, match);
            break;
    }
}

// ---------------------------------------------------------------------------
// Entrance animation sequence
// ---------------------------------------------------------------------------
void Wrestler::tickEntrance(Match &match) {
    (void)match;
    if (!anim_finished_ || profile_ == nullptr || data_ == nullptr ||
        profile_->entrance.anims.empty()) {
        return;
    }

    // Play EntranceInfo linearly from index 0 through the final ready pose
    // without modulo-wrapping back to index 0 (which previously snapped the
    // wrestler abruptly from ready stance back into his opening taunt).
    const std::size_t count = profile_->entrance.anims.size();
    while (entrance_anim_index_ + 1 < count) {
        ++entrance_anim_index_;
        const int next_id = profile_->entrance.anims[entrance_anim_index_];
        const AnimInfo *next = data_->animInfo(next_id);
        if (next == nullptr || next->frame_count <= 0) continue;
        anim_id_ = next_id;
        duration_ = animDuration(*data_, next_id);
        anim_time = 0.0f;
        anim_finished_ = false;
        return;
    }

    // Hold cleanly on the final frame of the last valid entrance clip.
    anim_time = duration_;
    anim_finished_ = false;
}

// ---------------------------------------------------------------------------
// Movement + attack selection
// ---------------------------------------------------------------------------
void Wrestler::moveInput(float dt, const InputFrame &input) {
    float speed_x = profile_ != nullptr ? profile_->base.walk_factor_x : 315.0f;
    float speed_z = profile_ != nullptr ? profile_->base.walk_factor_z : 270.0f;
    const bool running = subState() == state::RUN;
    const bool tired = profile_ != nullptr && energy < profile_->base.tired_value;

    if (running) {
        speed_x = profile_ != nullptr ? profile_->base.run_factor_x : 600.0f;
    } else if (tired) {
        speed_x *= profile_ != nullptr ? profile_->base.speed_down_factor_tired : 0.3f;
        speed_z *= profile_ != nullptr ? profile_->base.speed_down_factor_tired : 0.3f;
    }

    // HandleAutoWalk clamps a step to 20 px/frame at 30 fps.
    speed_x = std::min(speed_x, tuning::kMaxWalkStepPerSecond);

    const int dx = input.dirX();
    const int dz = input.dirZ();
    if (dx != 0) {
        x += static_cast<float>(dx) * speed_x * dt;
        if (!isBusy()) facing = dx > 0 ? 1.0f : -1.0f;
    }
    if (dz != 0) z += static_cast<float>(dz) * speed_z * dt;

    if (ring_ != nullptr) {
        z = ring_->clampZ(z);
        if (region == region::IN_RING || region == region::IN_RING_ALONE) {
            x = ring_->clampX(z, x);
        }
    }
}

void Wrestler::tickLocomotion(float dt, Match &match, const InputFrame &input) {
    const int previous = subState();
    moveInput(dt, input);

    // Desired locomotion state. Moving without input returns to IDLE.
    int target = state::IDLE;
    const bool tired = profile_ != nullptr && energy < profile_->base.tired_value;
    const bool moving = input.dirX() != 0 || input.dirZ() != 0;
    if (moving) {
        // In the original a double-tap starts RUN and holding button 0 keeps it;
        // keeping RUN while the stick is held reproduces both.
        target = (previous == state::RUN || input.btn0) ? state::RUN : state::WALK;
    } else if (tired) {
        target = state::STAND_TIRED;
    }

    // CWrestler::HandleIdle: when standing without stick input, keep facing the
    // active opponent so close-quarters strikes never punch into empty air.
    if (!moving && (target == state::IDLE || target == state::STAND_TIRED)) {
        const Wrestler *other = match.wrestler(opponent_);
        if (other != nullptr && other->active && !other->isDead() &&
            std::fabs(other->x - x) > 1.0f) {
            facing = other->x >= x ? 1.0f : -1.0f;
        }
    }

    if (target != previous) {
        changeState(target, 0, match);
    }

    if (input.anyButtonEdge() && canAct()) {
        tryStartAttack(match, input);
    }
}

float Wrestler::collisionRange(int state_id, float fallback) const {
    if (data_ == nullptr) return fallback;
    const StateInfo *info = data_->stateInfo(state_id);
    if (info == nullptr || info->collision_range_x < 0.0f) return fallback;
    return info->collision_range_x;
}

float Wrestler::stateSnapOffset(int state_id, float fallback) const {
    if (data_ == nullptr) return fallback * tuning::kPairedOffsetScale;
    const StateInfo *info = data_->stateInfo(state_id);
    if (info == nullptr || info->anim_id < 0) return fallback * tuning::kPairedOffsetScale;
    const AnimInfo *anim = data_->animInfo(info->anim_id);
    if (anim == nullptr || anim->x_offset == 0.0f) return fallback * tuning::kPairedOffsetScale;
    return anim->x_offset * tuning::kPairedOffsetScale;
}

void Wrestler::tryStartAttack(Match &match, const InputFrame &input) {
    Wrestler *other = match.wrestler(opponent_);
    if (profile_ == nullptr || other == nullptr || play_dead) return;
    if (!other->active || other->isDead() || other->team == team) return;

    // Snap facing toward the opponent if they are standing right in front/on top
    // of us before resolving close-quarters attacks.
    if (std::fabs(other->x - x) > 1.0f && input.dirX() == 0) {
        facing = other->x >= x ? 1.0f : -1.0f;
    }

    const float range_z = match.common().collision_range_z;
    const bool opponent_down = other->isDown();

    // Direction + attack = grapple. Allow initiating a clinch against free OR
    // hit-stunned standing targets so strikes combo cleanly into throws instead
    // of trapping the target in un-grappleable hit-stun!
    const bool grappling = input.dirX() != 0 || input.dirZ() != 0;
    const float grapple_range = collisionRange(state::GRAPPLE_IDLE, 100.0f);
    const bool opponent_grappleable =
        !opponent_down && !other->pinned && !other->held &&
        (!other->isBusy() || other->subState() == state::HITREACT_MID ||
         other->subState() == state::HITREACT_HIGH);
    if (grappling && opponent_grappleable &&
        canStrikeConnect(x, z, facing, other->x, other->z, grapple_range, range_z)) {
        startGrapple(match, input.dirZ() != 0);  // up/down = high/low grapple
        return;
    }

    // The opponent is down (HandleOpponentOnGround). Blue = cover him for the
    // pin; red = ground attack (stomp/elbow). Both need to be in the ring.
    if (opponent_down) {
        const float ground_range = collisionRange(state::USE_F_WS, grapple_range);
        if (canStrikeConnect(x, z, facing, other->x, other->z, ground_range, range_z)) {
            if (input.btn1_edge) {
                if (match.rule().can_pin && pinWindowOpen(*other, match)) {
                    startPin(match);
                } else {
                    // Not pinnable: lift him back up (PICKUP / PICKUPED pair).
                    changeState(state::PICKUP, 0, match);
                    other->facing = -facing;
                    other->opponent_ = id_;
                }
                return;
            }
            const StatusSlot &slot = profile_->status[3];
            for (int i = 0; i < slot.list_count && i < 4; ++i) {
                const int id = slot.attack[static_cast<std::size_t>(i)];
                if (id < 0) continue;
                const MoveInfo *move = data_->moveInfo(id);
                if (move == nullptr) continue;
                beginMove(id);
                useWrestleStyle(*move, match);
                return;
            }
        }
        return;
    }

    // Running attack (status slot 0). When out of reach of the running move the
    // press falls through to a plain strike instead of being eaten.
    if (subState() == state::RUN) {
        const StatusSlot &slot = profile_->status[0];
        const float run_range = collisionRange(state::RUN, grapple_range + 20.0f);
        for (int i = 0; i < slot.list_count && i < 4; ++i) {
            const int id = slot.attack[static_cast<std::size_t>(i)];
            if (id < 0) continue;
            const MoveInfo *move = data_->moveInfo(id);
            if (move == nullptr || move->energy_damage <= 0) continue;
            if (!canStrikeConnect(x, z, facing, other->x, other->z, run_range, range_z)) break;
            beginMove(id);
            useWrestleStyle(*move, match);
            return;
        }
    }

    startStrike(input.btn1_edge ? state::KICK : state::PUNCH, match);
}

bool Wrestler::pinWindowOpen(const Wrestler &victim, const Match &match) const {
    if (profile_ == nullptr || victim.profile() == nullptr || data_ == nullptr) return false;
    if (isDead() || play_dead || region != region::IN_RING) return false;
    if (victim.region != region::IN_RING) return false;
    if (victim.isDead() || victim.held || victim.pinned) return false;
    if (!victim.isDown()) return false;
    // The original's pin gate: the victim must be at or under his GBH energy
    // threshold (BaseInfo, 24) or the cover just picks him up instead.
    if (victim.energy > static_cast<float>(victim.profile()->base.gbh_value)) return false;
    const float range_x = collisionRange(state::PINNING, 100.0f);
    const float range_z = match.common().collision_range_z;
    return canReach(x, z, facing, victim.x, victim.z, range_x, range_z, false);
}

void Wrestler::startPin(Match &match) {
    Wrestler *other = match.wrestler(opponent_);
    if (other == nullptr) return;
    setOpponent(other->id());
    other->setOpponent(id_);
    if (other->x != x) facing = other->x > x ? 1.0f : -1.0f;
    // Seat the victim at the pin animation's origin (AdjustOpponentXZ), then
    // dive: JUMP_FOR_PIN -> PIN -> PINNING run chained in tickPinStates.
    if (other->subState() != state::ONGROUND && other->subState() != state::BE_PINNED) {
        other->changeState(state::ONGROUND, 0, match);
    }
    changeState(state::JUMP_FOR_PIN, 0, match);
    other->x = ring_ != nullptr
                   ? ring_->clampX(z, x + facing * stateSnapOffset(state::JUMP_FOR_PIN, 40.0f))
                   : x + facing * stateSnapOffset(state::JUMP_FOR_PIN, 40.0f);
    other->z = z;
    match.emit({EventType::Impact, id_, opponent_, 1.0f, state::JUMP_FOR_PIN});
}

void Wrestler::startStrike(int strike_state, Match &match) {
    changeState(strike_state, 0, match);
    const MoveInfo *move = data_ != nullptr ? data_->moveInfo(strike_state == state::KICK ? 1 : 0)
                                            : nullptr;
    move_id = move != nullptr ? move->id : -1;
    impact_time_ = duration_ * tuning::kStrikeImpactFraction;
    move_hit_applied = false;
    match.emit({EventType::Impact, id_, opponent_, 0.0f, strike_state});
}

void Wrestler::startGrapple(Match &match, bool high) {
    // GRAPPLE_IDLE (6) is the shared entry; GRAPPLE_HIGH/LOW are the stances
    // entered from it by the state table's OpponentState.
    changeState(state::GRAPPLE_IDLE, 0, match);
    Wrestler *other = match.wrestler(opponent_);
    if (other != nullptr) {
        other->facing = -facing;
        other->opponent_ = id_;
        // Seat the pair at WrestlerAT's exact engage xOffset; if clamping against
        // a rope pulls the opponent back, transfer the remainder back to self so
        // the two sprites never overlap at the turnbuckles!
        const float snap = stateSnapOffset(state::GRAPPLE_IDLE, 166.0f);
        float desired_other_x = x + facing * snap;
        if (ring_ != nullptr) {
            const float clamped = ring_->clampX(z, desired_other_x);
            const float remainder = desired_other_x - clamped;
            if (std::fabs(remainder) > 0.001f) {
                x = ring_->clampX(z, x - remainder);
            }
            desired_other_x = clamped;
        }
        other->x = desired_other_x;
        other->z = z;
        if (high) changeState(state::GRAPPLE_HIGH, 1, match);
    }
    match.emit({EventType::GrappleEngaged, id_, opponent_, 0.0f, subState()});
}

void Wrestler::beginMove(int move) {
    move_id = move;
    const MoveInfo *info = data_ != nullptr ? data_->moveInfo(move) : nullptr;
    move_sub_type = info != nullptr ? info->sub_type_id : 0;
    move_hit_applied = false;
    used_move_this_state_ = true;
}

// ---------------------------------------------------------------------------
// Strikes
// ---------------------------------------------------------------------------
void Wrestler::tickStrike(float dt, Match &match) {
    (void)dt;
    if (!move_hit_applied && impact_time_ >= 0.0f && anim_time >= impact_time_) {
        move_hit_applied = true;
        Wrestler *other = match.wrestler(opponent_);
        const float range_z = match.common().collision_range_z;
        const float strike_range = collisionRange(subState(), 110.0f);
        const MoveInfo *move = data_ != nullptr ? data_->moveInfo(move_id) : nullptr;
        if (other != nullptr && move != nullptr && !other->isDead() &&
            // Punches/kicks only connect against a standing target (the moves'
            // AttackPosition 0); a downed man is attacked with the ground
            // moves, never stood back up by a jab.
            !other->isDown() && !other->isInClinch() &&
            canStrikeConnect(x, z, facing, other->x, other->z, strike_range, range_z)) {
            const float energy_damage = static_cast<float>(move->energy_damage);
            const float motion_damage = static_cast<float>(move->motion_damage);
            other->applyDamage(energy_damage, motion_damage);
            other->facing = -facing;

            // Physical strike recoil: push the struck wrestler back slightly,
            // transferring any rope-blocked remainder back onto the attacker so
            // corner strikes never compress two models into the same X coordinate.
            const float push = facing * 12.0f;
            float next_other_x = other->x + push;
            if (ring_ != nullptr &&
                (other->region == region::IN_RING || other->region == region::IN_RING_ALONE)) {
                const float clamped = ring_->clampX(other->z, next_other_x);
                const float blocked = next_other_x - clamped;
                next_other_x = clamped;
                if (std::fabs(blocked) > 0.001f) {
                    x = ring_->clampX(z, x - blocked);
                }
            }
            other->x = next_other_x;

            ++neutral_combo_hits;
            if (neutral_combo_hits >= tuning::kMaxNeutralComboHits || other->energy <= 0.0f) {
                neutral_combo_hits = 0;
                other->changeState(state::SLAMMED, 0, match);
                match.emit({EventType::KnockDown, other->id(), id_, energy_damage, state::SLAMMED});
            } else {
                other->changeState(
                    subState() == state::KICK ? state::HITREACT_HIGH : state::HITREACT_MID, 0,
                    match);
            }

            addMotionPower(2.0f);
            if (profile_ != nullptr) addPowerUpBonus(profile_->base.power_up_bonus);
            match.emit({EventType::Hit, id_, opponent_, energy_damage, subState()});
        } else {
            neutral_combo_hits = 0;
        }
    }
    if (anim_finished_) changeState(state::IDLE, 0, match);
}

// ---------------------------------------------------------------------------
// Reactions / ground
// ---------------------------------------------------------------------------
void Wrestler::tickReaction(float dt, Match &match) {
    tickFlyOut(dt, match);
    if (flying_out) {
        // The tumble over the rope keeps going until Match says he has landed;
        // the clip itself is short, so hold it rather than snapping to the mat.
        if (anim_finished_) anim_time = duration_;
        return;
    }
    (void)dt;
    if (anim_finished_) {
        if (subState() == state::SLAMMED || subState() == state::KNOCKED_BACK) {
            changeState(state::ONGROUND, 0, match);
        } else {
            changeState(state::IDLE, 0, match);
        }
    }
}

void Wrestler::tickGround(float dt, Match &match, const InputFrame &input) {
    (void)dt;
    if (play_dead) return;  // a given-up/KO'd man does not get back up
    // Mash to get up (ButtonCountTable "ONGROUND" row); a downed wrestler below
    // 10 energy needs 100 presses, i.e. cannot get up.
    if (!button_counter_active) {
        button_counter_active = true;
        button_count = 0;
        button_goal = data_ != nullptr ? data_->buttonCountGoal(state::ONGROUND, energy, 0) : 4;
    }
    if (input.anyButtonEdge()) button_count += input.buttonEdges();

    const float max_ground = profile_ != nullptr ? profile_->base.max_on_ground_time_max : 8.0f;
    const bool reached = button_goal > 0 && button_count >= button_goal;
    if (reached || state_timer >= max_ground) {
        changeState(state::STANDUP, 0, match);
        match.emit({EventType::KickOut, id_, opponent_, 0.0f, state::STANDUP});
    }
}

// ---------------------------------------------------------------------------
// Irish whip
// ---------------------------------------------------------------------------
void Wrestler::tickWhip(float dt, Match &match) {
    tickFlyOut(dt, match);
    if (flying_out) {
        if (anim_finished_) anim_time = duration_;
        return;
    }
    const bool whipped = subState() == state::IRISH_WHIPPED;
    // The whipped wrestler runs away from the attacker and bounces off the ropes.
    const float speed = match.common().airborne_factor_x * 0.6f;
    if (whipped) {
        x += facing * speed * dt;
        if (ring_ != nullptr) {
            const bool at_rope = (facing < 0.0f && x <= ring_->leftX(z) + 2.0f) ||
                                 (facing > 0.0f && x >= ring_->rightX(z) - 2.0f);
            if (at_rope && !whip_bounced) {
                whip_bounced = true;
                facing = -facing;
                changeState(state::ROPEFLING, 0, match);
                match.emit({EventType::RopeBounce, id_, opponent_, 0.0f, subState()});
                return;
            }
            x = ring_->clampX(z, x);
        }
    }
    if (anim_finished_ && !whipped) {
        changeState(state::IDLE, 0, match);
    } else if (anim_finished_ && whip_bounced) {
        // Coming back from the ropes: end up next to the opponent and idle.
        changeState(state::IDLE, 0, match);
        whip_bounced = false;
    }
}

// ---------------------------------------------------------------------------
// Grapple / dogfight
// ---------------------------------------------------------------------------
void Wrestler::tickGrapple(float dt, Match &match, const InputFrame &input) {
    Wrestler *other = match.wrestler(opponent_);
    if (other == nullptr || other->isDead() || !other->isInClinch()) {
        changeState(state::IDLE, 0, match);
        return;
    }

    // Monotonic clinch watchdog shared across every clinch/headlock sub-state:
    // never reset by beat rolls, so no clinch can ever stall combat indefinitely.
    clinch_timer += dt;
    if (clinch_timer >= tuning::kMaxClinchDuration) {
        changeState(state::PUSHOFF, 0, match);
        other->changeState(state::PUSHOFF, 0, match);
        return;
    }

    const int s = subState();
    if (s == state::GRAPPLE_IDLE || s == state::GRAPPLE_HIGH || s == state::GRAPPLE_LOW) {
        if (input.anyButtonEdge() && profile_ != nullptr && data_ != nullptr) {
            const bool has_dir = input.dirX() != 0 || input.dirZ() != 0;
            if (has_dir && state_timer >= 0.14f) {
                // Directional press inside the clinch selects a grapple throw
                // from StatusSlot[1] (up=1, down=2, left=3, right=0).
                const StatusSlot &slot = profile_->status[1];
                int preferred = 0;
                if (input.dirZ() < 0) {
                    preferred = 1;
                } else if (input.dirZ() > 0) {
                    preferred = 2;
                } else if (input.dirX() < 0) {
                    preferred = 3;
                }
                for (int attempt = 0; attempt < 4; ++attempt) {
                    const int idx = (preferred + attempt) % 4;
                    if (idx >= slot.list_count) continue;
                    const int cand_id = slot.attack[static_cast<std::size_t>(idx)];
                    if (cand_id < 0) continue;
                    const MoveInfo *move = data_->moveInfo(cand_id);
                    if (move == nullptr) continue;
                    beginMove(cand_id);
                    useWrestleStyle(*move, match);
                    match.emit({EventType::GrappleWon, id_, other->id(), 0.0f, subState()});
                    return;
                }
            } else if (!has_dir) {
                // Neutral button press in a grapple hurries the grapple beat and,
                // on a realistic 0.25s cadence, delivers a clinch blow capped at
                // kMaxClinchStrikes so neutral clinch strikes never loop forever!
                state_timer += tuning::kGrappleMashStep;
                if (clinch_timer >= static_cast<float>(clinch_strikes + 1) * 0.25f) {
                    const MoveInfo *strike = data_->moveInfo(0);
                    const float dmg =
                        strike != nullptr ? static_cast<float>(strike->energy_damage) : 2.0f;
                    const float mdmg =
                        strike != nullptr ? static_cast<float>(strike->motion_damage) : 1.0f;
                    other->applyDamage(dmg, mdmg);
                    addPowerUpBonus(profile_->base.power_up_bonus * 0.5f);
                    match.emit({EventType::Hit, id_, other->id(), dmg, s});
                    ++clinch_strikes;
                    if (clinch_strikes >= tuning::kMaxClinchStrikes || other->energy <= 0.0f) {
                        changeState(state::IDLE, 0, match);
                        if (!match.attemptRumbleToss(*this, *other, facing)) {
                            other->changeState(state::SLAMMED, 0, match);
                        }
                        match.emit(
                            {EventType::KnockDown, other->id(), id_, dmg, state::SLAMMED});
                        return;
                    }
                }
            } else {
                state_timer += tuning::kGrappleMashStep;
            }
        }

        if (s != state::GRAPPLE_IDLE && anim_finished_) {
            changeState(state::PUSHOFF, 0, match);
            other->changeState(state::PUSHOFF, 0, match);
        }
        return;
    }

    if (anim_finished_) {
        changeState(state::IDLE, 0, match);
        if (other->isInClinch()) other->changeState(state::IDLE, 0, match);
    }
}

void Wrestler::tickDogfight(float dt, Match &match) {
    (void)dt;
    if (subState() == state::DOGFIGHT_STANDS) {
        // Safety fallback in case opponent vanished mid-clash.
        if (state_timer > tuning::kDogfightBeat * 3.0f) {
            changeState(state::IDLE, 0, match);
        }
        return;
    }
    // The follow-up beats (punch / being punched / higher / lower) resolve once
    // their clip finishes.
    if (anim_finished_) changeState(state::IDLE, 0, match);
}

// ---------------------------------------------------------------------------
// Move (USE_*_WS) resolution
// ---------------------------------------------------------------------------
void Wrestler::useWrestleStyle(const MoveInfo &move, Match &match) {
    // TypeID is translated through kMoveFamilyToEntryState - never used raw.
    const int entry = move.entry_state;
    changeState(entry, 0, match);

    Wrestler *other = match.wrestler(opponent_);
    const int sub_type = move.sub_type_id;

    switch (entry) {
        case state::PUNCH:
        case state::KICK: {
            impact_time_ = duration_ * tuning::kStrikeImpactFraction;
            break;
        }
        case state::USE_F_WS: {
            const ForceMove *force = data_->forceMove(sub_type);
            if (force != nullptr) {
                const AnimInfo *anim =
                    force->anim_id >= 0 ? data_->animInfo(force->anim_id) : nullptr;
                if (anim != nullptr) {
                    if (anim->facing != 0.0f) {
                        facing = (facing >= 0.0f ? 1.0f : -1.0f) * anim->facing;
                    }
                    anim_id_ = force->anim_id;
                    duration_ = animDuration(*data_, force->anim_id);
                }
                if (other != nullptr) {
                    other->changeState(state::BE_USED_F_WS, 0, match);
                    other->opponent_ = id_;
                    if (anim != nullptr) {
                        if (force->adjust_opponent) {
                            float desired_x =
                                x + facing * anim->x_offset * tuning::kPairedOffsetScale;
                            if (ring_ != nullptr) {
                                const float clamped = ring_->clampX(z, desired_x);
                                const float rem = desired_x - clamped;
                                if (std::fabs(rem) > 0.001f) x = ring_->clampX(z, x - rem);
                                desired_x = clamped;
                            }
                            other->x = desired_x;
                            other->z = z;
                        }
                        if (anim->opposing_facing != 0.0f) {
                            other->facing = (facing >= 0.0f ? 1.0f : -1.0f) * anim->opposing_facing;
                        }
                        const AnimInfo *victim_anim = data_->animInfo(force->opponent_anim_id);
                        if (victim_anim != nullptr && victim_anim->facing != 0.0f) {
                            other->facing =
                                (other->facing >= 0.0f ? 1.0f : -1.0f) * victim_anim->facing;
                        }
                        if (force->opponent_anim_id >= 0) {
                            other->anim_id_ = force->opponent_anim_id;
                            other->duration_ = animDuration(*data_, force->opponent_anim_id);
                            other->anim_time = 0.0f;
                            other->anim_finished_ = false;
                        }
                    } else {
                        other->facing = -facing;
                    }
                }
                hurt_timing_ = force->hurt_opponent_timing >= 0.0f
                                   ? force->hurt_opponent_timing
                                   : duration_ * tuning::kForceImpactFraction;
            }
            break;
        }
        case state::USE_S_WS: {
            const SubmissionMove *sub = data_->submissionMove(sub_type);
            if (sub != nullptr && other != nullptr) {
                anim_id_ = sub->anim_id >= 0 ? sub->anim_id : anim_id_;
                duration_ = animDuration(*data_, anim_id_);
                other->changeState(state::BE_USED_S_WS, 0, match);
                other->facing = -facing;
                other->opponent_ = id_;
                other->move_id = move_id;
                other->move_sub_type = sub_type;
                other->in_submission_ = true;
                other->held = true;
                const AnimInfo *anim = data_->animInfo(anim_id_);
                if (anim != nullptr && anim->x_offset != 0.0f) {
                    other->x = ring_ != nullptr ? ring_->clampX(z, x + facing * anim->x_offset)
                                                : x + facing * anim->x_offset;
                }
                other->z = z;
                if (sub->opponent_anim_id >= 0) {
                    other->anim_id_ = sub->opponent_anim_id;
                    other->duration_ = animDuration(*data_, sub->opponent_anim_id);
                }
                hurt_timing_ = sub->sec_per_hurt;
                match.emit({EventType::SubmissionStarted, id_, opponent_, 0.0f, state::USE_S_WS});
            }
            break;
        }
        case state::USE_CL_WS: {
            const ClotheslineMove *cl = data_->clotheslineMove(sub_type);
            if (cl != nullptr && cl->begin_anim_id >= 0) {
                anim_id_ = cl->begin_anim_id;
                duration_ = animDuration(*data_, anim_id_);
            }
            if (other != nullptr) {
                other->changeState(state::BE_USED_CL_HANG, 0, match);
                other->facing = -facing;
                other->opponent_ = id_;
            }
            hurt_timing_ = duration_ * tuning::kForceImpactFraction;
            break;
        }
        case state::USE_RAM_WS: {
            const RamMove *ram = data_->ramMove(sub_type);
            if (ram != nullptr && ram->anim_id >= 0) {
                anim_id_ = ram->anim_id;
                duration_ = animDuration(*data_, ram->anim_id);
            }
            if (other != nullptr) {
                other->changeState(ram != nullptr ? ram->opponent_rammed_state
                                                  : state::BE_USED_RAM_WS,
                                   0, match);
                other->facing = -facing;
                other->opponent_ = id_;
            }
            hurt_timing_ = duration_ * tuning::kForceImpactFraction;
            break;
        }
        case state::USE_HT_WS: {
            const HitTargetMove *ht = data_->hitTargetMove(sub_type);
            if (ht != nullptr && ht->anim_id >= 0) {
                anim_id_ = ht->anim_id;
                duration_ = animDuration(*data_, ht->anim_id);
            }
            hurt_timing_ = duration_ * tuning::kForceImpactFraction;
            break;
        }
        case state::USE_TB_WS: {
            const TurnbuckleMove *tb = data_->turnbuckleMove(sub_type);
            if (tb != nullptr && tb->anim_id >= 0) {
                anim_id_ = tb->anim_id;
                duration_ = animDuration(*data_, tb->anim_id);
                hurt_timing_ = tb->attack_timing > 0.0f ? tb->attack_timing
                                                        : duration_ * tuning::kForceImpactFraction;
            }
            break;
        }
        case state::USE_SB_WS: {
            const SpringboardMove *sb = data_->springboardMove(sub_type);
            if (sb != nullptr && sb->anim_id >= 0) {
                anim_id_ = sb->anim_id;
                duration_ = animDuration(*data_, sb->anim_id);
            }
            hurt_timing_ = duration_ * tuning::kForceImpactFraction;
            break;
        }
        case state::USE_RFWS_RUSH:
        case state::USE_RFWS: {
            const RushForceMove *rf = data_->rushForceMove(sub_type);
            if (rf != nullptr && rf->act_anim_id >= 0) {
                anim_id_ = rf->act_anim_id;
                duration_ = animDuration(*data_, rf->act_anim_id);
            }
            if (other != nullptr) {
                other->changeState(state::BE_USED_RFWS, 0, match);
                other->facing = -facing;
                other->opponent_ = id_;
                other->z = z;
                if (rf != nullptr && rf->opponent_anim_id >= 0) {
                    other->anim_id_ = rf->opponent_anim_id;
                    other->duration_ = animDuration(*data_, rf->opponent_anim_id);
                }
            }
            hurt_timing_ = duration_ * tuning::kForceImpactFraction;
            break;
        }
        default:
            hurt_timing_ = -1.0f;
            break;
    }
    match.emit({EventType::Announcer, id_, opponent_, static_cast<float>(move.id), entry});
}

void Wrestler::applyMoveHit(Match &match) {
    Wrestler *other = match.wrestler(opponent_);
    if (other == nullptr || other->isDead()) return;
    const MoveInfo *move = data_ != nullptr ? data_->moveInfo(move_id) : nullptr;
    if (move == nullptr) return;

    const float energy_damage = static_cast<float>(move->energy_damage);
    const float motion_damage = static_cast<float>(move->motion_damage);
    other->applyDamage(energy_damage, motion_damage);
    other->facing = -facing;

    const int entry = subState();
    // Royal Rumble: a throw that drives a man into the ropes can put him over
    // the top. When it fires, attemptRumbleToss owns the victim's state.
    const bool tossed = match.attemptRumbleToss(*this, *other, facing);
    if (!tossed && entry != state::USE_F_WS && entry != state::USE_S_WS &&
        entry != state::USE_CL_WS) {
        if (other->isDown()) {
            other->changeState(state::ONGROUND_HURT, 0, match);
        } else if (entry == state::USE_RFWS || entry == state::USE_RFWS_RUSH ||
                   entry == state::USE_RAM_WS || entry == state::USE_TB_WS ||
                   entry == state::USE_SB_WS || other->energy <= 0.0f) {
            other->changeState(state::SLAMMED, 0, match);
            match.emit({EventType::KnockDown, other->id(), id_, energy_damage, state::SLAMMED});
        } else {
            other->changeState(state::HITREACT_HIGH, 0, match);
        }
    }

    if (profile_ != nullptr) {
        for (const StatusSlot &slot : profile_->status) {
            for (int i = 0; i < 4; ++i) {
                if (slot.attack[static_cast<std::size_t>(i)] == move_id) {
                    score += slot.score[static_cast<std::size_t>(i)];
                }
            }
        }
    }
    if (profile_ != nullptr) addPowerUpBonus(profile_->base.power_up_bonus);
    match.emit({EventType::Hit, id_, opponent_, energy_damage, entry});
}

void Wrestler::finishMove(Match &match) {
    const int entry = subState();
    Wrestler *other = match.wrestler(opponent_);

    if (entry == state::USE_F_WS) {
        const ForceMove *force = data_->forceMove(move_sub_type);
        if (force != nullptr) {
            changeState(force->on_exit_self_state > 0 ? force->on_exit_self_state : state::IDLE, 0,
                        match);
            if (other != nullptr) {
                other->changeState(force->on_exit_opponent_state, 0, match);
                if (force->opponent_reverse) other->facing = -facing;
                other->x = ring_ != nullptr
                               ? ring_->clampX(z, x + facing * force->on_exit_opponent_x_offset)
                               : x + facing * force->on_exit_opponent_x_offset;
                other->z = z;
            }
            if (other != nullptr && other->subState() == state::SLAMMED) {
                match.emit({EventType::KnockDown, other->id(), id_, 0.0f, state::SLAMMED});
            }
            return;
        }
    }
    if (entry == state::USE_S_WS) {
        if (other != nullptr) {
            other->held = false;
            other->in_submission_ = false;
            if (!other->isDead() && !other->play_dead) {
                other->changeState(state::BROKEN_SLEEPHOLD, 0, match);
            }
        }
        changeState(state::STAND_TIRED, 0, match);
        match.emit({EventType::SubmissionBroken, id_, opponent_, 0.0f, entry});
        return;
    }
    if (other != nullptr &&
        (other->subState() == state::BE_USED_F_WS || other->subState() == state::BE_USED_RFWS ||
         other->subState() == state::BE_USED_CL_HANG ||
         other->subState() == state::BE_USED_RAM_WS)) {
        other->changeState(state::ONGROUND, 0, match);
    }
    changeState(state::IDLE, 0, match);
}

void Wrestler::tickMoveStates(float dt, Match &match) {
    (void)dt;
    if (!move_hit_applied && hurt_timing_ >= 0.0f && anim_time >= hurt_timing_) {
        move_hit_applied = true;
        applyMoveHit(match);
        hurt_timing_ = -1.0f;
    }

    if (anim_finished_) {
        finishMove(match);
    }
}

void Wrestler::tickVictimHold(float dt, Match &match) {
    (void)dt;
    Wrestler *owner = match.wrestler(opponent_);
    bool driving = false;
    if (owner != nullptr && state_timer < tuning::kVictimHoldMax) {
        switch (subState()) {
            case state::BE_USED_F_WS:
                driving = owner->subState() == state::USE_F_WS;
                break;
            case state::BE_USED_F_WS_PIN:
                driving = owner->subState() == state::USE_F_WS_PIN;
                break;
            case state::BE_USED_RFWS:
                driving = owner->subState() == state::USE_RFWS ||
                          owner->subState() == state::USE_RFWS_RUSH;
                break;
            case state::BE_USED_CL_HANG:
            case state::BE_USED_CL_DROP:
                driving = owner->subState() == state::USE_CL_WS ||
                          owner->subState() == state::USE_CL_HANG ||
                          owner->subState() == state::USE_CL_DROP ||
                          owner->subState() == state::USE_CL_BEGIN;
                break;
            case state::BE_USED_RAM_WS:
                driving = owner->subState() == state::USE_RAM_WS;
                break;
            case state::LO_TH_PRESSED:
                driving = owner->subState() == state::LO_TH_PRESS;
                break;
            case state::PICKUPED:
                driving = owner->subState() == state::PICKUP;
                break;
            default:
                break;
        }
    }
    if (driving) return;

    changeState(subState() == state::PICKUPED ? state::IDLE : state::ONGROUND, 0, match);
}

// ---------------------------------------------------------------------------
// Pins and submissions
// ---------------------------------------------------------------------------
void Wrestler::tickPinStates(float dt, Match &match, const InputFrame &input) {
    (void)dt;
    const int s = subState();
    switch (s) {
        case state::JUMP_FOR_PIN:
            if (anim_finished_) changeState(state::PIN, 0, match);
            return;
        case state::PIN: {
            if (!anim_finished_) return;
            Wrestler *other = match.wrestler(opponent_);
            if (other == nullptr || !other->isDown() || other->isDead() ||
                other->play_dead) {
                changeState(state::STANDUP, 0, match);
                return;
            }
            changeState(state::PINNING, 0, match);
            other->setOpponent(id_);
            other->pinned = true;
            ++other->pin_attempts;
            other->changeState(state::BE_PINNED, 0, match);
            match.emit({EventType::PinStarted, id_, opponent_,
                        static_cast<float>(other->energy), state::PINNING});
            return;
        }
        case state::PINNING: {
            Wrestler *other = match.wrestler(opponent_);
            if (other == nullptr || (!other->pinned && other->subState() != state::BE_PINNED)) {
                changeState(state::STANDUP, 0, match);
            }
            return;
        }
        case state::USE_F_WS_PIN: {
            Wrestler *other = match.wrestler(opponent_);
            if (other == nullptr || other->subState() != state::BE_USED_F_WS_PIN ||
                !match.rule().can_pin) {
                changeState(state::STANDUP, 0, match);
                if (other != nullptr && other->pinned) {
                    other->pinned = false;
                    other->changeState(state::PUSHOFF, 0, match);
                }
                return;
            }
            if (!other->pinned) {
                other->setOpponent(id_);
                other->pinned = true;
                ++other->pin_attempts;
                match.emit({EventType::PinStarted, id_, opponent_,
                            static_cast<float>(other->energy), s});
            }
            return;
        }
        case state::BE_USED_F_WS_PIN:
        case state::BE_PINNED: {
            if (!match.rule().can_pin && subState() == state::BE_USED_F_WS_PIN) {
                changeState(state::PUSHOFF, 0, match);
                return;
            }
            if (!button_counter_active) {
                if (!match.refereePinTarget(id_)) return;
                button_counter_active = true;
                button_count = 0;
                button_goal = data_ != nullptr ? data_->buttonCountGoal(state::BE_PINNED, energy,
                                                                        pin_attempts)
                                               : 9;
            }
            if (!play_dead && input.anyButtonEdge()) button_count += input.buttonEdges();
            if (button_goal > 0 && button_count >= button_goal) {
                pinned = false;
                button_counter_active = false;
                if (match.rule().heal_by_kick_out) {
                    addEnergy(match.common().heal_energy_by_kick_out_opponent);
                }
                changeState(state::PUSHOFF, 0, match);
                match.emit({EventType::KickOut, id_, opponent_,
                            static_cast<float>(button_count), s});
            }
            return;
        }
        default:
            return;
    }
}

void Wrestler::tickSubmission(float dt, Match &match, const InputFrame &input) {
    const bool holder = subState() == state::USE_S_WS;
    const SubmissionMove *sub = data_ != nullptr ? data_->submissionMove(move_sub_type) : nullptr;
    Wrestler *partner = match.wrestler(opponent_);

    if (!holder) {
        if (partner == nullptr || partner->subState() != state::USE_S_WS) {
            held = false;
            in_submission_ = false;
            changeState(state::IDLE, 0, match);
            return;
        }
        if (!button_counter_active && sub != nullptr) {
            button_counter_active = true;
            button_count = 0;
            button_goal = data_->buttonCountGoal(state::RESTRAINED, energy, 0, move_id);
            if (button_goal <= 0) button_goal = 13;
        }
        if (!play_dead && input.anyButtonEdge()) button_count += input.buttonEdges();
        if (button_goal > 0 && button_count >= button_goal) {
            held = false;
            in_submission_ = false;
            button_counter_active = false;
            changeState(state::BROKEN_RESTRAIN, 0, match);
            applyDamage(match.damage().be_broken_sleep_hold_energy,
                        match.damage().be_broken_sleep_hold_motion);
            match.emit({EventType::SubmissionBroken, opponent_, id_, 0.0f, subState()});
            if (partner != nullptr) partner->changeState(state::STAND_TIRED, 0, match);
            return;
        }
        if (sub != nullptr && sub->is_give_up && match.rule().can_submission) {
            suffocation += dt / std::max(0.01f, match.common().max_suffocation_time);
            if (suffocation >= 1.0f) {
                suffocation = 0.0f;
                match.emit({EventType::SubmissionGiveUp, id_, opponent_, 0.0f, subState()});
                if (partner != nullptr) partner->changeState(state::STAND_TIRED, 0, match);
                knockOut(match);
            }
        }
        return;
    }

    if (partner == nullptr || partner->isDead()) {
        changeState(state::STAND_TIRED, 0, match);
        return;
    }

    // Periodic submission damage ticks while the hold is locked in.
    if (sub != nullptr && sub->sec_per_hurt > 0.0f) {
        if (hurt_timing_ > 0.0f && anim_time >= hurt_timing_) {
            applyMoveHit(match);
            hurt_timing_ += sub->sec_per_hurt;
        }
    }

    if (anim_finished_) {
        finishMove(match);
    }
}

}  // namespace wrestlefest
