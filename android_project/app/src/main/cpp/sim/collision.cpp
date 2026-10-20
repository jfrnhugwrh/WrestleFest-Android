#include "collision.h"

#include <algorithm>
#include <cmath>

namespace wrestlefest {

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

bool isFacing(float ax, float facing, float bx) { return (bx - ax) * facing >= 0.0f; }

}  // namespace wrestlefest
