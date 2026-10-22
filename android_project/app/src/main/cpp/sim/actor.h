#ifndef WRESTLEFEST_SIM_ACTOR_H
#define WRESTLEFEST_SIM_ACTOR_H

// CActor: position, facing, meters and the 4-deep sub-state stack.
//
// Field names follow the offsets documented in GAMEPLAY_IMPLEMENTATION 1.3 so
// the port can be compared against the decompilation one-to-one:
//   x/y/z, facing, state_stack[4] + state_depth, region (0x6c), energy (0x78),
//   energy_display (0x74), energy_floor (0x70), motion_power, team, active.

#include "../data/game_data.h"
#include "tuning.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace wrestlefest {

// The original's eight input booleans (actor +0xa2..0xa9). Both the player and
// the AI write this struct; nothing else drives a wrestler.
struct InputFrame {
    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
    bool btn0 = false;       // held
    bool btn1 = false;       // held
    bool btn0_edge = false;  // pressed this frame
    bool btn1_edge = false;

    // Inside the 0.35 deadzone both bits of an axis are set (see 2.4), so the
    // axis is always consumed as a signed value.
    int dirX() const { return (right ? 1 : 0) - (left ? 1 : 0); }
    int dirZ() const { return (down ? 1 : 0) - (up ? 1 : 0); }
    bool anyButton() const { return btn0 || btn1; }
    bool anyButtonEdge() const { return btn0_edge || btn1_edge; }
    int buttonEdges() const { return (btn0_edge ? 1 : 0) + (btn1_edge ? 1 : 0); }
    void clear() { *this = InputFrame{}; }
};

class Actor {
public:
    virtual ~Actor() = default;

    // --- transform ---
    float x = 0.0f;
    float y = 0.0f;  // height off the floor (jumps, slams)
    float z = 0.0f;  // depth; larger = nearer the camera
    float facing = 1.0f;  // -1 left, +1 right
    int layer = 0;
    int team = 0;
    bool active = true;  // can be targeted

    // --- meters (CActor::SetEnergyDamage / AddEnergy / AutoReply*) ---
    float energy = 100.0f;
    float energy_max = 100.0f;
    float energy_floor = 0.0f;
    float energy_display = 100.0f;  // HUD bar, one-way ghost
    float motion_power = 100.0f;
    float motion_power_max = 100.0f;
    float powerup_value = 0.0f;
    float powerup_max = 100.0f;
    bool powered_up = false;

    // --- state stack (ChangeState / NextState) ---
    std::array<int, tuning::kStateStackDepth> state_stack{{state::IDLE, 0, 0, 0}};
    int state_depth = 0;
    int region = region::IN_RING;
    float anim_time = 0.0f;

    int subState() const { return state_stack[static_cast<std::size_t>(state_depth)]; }
    bool hasState(int s) const {
        for (int i = 0; i <= state_depth; ++i) {
            if (state_stack[static_cast<std::size_t>(i)] == s) return true;
        }
        return false;
    }

    // --- helpers ---
    float energyFraction() const {
        return energy_max <= 0.0f ? 0.0f : energy / energy_max;
    }
    void addEnergy(float amount) {
        energy = std::min(energy_max, std::max(energy_floor, energy + amount));
    }
    // SetEnergyDamage: the bar snaps to the new value, then decays (Tick).
    void applyDamage(float energy_damage, float motion_damage = 0.0f);
    void addMotionPower(float amount) {
        motion_power = std::min(motion_power_max, std::max(0.0f, motion_power + amount));
    }
    // One-way ghost bar (CWrestler::Tick): a drop is shown over a few frames,
    // a heal snaps the bar up immediately.
    void tickEnergyDisplay(float dt);
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_SIM_ACTOR_H
