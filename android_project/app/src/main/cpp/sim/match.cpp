#include "match.h"

#include <algorithm>
#include <cmath>

namespace wrestlefest {
namespace {

float lengthX(float a, float b) { return std::fabs(a - b); }

}  // namespace

void Match::configure(const GameData *data, const RingGeometry &ring, int rule_id,
                      std::uint32_t seed) {
    data_ = data;
    ring_ = ring;
    rule_ = data != nullptr ? data->rule(rule_id) : nullptr;
    if (rule_ == nullptr && data != nullptr && !data->rules().empty()) {
        rule_ = &data->rules().front();
    }
    rng_.seed(seed);
    wrestlers_.clear();
    ai_.clear();
    ai_controlled_.clear();
    pending_inputs_.clear();
    events_.clear();
    phase_ = MatchPhase::Intro;
    clock_ = 0.0f;
    intro_timer_ = 0.0f;
    winner_team_ = -1;
    loser_team_ = -1;
    outside_count_ = 0;
    outside_timer_ = 0.0f;
    outside_warned_ = false;
    grapple_beat_timer_ = 0.0f;
    grapple_resolved_ = false;
    auto_start_ = true;
    referee_.setup(data, &ring_, rule_ != nullptr ? rule_->max_referee_count : 3);
}

int Match::addWrestler(const WrestlerProfile *profile, int team, float x, float z, float facing) {
    const int index = static_cast<int>(wrestlers_.size());
    Wrestler wrestler;
    wrestler.setup(profile, index, team, data_, &ring_);
    wrestler.x = x;
    wrestler.z = z;
    wrestler.facing = facing;
    wrestler.region = region::IN_RING;
    wrestlers_.push_back(wrestler);
    AiEntity entity;
    entity.actor = index;
    ai_.push_back(entity);
    pending_inputs_.emplace_back();
    ai_controlled_.push_back(index != 0);  // index 0 is the human by default
    return index;
}

void Match::playEntrance() {
    if (phase_ != MatchPhase::Intro) return;
    for (Wrestler &w : wrestlers_) {
        if (w.profile() == nullptr) continue;
        w.changeState(state::ENTRANCE, 0, *this);
    }
    emit({EventType::Announcer, -1, -1, 0.0f, state::ENTRANCE});
}

void Match::start() {
    phase_ = MatchPhase::Running;
    clock_ = 0.0f;
    emit({EventType::Bell, -1, -1, 0.0f, state::IDLE});
    for (Wrestler &w : wrestlers_) {
        w.changeState(state::IDLE, 0, *this);
    }
}

Wrestler *Match::wrestler(int index) {
    if (index < 0 || index >= static_cast<int>(wrestlers_.size())) return nullptr;
    return &wrestlers_[static_cast<std::size_t>(index)];
}

const Wrestler *Match::wrestler(int index) const {
    if (index < 0 || index >= static_cast<int>(wrestlers_.size())) return nullptr;
    return &wrestlers_[static_cast<std::size_t>(index)];
}

float Match::timeLeft() const {
    if (rule_ == nullptr) return 0.0f;
    if (rule_->max_match_time >= 9000.0f) return -1.0f;  // infinite
    return std::max(0.0f, rule_->max_match_time - clock_);
}

void Match::finish(int winner_team) {
    if (phase_ == MatchPhase::Finished) return;
    phase_ = MatchPhase::Finished;
    winner_team_ = winner_team;
    loser_team_ = winner_team == 0 ? 1 : 0;
    for (Wrestler &w : wrestlers_) {
        if (w.team == winner_team) {
            w.changeState(state::VICTORY, 0, *this);
        } else {
            w.changeState(state::LOSE, 0, *this);
        }
    }
    emit({EventType::Bell, -1, -1, 0.0f, state::VICTORY});
    emit({EventType::Victory, -1, -1, static_cast<float>(winner_team), state::VICTORY});
}

void Match::setAiControlled(int index, bool controlled) {
    if (index < 0 || index >= static_cast<int>(ai_controlled_.size())) return;
    ai_controlled_[static_cast<std::size_t>(index)] = controlled;
}

void Match::updateOpponents() {
    for (int i = 0; i < wrestlerCount(); ++i) {
        Wrestler *self = wrestler(i);
        if (self == nullptr || self->isDead()) continue;
        // A wrestler in the middle of a paired move keeps its partner: the pair
        // must stay aligned until the animation finishes.
        if (self->isBusy() && self->opponentId() >= 0) continue;
        int best = -1;
        float best_distance = 0.0f;
        for (int j = 0; j < wrestlerCount(); ++j) {
            if (j == i) continue;
            Wrestler *other = wrestler(j);
            if (other == nullptr || other->isDead() || !other->active ||
                other->team == self->team) {
                continue;
            }
            const float distance = lengthX(self->x, other->x) +
                                   std::fabs(self->z - other->z) * 0.5f;
            if (best < 0 || distance < best_distance) {
                best_distance = distance;
                best = j;
            }
        }
        if (best >= 0) self->setOpponent(best);
    }
}

void Match::update(float dt, const std::vector<InputFrame> &inputs) {
    clock_ += dt;
    updateOpponents();

    if (phase_ == MatchPhase::Intro && auto_start_) {
        intro_timer_ += dt;
        // Entrance/FIGHT beat: ~1.6 s, then the bell.
        if (intro_timer_ >= 1.6f) start();
    }

    // 1. Simulation first (the original ticks actors before the AI).
    for (int i = 0; i < wrestlerCount(); ++i) {
        Wrestler *w = wrestler(i);
        if (w == nullptr) continue;
        InputFrame input;
        const bool ai_driven =
            i < static_cast<int>(ai_controlled_.size()) &&
            ai_controlled_[static_cast<std::size_t>(i)];
        if (ai_driven) {
            if (i < static_cast<int>(pending_inputs_.size())) {
                input = pending_inputs_[static_cast<std::size_t>(i)];
            }
        } else if (i < static_cast<int>(inputs.size())) {
            input = inputs[static_cast<std::size_t>(i)];
        }
        w->tick(dt, *this, input);
    }

    // 2. AI writes the inputs for the next frame.
    if (phase_ == MatchPhase::Running) {
        for (int i = 0; i < wrestlerCount(); ++i) {
            if (i >= static_cast<int>(ai_controlled_.size())) continue;
            if (!ai_controlled_[static_cast<std::size_t>(i)]) continue;
            Wrestler *w = wrestler(i);
            if (w == nullptr || w->isDead()) continue;
            InputFrame frame;
            ai_[static_cast<std::size_t>(i)].tick(dt, *this, &frame);
            if (i < static_cast<int>(pending_inputs_.size())) {
                pending_inputs_[static_cast<std::size_t>(i)] = frame;
            }
        }
    }

    // 3. Referee, counts, collisions.
    if (pin_cooldown_ > 0.0f) pin_cooldown_ = std::max(0.0f, pin_cooldown_ - dt);
    referee_.tick(dt, *this);
    updateGrappleBeats(dt);
    resolveCollisions();
    updateRegions();
    tryPinAttempts();

    // 4. Win conditions.
    if (phase_ == MatchPhase::Running) checkWinConditions(dt);
}

void Match::updateGrappleBeats(float dt) {
    grapple_beat_timer_ += dt;

    for (int i = 0; i < wrestlerCount(); ++i) {
        Wrestler *w = wrestler(i);
        if (w == nullptr) continue;
        const int s = w->subState();
        if (s != state::GRAPPLE_HIGH && s != state::GRAPPLE_LOW) continue;

        // Only the attacker runs the roll (the victim's facing points back).
        if (w->state_timer < tuning::kGrappleBeat) continue;

        Wrestler *other = wrestler(w->opponentId());
        if (other == nullptr) continue;

        // GrappleIdleState::onTick case 7 - verbatim rules:
        //   * within 300 px of a rope, facing it, the actor always wins;
        //   * otherwise rand()%101 > 49 wins (51/101);
        //   * in a Royal Rumble a second roll rand()%101 < 90 also wins.
        bool win = false;
        if (isRumble()) {
            win = rng_.range(tuning::kGrappleWinRollModulus) > tuning::kGrappleWinRollThreshold ||
                  rng_.range(tuning::kGrappleWinRollModulus) < tuning::kGrappleWinRollRumble;
        } else {
            win = rng_.range(tuning::kGrappleWinRollModulus) > tuning::kGrappleWinRollThreshold;
        }
        if (ring_.nearRope(w->z, w->x, tuning::kGrappleBeatForcedRange) &&
            isFacing(w->x, w->facing, w->facing > 0 ? ring_.rightX(w->z) : ring_.leftX(w->z))) {
            win = true;
        }
        w->state_timer = 0.0f;

        if (win) {
            Wrestler *attacker = w;
            Wrestler *victim = other;
            if (attacker->profile() != nullptr) {
                const StatusSlot &slot = attacker->profile()->status[1];
                for (int k = 0; k < slot.list_count && k < 4; ++k) {
                    const int move_id = slot.attack[static_cast<std::size_t>(k)];
                    if (move_id < 0) continue;
                    const MoveInfo *move = data_->moveInfo(move_id);
                    if (move == nullptr) continue;
                    attacker->beginMove(move_id);
                    attacker->useWrestleStyle(*move, *this);
                    emit({EventType::GrappleWon, attacker->id(), victim->id(), 0.0f,
                          attacker->subState()});
                    break;
                }
            }
        } else {
            // Royal Rumble toss-out: the loser of the beat is thrown over the top
            // rope when the rule's TossOutPercentage roll succeeds. This is the
            // only way a rumble ends (GAMEPLAY_IMPLEMENTATION 4.4).
            if (isRumble() && rule_ != nullptr) {
                const float roll = static_cast<float>(rng_.range(1000)) * 0.1f;  // 0.0-99.9
                if (roll < rule_->toss_out_percentage) {
                    w->knockOut(*this);
                    emit({EventType::KnockedOut, w->id(), other->id(), roll, state::LOSE});
                    continue;
                }
            }

            // The victim reverses: the tables give both sides the same slot.
            Wrestler *attacker = other;
            Wrestler *victim = w;
            emit({EventType::GrappleLost, w->id(), other->id(), 0.0f, w->subState()});
            if (attacker->profile() != nullptr) {
                const StatusSlot &slot = attacker->profile()->status[1];
                for (int k = 0; k < slot.list_count && k < 4; ++k) {
                    const int move_id = slot.attack[static_cast<std::size_t>(k)];
                    if (move_id < 0) continue;
                    const MoveInfo *move = data_->moveInfo(move_id);
                    if (move == nullptr) continue;
                    attacker->beginMove(move_id);
                    attacker->useWrestleStyle(*move, *this);
                    break;
                }
            }
            (void)victim;
        }
    }
}

void Match::resolveCollisions() {
    for (int i = 0; i < wrestlerCount(); ++i) {
        Wrestler *a = wrestler(i);
        if (a == nullptr) continue;
        for (int j = i + 1; j < wrestlerCount(); ++j) {
            Wrestler *b = wrestler(j);
            if (b == nullptr || b->team == a->team) continue;
            if (a->subState() != state::RUN || b->subState() != state::RUN) continue;
            if (!depthOverlap(a->z, b->z, data_->common().collision_range_z)) continue;
            if (lengthX(a->x, b->x) > 80.0f) continue;

            // Two running wrestlers clash: both go to DOGFIGHT_STANDS and the
            // pair resolves by DogfightPercentage.
            if (a->subState() != state::DOGFIGHT_STANDS) {
                a->changeState(state::DOGFIGHT_STANDS, 0, *this);
            }
            if (b->subState() != state::DOGFIGHT_STANDS) {
                b->changeState(state::DOGFIGHT_STANDS, 0, *this);
            }
            a->setOpponent(j);
            b->setOpponent(i);
            a->dogfight_beat = tuning::kDogfightBeat;
            b->dogfight_beat = tuning::kDogfightBeat;
            emit({EventType::CollisionDogfight, a->id(), b->id(), 0.0f, state::DOGFIGHT_STANDS});
        }
    }

    for (Wrestler &w : wrestlers_) {
        if (w.subState() == state::DOGFIGHT_STANDS) {
            if (w.state_timer < tuning::kDogfightBeat) continue;
            Wrestler *other = wrestler(w.opponentId());
            if (other == nullptr) {
                w.changeState(state::IDLE, 0, *this);
                continue;
            }
            const float roll = static_cast<float>(rng_.range(100));
            // Winner punches through, the loser is knocked back (5.3: the pair
            // routes to DOGFIGHT_PUNCH/PUNCHED and the loser goes to
            // KNOCKED_BACK).
            if (roll < data_->common().dogfight_percentage) {
                w.changeState(state::DOGFIGHT_PUNCH, 0, *this);
                other->changeState(state::KNOCKED_BACK, 0, *this);
                other->applyDamage(2.0f);
            } else {
                other->changeState(state::DOGFIGHT_PUNCH, 0, *this);
                w.changeState(state::KNOCKED_BACK, 0, *this);
                w.applyDamage(2.0f);
            }
            emit({EventType::KnockDown, -1, -1, roll, state::DOGFIGHT_PUNCH});
            w.dogfight_beat = 0.0f;
        }
    }
}

void Match::updateRegions() {
    // Region bookkeeping. Ropes/climbing are not simulated yet (the state table
    // has the CLIMB* states for it); what matters here is the outside count.
    bool anyone_outside = false;
    for (Wrestler &w : wrestlers_) {
        if (w.region == region::OUTSIDE) anyone_outside = true;
    }
    if (anyone_outside) {
        outside_timer_ += tuning::kFixedDelta;
        if (outside_timer_ >= 1.0f) {
            outside_timer_ -= 1.0f;
            if (outside_count_ < (rule_ != nullptr ? rule_->max_outside_count : 20)) {
                ++outside_count_;
                emit({EventType::RefCount, -1, -1, static_cast<float>(outside_count_),
                      state::CLIMBOUT});
            }
        }
    } else {
        outside_timer_ = 0.0f;
        outside_count_ = 0;
    }
}

void Match::tryPinAttempts() {
    if (rule_ == nullptr || !rule_->can_pin) return;
    if (pin_cooldown_ > 0.0f) return;

    for (int i = 0; i < wrestlerCount(); ++i) {
        Wrestler *attacker = wrestler(i);
        if (attacker == nullptr || attacker->isDead() || attacker->play_dead) continue;
        if (attacker->subState() != state::IDLE && attacker->subState() != state::WALK) continue;
        Wrestler *victim = wrestler(attacker->opponentId());
        if (victim == nullptr || victim->team == attacker->team) continue;
        if (!victim->isDown() || victim->pinned) continue;
        // A pin is attempted on a wrestler with no energy left to fight out;
        // the mash goal from ButtonCountTable decides the kick-out.
        // Pins are attempted on a wrestler with no fight left; the arcade
        // original only pins when the energy bar is empty.
        if (victim->energy > 6.0f) continue;
        attacker->changeState(state::PINNING, 0, *this);
        victim->changeState(state::BE_PINNED, 0, *this);
        victim->pinned = true;
        victim->pin_attempts = victim->energy <= 1.0f ? victim->pin_attempts + 1 : 0;
        victim->setOpponent(attacker->id());
        // Do not let a pin be re-attempted every frame after a kick-out.
        pin_cooldown_ = pin_interval_;
        emit({EventType::PinStarted, victim->id(), attacker->id(),
              static_cast<float>(victim->energy), state::BE_PINNED});
        return;
    }
}

void Match::checkWinConditions(float dt) {
    (void)dt;

    // Royal Rumble: last wrestler still standing wins. Each entrant is his own
    // side, so the team checks below do not apply.
    if (isRumble()) {
        int alive = 0;
        int winner = -1;
        for (int i = 0; i < wrestlerCount(); ++i) {
            const Wrestler *w = wrestler(i);
            if (w == nullptr || w->isDead()) continue;
            ++alive;
            winner = i;
        }
        if (alive <= 1 && wrestlerCount() > 1) {
            finish(winner >= 0 ? wrestler(winner)->team : 0);
        }
        return;
    }

    // 1. Pinfall: the referee reached MaxRefereeCount with a wrestler pinned.
    if (rule_ != nullptr && rule_->can_pin && referee_.count() >= referee_.maxCount()) {
        for (int i = 0; i < wrestlerCount(); ++i) {
            Wrestler *w = wrestler(i);
            if (w != nullptr && w->pinned) {
                finish(w->team == 0 ? 1 : 0);
                return;
            }
        }
    }

    // 2. Knockout / submission give-up: every wrestler of a team is out.
    for (int team = 0; team < 2; ++team) {
        bool any_alive = false;
        bool team_present = false;
        for (const Wrestler &w : wrestlers_) {
            if (w.team != team) continue;
            team_present = true;
            if (!w.isDead()) any_alive = true;
        }
        if (team_present && !any_alive) {
            finish(team == 0 ? 1 : 0);
            return;
        }
    }

    // 3. Count-out.
    if (outside_count_ >= (rule_ != nullptr ? rule_->max_outside_count : 20)) {
        // Everyone outside: no elimination, the count restarts (the original
        // fires the "all outside" listener instead).
        outside_count_ = 0;
    }

    // 4. Time limit: decision by remaining energy.
    if (rule_ != nullptr && rule_->max_match_time < 9000.0f &&
        clock_ >= rule_->max_match_time) {
        float energy0 = 0.0f;
        float energy1 = 0.0f;
        for (const Wrestler &w : wrestlers_) {
            if (w.team == 0) {
                energy0 += w.energy;
            } else {
                energy1 += w.energy;
            }
        }
        finish(energy0 >= energy1 ? 0 : 1);
    }
}

}  // namespace wrestlefest
