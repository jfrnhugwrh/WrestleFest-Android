#ifndef WRESTLEFEST_SIM_COLLISION_H
#define WRESTLEFEST_SIM_COLLISION_H

// Ring geometry + the 1-D collision model of the original.
//
// Coordinates are the original's: x horizontal, y height (jumps), z depth.
// In screen space z is the vertical position on the floor (larger z = closer to
// the camera). The ring is a trapezoid whose left/right edges interpolate with z
// (CActor::GetRingLeftX / GetRingRightX). All distances are in logical 960x640
// units - the same space the SML tables are authored in.

namespace wrestlefest {

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
    // True when x is within `margin` of either rope at depth z, used by the
    // 300 px grapple force and the rope-bounce logic.
    bool nearRope(float z, float x, float margin) const;
};

// Distance along x between two actors.
float distanceX(float ax, float bx);
// |dz| < CollisionRangeZ(depth gate).
bool depthOverlap(float az, float bz, float range_z);

// The original's per-state collision: the attacker's CollisionRangeX defines a
// symmetric interval, and the pair must be at a compatible depth. `facing_only`
// additionally requires the target to be in front of the actor (the
// `param_2 != 0` branch of CWrestler::Collision).
bool canReach(float ax, float az, float facing, float bx, float bz, float range_x, float range_z,
              bool facing_only);

// IsActor1_Face_Acto2: (bx - ax) * facing >= 0.
bool isFacing(float ax, float facing, float bx);

}  // namespace wrestlefest

#endif  // WRESTLEFEST_SIM_COLLISION_H
