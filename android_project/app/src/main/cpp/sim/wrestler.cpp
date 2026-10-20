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
}

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
void Wrestler::changeState(int new_state, int chain, Match &match) {
    if (new_state <= 0) new_state = state::IDLE;

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
                if (anim->x_offset != 0.0f) {
                    Wrestler *other = match.wrestler(opponent_);
                    if (other != nullptr) {
                        other->x = x + facing * anim->x_offset;
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

void Wrestler::knockOut(Match &match) {
    region = region::KNOCKED_OUT;
    energy = 0.0f;
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
    // The locomotion and hold animations repeat for as long as the state lasts;
    // everything else (strikes, reactions, moves) plays once and then reports
    // finished, which is what drives the state transitions above.
    const int s = subState();
    return s == state::IDLE || s == state::WALK || s == state::RUN ||
           s == state::STAND_TIRED || s == state::TAUNT || s == state::GRAPPLE_IDLE ||
           s == state::GRAPPLE_HIGH || s == state::GRAPPLE_LOW || s == state::HEADLOCK ||
           s == state::HEADLOCKED || s == state::DOGFIGHT_STANDS || s == state::DOGFIGHT_HIGHER ||
           s == state::DOGFIGHT_LOWER || s == state::PINNING || s == state::BE_PINNED ||
           s == state::VICTORY || s == state::LOSE ||
           (s == state::ENTRANCE &&
            (profile_ == nullptr || profile_->entrance.anims.empty()));
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
    if (alive) {
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
        case state::PINNING:
        case state::BE_PINNED:
            tickPinStates(dt, match, input);
            break;
        case state::USE_S_WS:
        case state::BE_USED_S_WS:
            tickSubmission(dt, match, input);
            break;
        case state::STANDUP:
        case state::JUMP_FOR_PIN:
        case state::PICKUP:
        case state::PICKUPED:
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

    // EntranceInfo is a timed playlist; many wrestlers repeat their last walk
    // animation several times. Advance one entry only after its clip completes.
    const std::size_t count = profile_->entrance.anims.size();
    for (std::size_t attempts = 0; attempts < count; ++attempts) {
        entrance_anim_index_ = (entrance_anim_index_ + 1) % count;
        const int next_id = profile_->entrance.anims[entrance_anim_index_];
        const AnimInfo *next = data_->animInfo(next_id);
        if (next == nullptr || next->frame_count <= 0) continue;
        anim_id_ = next_id;
        duration_ = animDuration(*data_, next_id);
        anim_time = 0.0f;
        anim_finished_ = false;
        return;
    }
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
    (void)dt;
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
    if (data_ == nullptr) return fallback;
    const StateInfo *info = data_->stateInfo(state_id);
    if (info == nullptr || info->anim_id < 0) return fallback;
    const AnimInfo *anim = data_->animInfo(info->anim_id);
    if (anim == nullptr || anim->x_offset == 0.0f) return fallback;
    return anim->x_offset;
}

void Wrestler::tryStartAttack(Match &match, const InputFrame &input) {
    Wrestler *other = match.wrestler(opponent_);
    if (profile_ == nullptr || other == nullptr || play_dead) return;
    if (!other->active || other->isDead() || other->team == team) return;

    const float range_z = match.common().collision_range_z;
    const bool opponent_down = other->isDown();

    // Direction + attack = grapple. The status slot decides what is available
    // (0 running attack, 1 throw, 3 ground attack).
    const bool grappling = input.dirX() != 0 || input.dirZ() != 0;
    const float grapple_range = collisionRange(state::GRAPPLE_IDLE, 100.0f);
    if (grappling && !opponent_down && !other->isBusy() &&
        canReach(x, z, facing, other->x, other->z, grapple_range, range_z, true)) {
        startGrapple(match, input.dirZ() != 0);  // up/down = high/low grapple
        return;
    }

    // Ground attack when the opponent is down (uses the ground move slot 3).
    const float ground_range = collisionRange(state::USE_F_WS, grapple_range);
    if (opponent_down &&
        canReach(x, z, facing, other->x, other->z, ground_range, range_z, true)) {
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

    // Plain strike. Fast strikes (PUNCH/KICK) are always available; the running
    // attack slot 0 is used while running.
    const int slot_index = subState() == state::RUN ? 0 : -1;
    if (slot_index >= 0) {
        const StatusSlot &slot = profile_->status[static_cast<std::size_t>(slot_index)];
        for (int i = 0; i < slot.list_count && i < 4; ++i) {
            const int id = slot.attack[static_cast<std::size_t>(i)];
            if (id < 0) continue;
            const MoveInfo *move = data_->moveInfo(id);
            if (move == nullptr) continue;
            if (move->energy_damage <= 0) continue;
            const float run_range = collisionRange(state::RUN, grapple_range + 20.0f);
            if (!canReach(x, z, facing, other->x, other->z, run_range, range_z, true)) return;
            beginMove(id);
            useWrestleStyle(*move, match);
            return;
        }
    }

    startStrike(input.btn1_edge ? state::KICK : state::PUNCH, match);
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
        // Snap the pair together so the grapple animations line up, using the
        // grapple animation's own xOffset (the engine's AdjustOpponentXZ).
        other->x = x + facing * stateSnapOffset(state::GRAPPLE_IDLE, 120.0f);
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
            canReach(x, z, facing, other->x, other->z, strike_range, range_z, true)) {
            // Damage is the (energy, motion) pair from WrestleStyleList.
            const float energy_damage = static_cast<float>(move->energy_damage);
            const float motion_damage = static_cast<float>(move->motion_damage);
            other->applyDamage(energy_damage, motion_damage);
            other->facing = -facing;
            other->changeState(subState() == state::KICK ? state::HITREACT_HIGH : state::HITREACT_MID,
                               0, match);
            // Motion power for the attacker on a clean hit.
            addMotionPower(2.0f);
            if (profile_ != nullptr) addPowerUpBonus(profile_->base.power_up_bonus);
            match.emit({EventType::Hit, id_, opponent_, energy_damage, subState()});
        }
    }
    if (anim_finished_) changeState(state::IDLE, 0, match);
}

// ---------------------------------------------------------------------------
// Reactions / ground
// ---------------------------------------------------------------------------
void Wrestler::tickReaction(float dt, Match &match) {
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
    // Mash to get up (ButtonCountTable "ONGROUND" row); a downed wrestler below
    // 10 energy needs 100 presses, i.e. cannot get up.
    if (!button_counter_active) {
        button_counter_active = true;
        button_count = 0;
        button_goal = data_ != nullptr ? data_->buttonCountGoal(state::ONGROUND, energy, 0) : 4;
    }
    if (input.anyButton()) ++button_count;

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
    (void)dt;
    Wrestler *other = match.wrestler(opponent_);
    if (other == nullptr) {
        changeState(state::IDLE, 0, match);
        return;
    }

    const int s = subState();
    if (s == state::GRAPPLE_IDLE) {
        // The clinch is decided by Match's beat roll (GrappleIdleState::onTick),
        // not by the button: a press only hurries the next beat along, which is
        // what mashing does in the original.
        if (input.anyButtonEdge() && profile_ != nullptr && data_ != nullptr) {
            state_timer += tuning::kGrappleMashStep;
        }
        if (state_timer > 2.5f) {
            // Time out of the clinch.
            changeState(state::PUSHOFF, 0, match);
            other->changeState(state::PUSHOFF, 0, match);
        }
        return;
    }

    if (s == state::GRAPPLE_HIGH || s == state::GRAPPLE_LOW) {
        // The AI decides the exchange (GrappleIdleState::onTick); the wrestler
        // only exposes the window. Match drives the roll and calls winGrapple().
        if (anim_finished_) {
            changeState(state::PUSHOFF, 0, match);
            other->changeState(state::PUSHOFF, 0, match);
        }
        return;
    }

    if (anim_finished_) changeState(state::IDLE, 0, match);
}

void Wrestler::tickDogfight(float dt, Match &match) {
    (void)dt;
    if (subState() == state::DOGFIGHT_STANDS) {
        // The clash is owned by the AI state (DogfightState), which resolves it
        // after its beat; the wrestler holds the pose until then.
        return;
    }
    // The follow-up beats (punch / being punched) are ordinary reactions.
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
                if (force->anim_id >= 0) {
                    anim_id_ = force->anim_id;
                    duration_ = animDuration(*data_, force->anim_id);
                }
                if (other != nullptr) {
                    if (force->opponent_anim_id >= 0) {
                        other->anim_id_ = force->opponent_anim_id;
                        other->duration_ = animDuration(*data_, force->opponent_anim_id);
                    }
                    other->changeState(state::BE_USED_F_WS, 0, match);
                    other->facing = -facing;
                    other->opponent_ = id_;
                    // AdjustOpponentXZ on entry keeps paired animations aligned.
                    if (force->adjust_opponent) {
                        other->x = x + facing * 60.0f;
                        other->z = z;
                    }
                }
                // Damage lands at HurtOpponentTimming, else at a calibrated point.
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
                other->move_sub_type = sub_type;
                other->in_submission_ = true;
                other->held = true;
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
                duration_ = animDuration(*data_, anim_id_);
            }
            if (other != nullptr) {
                other->changeState(state::BE_USED_RFWS, 0, match);
                other->facing = -facing;
                other->opponent_ = id_;
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

    // Reaction state from the move family, matching the original's pairing.
    const int entry = subState();
    if (entry != state::USE_F_WS && entry != state::USE_S_WS && entry != state::USE_CL_WS) {
        other->changeState(isDown() ? state::ONGROUND_HURT : state::HITREACT_HIGH, 0, match);
    }
    if (other->energy <= 0.0f) {
        other->energy = 0.0f;
        other->energy_display = 0.0f;
        other->play_dead = true;
        other->active = false;
        other->region = region::KNOCKED_OUT;
        other->changeState(state::ONGROUND, 0, match);
        match.emit({EventType::KnockedOut, other->id(), id_, 0.0f, state::ONGROUND});
    }
    // Score comes from the wrestler's status slot, not from a literal.
    if (profile_ != nullptr) {
        for (const StatusSlot &slot : profile_->status) {
            for (int i = 0; i < 4; ++i) {
                if (slot.attack[static_cast<std::size_t>(i)] == move_id) {
                    score += slot.score[static_cast<std::size_t>(i)];
                }
            }
        }
    }
    // PowerUpBonus: landing a hit fills the attacker's meter by the
    // CommonVariableInfo value (10) - the arcade's momentum meter.
    if (profile_ != nullptr) addPowerUpBonus(profile_->base.power_up_bonus);
    match.emit({EventType::Hit, id_, opponent_, energy_damage, entry});
}

void Wrestler::finishMove(Match &match) {
    const int entry = subState();
    Wrestler *other = match.wrestler(opponent_);

    if (entry == state::USE_F_WS) {
        const ForceMove *force = data_->forceMove(move_sub_type);
        if (force != nullptr) {
            // OnExitSelfState is the "spent" pose (STAND_TIRED for most throws).
            changeState(force->on_exit_self_state > 0 ? force->on_exit_self_state : state::IDLE, 0,
                        match);
            if (other != nullptr) {
                other->changeState(force->on_exit_opponent_state, 0, match);
                if (force->opponent_reverse) other->facing = -facing;
                other->x = x + facing * force->on_exit_opponent_x_offset;
                other->z = z;
            }
            if (other != nullptr && other->subState() == state::SLAMMED) {
                match.emit({EventType::KnockDown, other->id(), id_, 0.0f, state::SLAMMED});
            }
            return;
        }
    }
    if (entry == state::USE_S_WS) {
        // A submission that was not given up: the hold breaks and both stand.
        if (other != nullptr) {
            other->held = false;
            other->in_submission_ = false;
            other->changeState(state::BROKEN_SLEEPHOLD, 0, match);
        }
        changeState(state::STAND_TIRED, 0, match);
        match.emit({EventType::SubmissionBroken, id_, opponent_, 0.0f, entry});
        return;
    }
    if (other != nullptr && other->subState() == state::BE_USED_F_WS) {
        other->changeState(state::ONGROUND, 0, match);
    }
    changeState(state::IDLE, 0, match);
}

void Wrestler::tickMoveStates(float dt, Match &match) {
    (void)dt;

    // Damage lands once, at the calibrated/HurtTimming point.
    if (!move_hit_applied && hurt_timing_ >= 0.0f && anim_time >= hurt_timing_) {
        move_hit_applied = true;
        applyMoveHit(match);
        hurt_timing_ = -1.0f;
    }

    if (anim_finished_) {
        finishMove(match);
    }
}

// ---------------------------------------------------------------------------
// Pins and submissions
// ---------------------------------------------------------------------------
void Wrestler::tickPinStates(float dt, Match &match, const InputFrame &input) {
    (void)dt;
    const int s = subState();
    if (s == state::PINNING) {
        // The referee owns the count; hold the pose until Match ends the pin.
        return;
    }
    if (s == state::BE_PINNED) {
        if (!button_counter_active) {
            button_counter_active = true;
            button_count = 0;
            button_goal = data_ != nullptr ? data_->buttonCountGoal(state::BE_PINNED, energy,
                                                                    pin_attempts)
                                           : 9;
            match.emit({EventType::PinStarted, id_, opponent_,
                        static_cast<float>(button_goal), s});
        }
        if (input.anyButton()) ++button_count;
        if (button_goal > 0 && button_count >= button_goal) {
            pinned = false;
            button_counter_active = false;
            if (match.rule().heal_by_kick_out) {
                addEnergy(match.common().heal_energy_by_kick_out_opponent);
            }
            changeState(state::STANDUP, 0, match);
            match.emit({EventType::KickOut, id_, opponent_, static_cast<float>(button_count), s});
            Wrestler *other = match.wrestler(opponent_);
            if (other != nullptr && other->subState() == state::PINNING) {
                other->changeState(state::STANDUP, 0, match);
            }
        }
        return;
    }
}

void Wrestler::tickSubmission(float dt, Match &match, const InputFrame &input) {
    const bool holder = subState() == state::USE_S_WS;
    const SubmissionMove *sub = data_ != nullptr ? data_->submissionMove(move_sub_type) : nullptr;

    if (!holder) {
        // Victim: mash to escape, otherwise suffocation builds toward a give-up.
        if (!button_counter_active && sub != nullptr) {
            button_counter_active = true;
            button_count = 0;
            button_goal = data_->buttonCountGoal(state::RESTRAINED, energy, 0);
            if (button_goal <= 0) button_goal = 13;
        }
        if (input.anyButton()) ++button_count;
        if (button_goal > 0 && button_count >= button_goal) {
            held = false;
            button_counter_active = false;
            changeState(state::BROKEN_RESTRAIN, 0, match);
            applyDamage(match.damage().be_broken_sleep_hold_energy,
                        match.damage().be_broken_sleep_hold_motion);
            match.emit({EventType::SubmissionBroken, opponent_, id_, 0.0f, subState()});
            Wrestler *owner = match.wrestler(opponent_);
            if (owner != nullptr) owner->changeState(state::STAND_TIRED, 0, match);
            return;
        }
        // Suffocation: GetSuffocationRate = time / MaxSuffocationTime.
        if (sub != nullptr && sub->is_give_up) {
            suffocation += dt / std::max(0.01f, match.common().max_suffocation_time);
            if (suffocation >= 1.0f) {
                suffocation = 0.0f;
                energy = 0.0f;
                play_dead = true;
                changeState(state::ONGROUND, 0, match);
                match.emit({EventType::SubmissionGiveUp, id_, opponent_, 0.0f, subState()});
            }
        }
        return;
    }

    // Holder: keeps the hold until the animation ends or the victim gives up.
    if (anim_finished_) {
        finishMove(match);
    }
}

}  // namespace wrestlefest
