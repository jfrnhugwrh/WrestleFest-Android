#include "referee.h"

#include "match.h"

#include <algorithm>
#include <cmath>

namespace wrestlefest {
namespace {
// CRef::StateOnEnterRef case 2 - the referee stands 280 px on the far side of
// the wrestler he is watching (0x438c0000 == 280.0f).
constexpr float kRefStandOff = 280.0f;
constexpr float kRefWalkSpeed = 260.0f;
constexpr float kRefCountBeat = 0.9f;  // one "1..2..3" beat per second-ish
}  // namespace

void Referee::setup(const GameData *data, const RingGeometry *ring, int max_count) {
    data_ = data;
    ring_ = ring;
    max_count_ = max_count;
    master_ = RefMaster::Watch;
    sub_ = RefSub::None;
    count_ = 0;
    count_timer_ = 0.0f;
    reaction_delay_ = 0.0f;
    watched_actor_ = -1;
    if (ring != nullptr) {
        x = ring->centerX();
        z = ring->top_z + 40.0f;
    }
    facing = -1.0f;
}

void Referee::moveTo(float target_x, float target_z, float dt) {
    const float dx = target_x - x;
    const float dz = target_z - z;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len > 0.5f) {
        const float step = std::min(len, kRefWalkSpeed * dt);
        x += dx / len * step;
        z += dz / len * step;
        if (std::fabs(dx) > 1.0f) facing = dx > 0.0f ? 1.0f : -1.0f;
    }
    if (ring_ != nullptr) {
        z = ring_->clampZ(z);
        x = ring_->clampX(z, x);
    }
}

void Referee::watch(Match &match) {
    // WATCH scan: a pinned main wrestler first, then a submission, then a
    // tag-break. The scan order is the original's.
    for (int i = 0; i < match.wrestlerCount(); ++i) {
        Wrestler *w = match.wrestler(i);
        if (w == nullptr || w->isDead()) continue;
        if (w->pinned) {
            watched_actor_ = i;
            sub_ = RefSub::PinCount;
            count_ = 0;
            count_timer_ = 0.0f;
            master_ = RefMaster::Move;
            const float target_x = w->x - w->facing * kRefStandOff;
            const float target_z = w->z;
            const float clamped = ring_ != nullptr ? ring_->clampX(target_z, target_x) : target_x;
            if (std::fabs(clamped - target_x) > 1.0f) {
                // Blocked by the ropes: stand on the other side.
                x = w->x + w->facing * kRefStandOff;
            }
            moveTo(clamped, target_z, 0.0f);
            return;
        }
    }
    for (int i = 0; i < match.wrestlerCount(); ++i) {
        Wrestler *w = match.wrestler(i);
        if (w == nullptr) continue;
        if (w->subState() == state::BE_USED_S_WS) {
            watched_actor_ = i;
            sub_ = RefSub::GiveUp;
            master_ = RefMaster::Move;
            const float target_x = w->x - w->facing * kRefStandOff;
            const float target_z = std::min(w->z + 150.0f,
                                            ring_ != nullptr ? ring_->bottom_z : w->z + 150.0f);
            moveTo(target_x, target_z, 0.0f);
            return;
        }
    }
}

void Referee::tick(float dt, Match &match) {
    if (!match.rule().have_referee) return;

    switch (master_) {
        case RefMaster::Idle:
            master_ = RefMaster::Watch;
            break;
        case RefMaster::Watch: {
            watched_actor_ = -1;
            sub_ = RefSub::None;
            count_ = 0;
            reaction_delay_ += dt;
            if (reaction_delay_ >= 0.25f) {
                reaction_delay_ = 0.0f;
                watch(match);
            }
            break;
        }
        case RefMaster::Move: {
            Wrestler *target = match.wrestler(watched_actor_);
            if (target == nullptr) {
                master_ = RefMaster::Watch;
                break;
            }
            if (sub_ == RefSub::PinCount) {
                if (!target->pinned) {
                    master_ = RefMaster::Watch;
                    break;
                }
                const float target_x = target->x - target->facing * kRefStandOff;
                moveTo(ring_ != nullptr ? ring_->clampX(target->z, target_x) : target_x, target->z,
                       dt);
                const float dist = std::fabs(target_x - x);
                if (dist < 12.0f) {
                    count_timer_ += dt;
                    if (count_timer_ >= kRefCountBeat && count_ < max_count_) {
                        count_timer_ -= kRefCountBeat;
                        ++count_;
                        match.emit({EventType::RefCount, watched_actor_, -1,
                                    static_cast<float>(count_), state::USE_S_WS});
                    }
                }
            } else if (sub_ == RefSub::GiveUp) {
                if (target->subState() != state::BE_USED_S_WS) {
                    master_ = RefMaster::Watch;
                    break;
                }
                const float target_x = target->x - target->facing * kRefStandOff;
                moveTo(target_x, std::min(target->z + 150.0f, ring_->bottom_z), dt);
            }
            break;
        }
        case RefMaster::Outside: {
            // Outside count (CHUDNumber against InGameTable.MaxOutsideCount).
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
    }
}

}  // namespace wrestlefest
