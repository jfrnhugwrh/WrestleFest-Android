#include "ai.h"

#include "match.h"

#include <algorithm>
#include <cmath>

namespace wrestlefest {
namespace {

// Sets the stick toward a target with the 0.35 deadzone of the original: both
// bits of an axis are set when the axis is inside the deadzone.
void stickToward(InputFrame *input, float dx, float dz) {
    const bool right = dx >= -0.35f;
    const bool left = dx <= 0.35f;
    input->right = right;
    input->left = left;
    input->down = dz >= -0.35f;
    input->up = dz <= 0.35f;
}

// Deliberate single-axis push, used when the AI wants a grapple: the deadzone
// form above would read as "no direction" and produce a strike instead.
void stickDirection(InputFrame *input, float dx, float dz) {
    input->left = dx < 0.0f;
    input->right = dx > 0.0f;
    input->up = dz < 0.0f;
    input->down = dz > 0.0f;
}

}  // namespace

int AiEntity::partnerOf(Match &match) const {
    const Wrestler *self = match.wrestler(actor);
    if (self == nullptr) return -1;
    for (int i = 0; i < match.wrestlerCount(); ++i) {
        const Wrestler *other = match.wrestler(i);
        if (other != nullptr && other != self && other->team == self->team) return i;
    }
    return -1;
}

void AiEntity::setState(AiState next, Match &match) {
    (void)match;
    state = next;
    timer = 0.0f;
    switch (next) {
        case AiState::BeAttacked: {
            // 0.30 - 0.023 * (strength - 1): stronger wrestlers recover faster.
            const Wrestler *self = match.wrestler(actor);
            const float strength = self != nullptr && self->profile() != nullptr
                                       ? static_cast<float>(self->profile()->strength())
                                       : 1.0f;
            beat = std::max(0.02f, tuning::kReactionBeat +
                                       tuning::kReactionBeatPerStrength * (strength - 1.0f));
            break;
        }
        case AiState::BeHeld:
            beat = tuning::kHeldBeat;
            break;
        case AiState::BePin:
            // 0.12 + 7.63e-7 * (rand() & 0x7fff) -> 0.12 .. 0.145
            beat = tuning::kPinEscapeBase;
            break;
        default:
            beat = 0.0f;
            break;
    }
}

bool AiEntity::checkTarget(Match &match) {
    Wrestler *self = match.wrestler(actor);
    if (self == nullptr) return false;
    Wrestler *other = match.wrestler(target);
    if (other == nullptr || other == self) return false;
    if (other->team == self->team) return false;
    if (!other->active || other->isDead() || other->held) return false;
    return true;
}

void AiEntity::searchEnemy(Match &match) {
    Wrestler *self = match.wrestler(actor);
    if (self == nullptr) return;
    int best = -1;
    float best_dist = 1.0e9f;
    for (int i = 0; i < match.wrestlerCount(); ++i) {
        Wrestler *other = match.wrestler(i);
        if (other == nullptr || other == self) continue;
        if (other->team == self->team || !other->active || other->isDead()) continue;
        const float d = std::fabs(other->x - self->x) + std::fabs(other->z - self->z);
        if (d < best_dist) {
            best_dist = d;
            best = i;
        }
    }
    target = best;
}

void AiEntity::tickWalk(float dt, Match &match, InputFrame *input) {
    (void)dt;
    Wrestler *self = match.wrestler(actor);
    Wrestler *other = match.wrestler(target);
    if (self == nullptr || other == nullptr) {
        setState(AiState::SearchEnemy, match);
        return;
    }
    if (!checkTarget(match)) {
        setState(AiState::SearchEnemy, match);
        return;
    }

    // WalkState: approach into grapple range (100 px, comfortably outside the
    // 80 px solid body block) so both strikes and clinches are live on arrival.
    const float dx = other->x - self->x;
    const float dz = other->z - self->z;
    const StateInfo *grapple = match.data().stateInfo(state::GRAPPLE_IDLE);
    const float engage_range = grapple != nullptr && grapple->collision_range_x > 0.0f
                                   ? grapple->collision_range_x - 4.0f
                                   : 96.0f;
    if (std::fabs(dx) > engage_range || std::fabs(dz) > match.common().collision_range_z) {
        stickToward(input, dx, dz * 0.6f);
        // A long gap is closed at a run: holding the red button puts the
        // wrestler in RUN (CWrestler::TickLocomotion), which is also what makes
        // the running attack available.
        const float gap = std::fabs(dx) + std::fabs(dz) * 0.5f;
        if (gap > tuning::kAiRunRange) input->btn0 = true;
        timer += tuning::kFixedDelta;
    } else {
        setState(AiState::Attack, match);
    }
}

void AiEntity::tickAttackVsOpponent(Match &match, InputFrame *input, int opponent_state) {
    Wrestler *self = match.wrestler(actor);
    Wrestler *other = match.wrestler(target);
    if (self == nullptr || other == nullptr) return;

    // AttackState::onTick. The routings that matter for a 1v1:
    switch (opponent_state) {
        case state::IDLE:
        case state::WALK:
        case state::STAND_TIRED:
        case state::HITREACT_MID:
        case state::HITREACT_HIGH:
        case state::GRAPPLE_IDLE:
        case state::GRAPPLE_HIGH:
        case state::GRAPPLE_LOW: {
            // Grappling is how a match is actually won (a punch does 2 damage
            // against 1.4/s of healing; a throw does 10 and floors him). Use the
            // full StateTable CollisionRangeX (100 px) so the [80..100] window
            // outside kBodySeparation (80 px) reliably engages clinches!
            const StateInfo *grapple_info = match.data().stateInfo(state::GRAPPLE_IDLE);
            const float grapple_range = grapple_info != nullptr && grapple_info->collision_range_x > 0.0f
                                            ? grapple_info->collision_range_x
                                            : 100.0f;
            const float dx = other->x - self->x;
            const float dz = other->z - self->z;
            const bool grapple_close = std::fabs(dx) <= grapple_range &&
                                       std::fabs(dz) <= match.common().collision_range_z;
            const bool wants_grapple = match.isRumble() || match.rng().range(11) < 8;
            if (wants_grapple && !grapple_close) {
                // Step inside [80..100] grapple reach instead of throwing a
                // max-range jab that pushes the target back out of clinch range!
                stickToward(input, dx, dz);
                return;
            }
            if (wants_grapple && grapple_close) {
                // dx==0 (standing on top of him) still needs a direction or the
                // press reads as a plain strike - reach the way we're facing.
                stickDirection(input, dx != 0.0f ? dx : self->facing, dz);
            }
            input->btn0 = true;
            input->btn0_edge = true;
            setState(AiState::Attack, match);
            return;
        }
        case state::RUN: {
            // Anti-running: intercept with a strike (the whip/strike split is
            // rand()%11 < 5 in the original).
            input->btn0 = true;
            input->btn0_edge = true;
            return;
        }
        case state::ONGROUND:
        case state::ONGROUND_HURT:
        case state::SLAMMED: {
            setState(AiState::OnGroundAttack, match);
            return;
        }
        default: {
            // Anything else: if the opponent is free, throw a strike. This keeps
            // the AI from stalling when it meets a state the routing table does
            // not name.
            if (!other->isBusy()) {
                stickToward(input, other->x - self->x, other->z - self->z);
                input->btn0 = true;
                input->btn0_edge = true;
            }
            return;
        }
    }
}

void AiEntity::tickAttack(float dt, Match &match, InputFrame *input) {
    Wrestler *self = match.wrestler(actor);
    Wrestler *other = match.wrestler(target);
    if (self == nullptr || other == nullptr) {
        setState(AiState::SearchEnemy, match);
        return;
    }
    if (!checkTarget(match)) {
        setState(AiState::SearchEnemy, match);
        return;
    }

    const float dx = other->x - self->x;
    const float dz = other->z - self->z;
    // A little closer than strike reach: the AI commits to the clash here.
    const StateInfo *punch = match.data().stateInfo(state::PUNCH);
    const float reach = punch != nullptr && punch->collision_range_x > 0.0f
                            ? punch->collision_range_x + 10.0f
                            : 120.0f;
    const bool in_range = std::fabs(dx) <= reach &&
                          std::fabs(dz) <= match.common().collision_range_z;
    if (!in_range) {
        stickToward(input, dx, dz);
        timer += dt;
        if (timer > 0.6f) setState(AiState::Walk, match);
        return;
    }
    // Face the target by walking into it, then press - but only once the AI's
    // beat allows another attempt.
    stickToward(input, dx, dz);
    if (attack_delay > 0.0f) return;
    const int before_id = self->move_id;
    tickAttackVsOpponent(match, input, other->subState());
    if (input->anyButtonEdge() || self->move_id != before_id) {
        attack_delay = 0.55f + static_cast<float>(match.rng().range(45)) * 0.01f;
    }
}

bool AiEntity::tickTag(Match &match, InputFrame *input) {
    Wrestler *self = match.wrestler(actor);
    if (self == nullptr || !match.tagEnabled() || !self->active || self->isBusy()) return false;
    const int mate_index = match.partnerIndex(actor);
    if (mate_index < 0 || !match.isOnApron(mate_index)) return false;
    Wrestler *mate = match.wrestler(mate_index);
    if (mate == nullptr || mate->isDead()) return false;

    // Only tag out when tired and the man on the apron has more left.
    const float mine = self->energyFraction();
    if (mine > tuning::kTagEnergyFraction || mate->energyFraction() < mine + 0.15f) return false;

    const float dx = mate->x - self->x;
    const float dz = mate->z - self->z;
    stickToward(input, dx, dz);
    if (std::fabs(dx) < tuning::kTagReach * 0.8f && std::fabs(dz) < tuning::kTagReach * 0.8f) {
        input->btn1 = true;
        input->btn1_edge = true;
    }
    return true;
}

void AiEntity::tickRingSide(Match &match, InputFrame *input) {
    Wrestler *self = match.wrestler(actor);
    if (self == nullptr) return;
    // RingSideState: head for the centre of the ring and climb in.
    const float dx = match.ring().centerX() - self->x;
    stickToward(input, dx, 0.0f);
    setState(AiState::Walk, match);
}

void AiEntity::tickGrapple(float dt, Match &match) {
    Wrestler *self = match.wrestler(actor);
    if (self == nullptr) return;
    // GrappleIdleState: the roll itself lives in Match (it owns the RNG and the
    // rope test); the AI only exposes the timing.
    timer += dt;
    if (timer >= tuning::kGrappleBeat) timer = 0.0f;
}

void AiEntity::tickReaction(float dt, Match &match) {
    timer += dt;
    if (timer >= beat) setState(AiState::SearchEnemy, match);
}

// One button EDGE every kAiMashPeriod seconds. The original's mash counters
// count real presses (the +0xa8/+0xa9 bits are edge pulses), so holding the
// button flat - the old behaviour - did nothing; the cadence is what a mashing
// thumb does (BePinState's 0.12 s beat).
void AiEntity::emitMash(float dt, Match &match, InputFrame *input, bool also_blue) {
    mash_timer_ += dt;
    if (mash_timer_ < tuning::kAiMashPeriod) return;
    mash_timer_ = 0.0f;
    input->btn0 = true;
    input->btn0_edge = true;
    if (also_blue && match.rng().range(3) == 0) {
        input->btn1 = true;
        input->btn1_edge = true;
    }
}

void AiEntity::tick(float dt, Match &match, InputFrame *input) {
    if (input == nullptr) return;
    input->clear();
    Wrestler *self = match.wrestler(actor);
    if (self == nullptr || self->isDead()) return;

    timer += dt;
    if (attack_delay > 0.0f) attack_delay -= dt;

    // A tired tag wrestler heads for his corner before anything else.
    if (tickTag(match, input)) return;

    // The routing switch of GAMEPLAY_IMPLEMENTATION 5.2 - re-derived every tick.
    if (self->held) {
        setState(AiState::BeHeld, match);
    } else if (self->pinned) {
        setState(AiState::BePin, match);
    } else {
        switch (self->subState()) {
            case state::IDLE:
            case state::WALK:
            case state::RUN:
                setState(AiState::SearchEnemy, match);
                break;
            case state::GRAPPLE_IDLE:
            case state::GRAPPLE_HIGH:
            case state::GRAPPLE_LOW:
                setState(AiState::GrappleIdle, match);
                break;
            case state::HEADLOCK:
            case state::IRISH_WHIP:
            case state::USE_F_WS:
            case state::USE_F_WS_PIN:
            case state::USE_S_WS:
                setState(AiState::Attack, match);
                break;
            case state::HEADLOCKED:
            case state::ONGROUND:
            case state::IRISH_WHIPPED:
            case state::BE_USED_F_WS:
            case state::BE_USED_F_WS_PIN:
            case state::BE_USED_S_WS:
            case state::BREAK_RESTRAIN:
            case state::BROKEN_RESTRAIN:
                setState(AiState::BeAttacked, match);
                break;
            case state::DOGFIGHT_STANDS:
            case state::DOGFIGHT_HIGHER:
            case state::DOGFIGHT_LOWER:
            case state::DOGFIGHT_PUNCH:
            case state::DOGFIGHT_PUNCHED:
                setState(AiState::Dogfight, match);
                break;
            default:
                break;
        }
    }

    // Handlers can hand over to another state in the same tick (the original
    // re-derives from the actor every tick, so searching->walking->attacking
    // happens without a frame of delay).
    for (int pass = 0; pass < 4; ++pass) {
        const AiState before = state;
        switch (state) {
            case AiState::SearchEnemy:
                searchEnemy(match);
                if (target < 0) return;
                setState(AiState::Walk, match);
                break;
            case AiState::Walk:
                tickWalk(dt, match, input);
                break;
            case AiState::Attack:
                tickAttack(dt, match, input);
                break;
            case AiState::GrappleIdle: {
                // In the clinch the AI mashes at human thumb cadence while
                // Match::updateGrappleBeats resolves the throw/reversal roll.
                tickGrapple(dt, match);
                emitMash(dt, match, input, false);
                break;
            }
            case AiState::Dogfight:
                input->btn0 = true;
                break;
            case AiState::BeAttacked:
            case AiState::BeHeld:
                tickReaction(dt, match);
                emitMash(dt, match, input, false);  // mash out
                break;
            case AiState::BePin: {
                tickReaction(dt, match);
                // Kick-out mash: while the ref is coming over, the victim
                // hammers both buttons to reach the button_count goal.
                emitMash(dt, match, input, true);
                break;
            }
            case AiState::OnGroundAttack: {
                Wrestler *other = match.wrestler(target);
                if (other != nullptr) {
                    stickToward(input, other->x - self->x, other->z - self->z);
                    const StateInfo *pin_info = match.data().stateInfo(state::PINNING);
                    const float reach = pin_info != nullptr && pin_info->collision_range_x > 0.0f
                                            ? pin_info->collision_range_x * 0.9f
                                            : 90.0f;
                    const bool in_reach =
                        std::fabs(other->x - self->x) <= reach &&
                        std::fabs(other->z - self->z) <= match.common().collision_range_z;
                    if (in_reach && attack_delay <= 0.0f) {
                        // Go for the pin when the rule allows it and the man on
                        // the mat is drained; otherwise keep stomping.
                        const bool pin = match.rule().can_pin &&
                                         self->pinWindowOpen(*other, match);
                        if (pin) {
                            input->btn1 = true;
                            input->btn1_edge = true;
                        } else {
                            input->btn0 = true;
                            input->btn0_edge = true;
                        }
                        attack_delay = tuning::kAiMashPeriod * 8.0f +
                                       static_cast<float>(match.rng().range(40)) * 0.01f;
                    }
                }
                break;
            }
            case AiState::RingSide:
                tickRingSide(match, input);
                break;
            case AiState::OutSide:
            case AiState::InRingAlone:
            case AiState::None:
            default:
                break;
        }
        if (state == before) break;
    }
}

}  // namespace wrestlefest
