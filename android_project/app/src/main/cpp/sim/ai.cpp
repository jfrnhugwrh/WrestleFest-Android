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
        case AiState::BeAttacked:
            // 0.30 - 0.023 * (strength - 1)
            beat = std::max(0.02f, tuning::kReactionBeat);
            break;
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

    // WalkState: approach until in striking range, then engage.
    const float dx = other->x - self->x;
    const float dz = other->z - self->z;
    // Strike reach from the state table (PUNCH CollisionRangeX).
    const StateInfo *punch = match.data().stateInfo(state::PUNCH);
    const float engage_range = punch != nullptr && punch->collision_range_x > 0.0f
                                   ? punch->collision_range_x
                                   : 110.0f;
    if (std::fabs(dx) > engage_range || std::fabs(dz) > match.common().collision_range_z) {
        stickToward(input, dx, dz * 0.6f);
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
        case state::GRAPPLE_IDLE:
        case state::GRAPPLE_HIGH:
        case state::GRAPPLE_LOW: {
            // Grappling is how a match is actually won (a punch does 2 damage
            // against 1.4/s of healing). The AI fights for the clinch most of
            // the time, exactly like AttackState's close-range routing.
            // In a Rumble only the clinch can win (CanPin/CanSubmission are 0),
            // so the AI always fights for the grapple there.
            const bool grapple = match.isRumble() || match.rng().range(11) < 8;
            if (grapple && std::fabs(other->x - self->x) > 4.0f) {
                stickDirection(input, other->x - self->x, other->z - self->z);
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
                // In the clinch the AI mashes: the exchange itself is resolved
                // by Match's beat roll.
                tickGrapple(dt, match);
                if (match.rng().range(2) == 0) {
                    input->btn0 = true;
                    input->btn0_edge = true;
                }
                break;
            }
            case AiState::Dogfight:
                input->btn0 = true;
                break;
            case AiState::BeAttacked:
            case AiState::BeHeld:
                tickReaction(dt, match);
                input->btn0 = true;  // mash out
                break;
            case AiState::BePin:
                tickReaction(dt, match);
                input->btn0 = true;
                input->btn1 = match.rng().range(2) == 0;
                break;
            case AiState::OnGroundAttack: {
                Wrestler *other = match.wrestler(target);
                if (other != nullptr) {
                    stickToward(input, other->x - self->x, other->z - self->z);
                    input->btn0 = true;
                    input->btn0_edge = true;
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
