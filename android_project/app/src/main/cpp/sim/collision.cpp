#include "collision.h"

#include "../data/game_data.h"

#include <algorithm>
#include <cmath>

namespace wrestlefest {

CollisionLayer stateCollisionLayer(int sub_state) {
    switch (sub_state) {
        case state::kNoState:
        case state::ENTRANCE:
        case state::VICTORY:
        case state::LOSE:
        case state::TAG_IN:
        case state::TAG_OUT:
        case state::CLIMBTOP:
        case state::CLIMBIN:
        case state::CLIMBOUT:
        case state::CLIMB_DOWN_F_TO:
            return CollisionLayer::NonInteractive;

        case state::ONGROUND:
        case state::ONGROUND_HURT:
        case state::SLAMMED:
        case state::KNOCKED_BACK:
            return CollisionLayer::Grounded;

        case state::AIRBORNE:
        case state::USE_TB_WS:
        case state::USE_SB_WS:
        case state::USE_HT_WS:
        case state::USE_TA_WS:
            return CollisionLayer::Airborne;

        case state::GRAPPLE_IDLE:
        case state::GRAPPLE_HIGH:
        case state::GRAPPLE_LOW:
        case state::HEADLOCK:
        case state::HEADLOCKED:
        case state::HEADLOCK_WAIT:
        case state::DOGFIGHT_STANDS:
        case state::DOGFIGHT_HIGHER:
        case state::DOGFIGHT_LOWER:
        case state::DOGFIGHT_PUNCH:
        case state::DOGFIGHT_PUNCHED:
        case state::IRISH_WHIP:
        case state::IRISH_WHIPPED:
        case state::RESTRAIN:
        case state::RESTRAINED:
        case state::USE_F_WS_BEGIN:
        case state::USE_F_WS:
        case state::BE_USED_F_WS:
        case state::USE_F_WS_PIN:
        case state::BE_USED_F_WS_PIN:
        case state::USE_S_WS:
        case state::BE_USED_S_WS:
        case state::USE_CL_WS:
        case state::USE_CL_BEGIN:
        case state::USE_CL_HANG:
        case state::USE_CL_DROP:
        case state::BE_USED_CL_HANG:
        case state::BE_USED_CL_DROP:
        case state::USE_RAM_WS:
        case state::BE_USED_RAM_WS:
        case state::USE_RFWS_BEGIN:
        case state::USE_RFWS_RUSH:
        case state::USE_RFWS:
        case state::BE_USED_RFWS:
        case state::LO_TH_PRESS:
        case state::LO_TH_PRESSED:
        case state::PICKUP:
        case state::PICKUPED:
        case state::JUMP_FOR_PIN:
        case state::PIN:
        case state::PINNING:
        case state::BE_PINNED:
            return CollisionLayer::Paired;

        default:
            // IDLE, WALK, RUN, STAND_TIRED, TAUNT, PUNCH, KICK, HITREACT_MID,
            // HITREACT_HIGH, PUSHOFF, STANDUP, ROPEFLING, BOUNCE_OFF_ROPE, etc.
            return CollisionLayer::StandingSolid;
    }
}

float RingGeometry::leftX(float z) const {
    const float t = (bottom_z - top_z) <= 0.0f
                        ? 0.0f
                        : std::clamp((z - top_z) / (bottom_z - top_z), 0.0f, 1.0f);
    return top_left_x + (bottom_left_x - top_left_x) * t;
}

float RingGeometry::rightX(float z) const {
    const float t = (bottom_z - top_z) <= 0.0f
                        ? 0.0f
                        : std::clamp((z - top_z) / (bottom_z - top_z), 0.0f, 1.0f);
    return top_right_x + (bottom_right_x - top_right_x) * t;
}

float RingGeometry::centerX() const {
    return (top_left_x + top_right_x + bottom_left_x + bottom_right_x) * 0.25f;
}

float RingGeometry::centerZ() const { return (top_z + bottom_z) * 0.5f; }

float RingGeometry::clampX(float z, float x) const {
    return std::clamp(x, leftX(z), rightX(z));
}

float RingGeometry::clampZ(float z) const { return std::clamp(z, top_z, bottom_z); }

bool RingGeometry::nearRope(float z, float x, float margin) const {
    return x <= leftX(z) + margin || x >= rightX(z) - margin;
}

float distanceX(float ax, float bx) { return std::fabs(bx - ax); }

bool depthOverlap(float az, float bz, float range_z) { return std::fabs(bz - az) <= range_z; }

bool canReach(float ax, float az, float facing, float bx, float bz, float range_x, float range_z,
              bool facing_only) {
    if (range_x < 0.0f) return false;  // negative range = the state does not collide
    if (!depthOverlap(az, bz, range_z)) return false;
    if (facing_only && !isFacing(ax, facing, bx)) return false;
    return distanceX(ax, bx) <= range_x;
}

bool canStrikeConnect(float ax, float az, float facing, float bx, float bz, float range_x,
                      float range_z, float close_overlap) {
    if (range_x < 0.0f) return false;
    if (!depthOverlap(az, bz, range_z)) return false;
    const float signed_dx = (bx - ax) * (facing >= 0.0f ? 1.0f : -1.0f);
    return signed_dx >= -close_overlap && signed_dx <= range_x;
}

void separateSolidPair(const RingGeometry &ring, bool clamp_a, bool clamp_b, float min_gap,
                       float weight_a, float weight_b, float fallback_dir, float &ax, float az,
                       float &bx, float bz) {
    const float dx = bx - ax;
    const float gap = std::fabs(dx);
    if (gap >= min_gap) return;

    float dir = fallback_dir >= 0.0f ? 1.0f : -1.0f;
    if (gap > 1.0e-3f) {
        dir = dx >= 0.0f ? 1.0f : -1.0f;
    }

    const float overlap = min_gap - gap;
    const float sum = weight_a + weight_b;
    const float share_a = sum > 0.0f ? (weight_a / sum) : 0.5f;
    const float share_b = 1.0f - share_a;

    ax -= overlap * share_a * dir;
    bx += overlap * share_b * dir;
    if (clamp_a) ax = ring.clampX(az, ax);
    if (clamp_b) bx = ring.clampX(bz, bx);

    // If either wrestler hit a ring rope during the first push, transfer the
    // leftover overlap onto the other wrestler so corners never clip sprites.
    const float remaining = min_gap - std::fabs(bx - ax);
    if (remaining > 1.0e-3f) {
        const float a_room = dir > 0.0f ? (ax - ring.leftX(az)) : (ring.rightX(az) - ax);
        const float b_room = dir > 0.0f ? (ring.rightX(bz) - bx) : (bx - ring.leftX(bz));
        if (!clamp_b || b_room >= a_room) {
            bx += remaining * dir;
            if (clamp_b) bx = ring.clampX(bz, bx);
        } else {
            ax -= remaining * dir;
            if (clamp_a) ax = ring.clampX(az, ax);
        }
    }
}

bool isFacing(float ax, float facing, float bx) { return (bx - ax) * facing >= 0.0f; }

}  // namespace wrestlefest
