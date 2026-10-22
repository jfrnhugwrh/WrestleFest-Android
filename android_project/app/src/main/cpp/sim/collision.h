#ifndef WRESTLEFEST_SIM_COLLISION_H
#define WRESTLEFEST_SIM_COLLISION_H

// Ring geometry + collision layers + close-quarters combat geometry.
//
// Coordinates are the original's: x horizontal, y height (jumps), z depth.
// In screen space z is the vertical position on the floor (larger z = closer to
// the camera). The ring is a trapezoid whose left/right edges interpolate with z
// (CActor::GetRingLeftX / GetRingRightX). All distances are in logical 960x640
// units - the same space the SML tables are authored in.

#include "tuning.h"

#include <algorithm>

namespace wrestlefest {

// Explicit collision layers so solid hurtboxes never drop out during standing
// strikes or hit reactions, while paired throws/pins and airborne dives run
// unblocked on their own contact planes.
enum class CollisionLayer : int {
    NonInteractive = 0,  // KO'd, waiting entrant, apron partner, intro/victory
    StandingSolid = 1,   // locomotion, standing strikes, standing hit-reacts
    Paired = 2,          // grapples, headlocks, throws, submissions, pin holds
    Grounded = 3,        // flat on the mat (stomp/pin target, passable by feet)
    Airborne = 4,        // springboard / turnbuckle / fall-away flight
};

CollisionLayer stateCollisionLayer(int sub_state);

struct RingGeometry {
    float top_z = 368.0f;              // far edge (RingTopZ)
    float bottom_z = 560.0f;           // near edge (RingBottomZ)
    float top_left_x = 223.0f;         // RingTopLeftX
    float top_right_x = 738.0f;        // RingTopRightX
    float bottom_left_x = 140.0f;      // RingBottomLeftX
    float bottom_right_x = 822.0f;     // RingBottomRightX

    float leftX(float z) const;
    float rightX(float z) const;
    float centerX() const;
    float centerZ() const;
    // Clamps x into the trapezoid at depth z, and z into [top_z, bottom_z].
    float clampX(float z, float x) const;
    float clampZ(float z) const;
    // Ringside: the strip of floor outside the ropes a wrestler can be thrown
    // into. clampXArena keeps a man who is on his way out inside the camera's
    // view instead of letting him slide off the world.
    float ringsideLeftX(float z) const { return leftX(z) - tuning::kRingsideWidth; }
    float ringsideRightX(float z) const { return rightX(z) + tuning::kRingsideWidth; }
    float clampXArena(float z, float x) const {
        return std::clamp(x, ringsideLeftX(z), ringsideRightX(z));
    }
    // True when x is past the ropes by `margin` (the ring's own edge is 0).
    bool outsideRope(float z, float x, float margin) const {
        return x < leftX(z) - margin || x > rightX(z) + margin;
    }
    // True when x is within `margin` of either rope at depth z, used by the
    // 300 px grapple force and the rope-bounce logic.
    bool nearRope(float z, float x, float margin) const;
};

// Distance along x between two actors.
float distanceX(float ax, float bx);
// |dz| <= CollisionRangeZ (depth gate).
bool depthOverlap(float az, float bz, float range_z);

// The original's per-state collision: the attacker's CollisionRangeX defines a
// symmetric interval, and the pair must be at a compatible depth. `facing_only`
// additionally requires the target to be in front of the actor (the
// `param_2 != 0` branch of CWrestler::Collision).
bool canReach(float ax, float az, float facing, float bx, float bz, float range_x, float range_z,
              bool facing_only);

// Close-quarters hurtbox check for standing strikes: preserves directional hit
// detection while tolerating `close_overlap` when two solid hurtboxes are in
// chest-to-chest contact so point-blank punches/kicks never whiff through a body.
bool canStrikeConnect(float ax, float az, float facing, float bx, float bz, float range_x,
                      float range_z, float close_overlap = tuning::kCloseStrikeOverlap);

// Resolves horizontal overlap between two solid standing actors within the ring
// trapezoid. Applies mobility weights (`weight_a`, `weight_b`) and transfers any
// rope-clamped remainder onto the unpinned actor so corner exchanges never clip.
void separateSolidPair(const RingGeometry &ring, bool clamp_a, bool clamp_b, float min_gap,
                       float weight_a, float weight_b, float fallback_dir, float &ax, float az,
                       float &bx, float bz);

// IsActor1_Face_Acto2: (bx - ax) * facing >= 0.
bool isFacing(float ax, float facing, float bx);

}  // namespace wrestlefest

#endif  // WRESTLEFEST_SIM_COLLISION_H
