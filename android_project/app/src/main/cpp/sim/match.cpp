#include "match.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace wrestlefest {
namespace {

float lengthX(float a, float b) { return std::fabs(a - b); }

// Mobility weights for solid separation: walking/running actors yield slightly
// more than idle/stunned ones so pushing an opponent into the corner smoothly
// slides both bodies instead of clipping through an unmoving hitbox.
float actorMobilityWeight(const Wrestler &w) {
    const int s = w.subState();
    if (s == state::PUNCH || s == state::KICK || s == state::HITREACT_MID ||
        s == state::HITREACT_HIGH || s == state::PUSHOFF) {
        return 0.45f;
    }
    if (s == state::IDLE || s == state::STAND_TIRED || s == state::TAUNT) {
        return 0.45f;
    }
    return 0.55f;
}

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
    eliminations_ = 0;
    outside_count_ = 0;
    outside_timer_ = 0.0f;
    outside_warned_ = false;
    grapple_beat_timer_ = 0.0f;
    grapple_resolved_ = false;
    auto_start_ = true;
    entry_interval_ = 0.0f;
    entry_timer_ = 0.0f;
    pending_entrants_.clear();
    apron_.clear();
    tag_mode_ = false;
    referee_.setup(data, &ring_, rule_ != nullptr ? rule_->max_referee_count : 3);
    announcer_.setup(data, &ring_);
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
    apron_.push_back(false);
    return index;
}

void Match::playEntrance() {
    if (phase_ != MatchPhase::Intro) return;
    announcer_.setCue(AnnouncerCue::Entrance);
    for (Wrestler &w : wrestlers_) {
        if (w.profile() == nullptr) continue;
        w.changeState(state::ENTRANCE, 0, *this);
    }
    emit({EventType::Announcer, -1, -1, 0.0f, state::ENTRANCE});
}

void Match::endEntrance() {
    if (phase_ != MatchPhase::Intro) return;
    parkLateEntrants();
    setupTagTeams();
    entry_timer_ = 0.0f;
    for (std::size_t i = 0; i < wrestlers_.size(); ++i) {
        Wrestler &w = wrestlers_[i];
        if (isWaitingEntrant(static_cast<int>(i))) continue;
        w.changeState(state::IDLE, 0, *this);
    }
    resolveBodySeparation();
}

void Match::start() {
    phase_ = MatchPhase::Running;
    clock_ = 0.0f;
    announcer_.setCue(AnnouncerCue::Hidden);
    emit({EventType::Bell, -1, -1, 0.0f, state::IDLE});

    parkLateEntrants();
    setupTagTeams();
    entry_timer_ = 0.0f;

    for (int i = 0; i < static_cast<int>(wrestlers_.size()); ++i) {
        Wrestler &w = wrestlers_[static_cast<std::size_t>(i)];
        if (isWaitingEntrant(i)) continue;
        w.changeState(state::IDLE, 0, *this);
    }
    resolveBodySeparation();
}

void Match::enableStaggeredEntry(float interval, int initial_entrants) {
    entry_interval_ = std::max(0.0f, interval);
    entry_initial_ = initial_entrants;
    if (phase_ == MatchPhase::Intro) parkLateEntrants();
}

void Match::parkLateEntrants() {
    pending_entrants_.clear();
    if (!isRumble() || entry_interval_ <= 0.0f || wrestlers_.empty()) return;
    const int initial = std::clamp(entry_initial_, 1, static_cast<int>(wrestlers_.size()));
    for (int i = initial; i < static_cast<int>(wrestlers_.size()); ++i) {
        Wrestler &w = wrestlers_[static_cast<std::size_t>(i)];
        pending_entrants_.push_back(i);
        w.active = false;
        w.x = -600.0f;
        w.z = ring_.centerZ();
        w.facing = 1.0f;
    }
}

float Match::nextEntryIn() const {
    if (pending_entrants_.empty()) return 0.0f;
    return std::max(0.0f, entry_interval_ - entry_timer_);
}

bool Match::isWaitingEntrant(int index) const {
    return std::find(pending_entrants_.begin(), pending_entrants_.end(), index) !=
           pending_entrants_.end();
}

void Match::updateRumbleEntries(float dt) {
    if (pending_entrants_.empty() || phase_ != MatchPhase::Running) return;
    entry_timer_ += dt;
    while (!pending_entrants_.empty() && entry_timer_ >= entry_interval_) {
        entry_timer_ -= entry_interval_;
        const int index = pending_entrants_.front();
        pending_entrants_.erase(pending_entrants_.begin());
        Wrestler *w = wrestler(index);
        if (w == nullptr || w->isDead()) continue;
        w->x = ring_.leftX(ring_.centerZ()) - 40.0f;
        w->z = ring_.centerZ();
        w->facing = 1.0f;
        w->active = true;
        w->changeState(state::WALK, 0, *this);
        emit({EventType::Announcer, index, -1, 0.0f, state::WALK});
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

// ---------------------------------------------------------------------------
// Royal Rumble elimination
// ---------------------------------------------------------------------------
int Match::tossOutSide(const Wrestler &victim, const Wrestler *attacker) {
    if (!isRumble() || rule_ == nullptr || data_ == nullptr) return 0;
    if (victim.isDead() || attacker == nullptr || attacker->isDead()) return 0;

    // UseToseOutAreaWidth (CommonVariableInfo, 50): how far past the rope a man
    // has to be before he counts as being on his way out.
    const float margin = data_->common().use_toss_out_area_width;
    int side = 0;
    if (victim.x < ring_.leftX(victim.z) + margin) {
        side = 1;
    } else if (victim.x > ring_.rightX(victim.z) - margin) {
        side = 2;
    } else {
        return 0;
    }

    float chance = rule_->toss_out_percentage;
    const int dynamic_by = rule_->toss_out_dynamic_by;
    if (dynamic_by == 1 || dynamic_by == 2) {
        const float ratio = dynamic_by == 1
                                ? attacker->energyFraction()
                                : (attacker->motion_power_max > 0.0f
                                       ? attacker->motion_power / attacker->motion_power_max
                                       : 1.0f);
        const float spent = 1.0f - std::clamp(ratio, 0.0f, 1.0f);
        chance += (100.0f - chance) * spent;
    }
    if (chance < 100.0f) {
        const int roll = rng_.range(101);
        if (chance < static_cast<float>(roll)) return 0;
    }
    return side;
}

bool Match::attemptRumbleToss(Wrestler &attacker, Wrestler &victim, float dir) {
    if (!isRumble() || phase_ != MatchPhase::Running) return false;
    if (victim.isDead() || victim.flying_out || !victim.active) return false;
    if (dir == 0.0f || attacker.isDead()) return false;
    // The throw has to be taking him towards a rope, and he has to be close
    // enough to it for the toss to be on (tuning::kRumbleTossRange).
    if (!ring_.nearRope(victim.z, victim.x, tuning::kRumbleTossRange)) return false;
    const float outward = dir >= 0.0f ? 1.0f : -1.0f;
    const bool towards_rope =
        outward > 0.0f ? victim.x >= ring_.rightX(victim.z) - tuning::kRumbleTossRange
                       : victim.x <= ring_.leftX(victim.z) + tuning::kRumbleTossRange;
    if (!towards_rope) return false;

    // canTossOpponentOutOfRing is tested on the man as he clears the ropes, so
    // measure where the throw is putting him, not where he is standing now.
    const float thrown_x = victim.x + outward * data_->common().use_toss_out_area_width * 2.0f;
    const float saved_x = victim.x;
    victim.x = thrown_x;
    const int side = tossOutSide(victim, &attacker);
    victim.x = saved_x;
    if (side == 0) return false;
    // IsFlyOut: the throw has to be going the same way the man is going out.
    if (outward < 0.0f && side != 1) return false;
    if (outward > 0.0f && side != 2) return false;

    victim.startFlyOut(outward, *this);
    emit({EventType::GrappleWon, attacker.id(), victim.id(), static_cast<float>(side),
          victim.subState()});
    return true;
}

void Match::eliminateOverRope(Wrestler &winner, Wrestler &victim, int side) {
    if (victim.isDead()) return;
    const int loser_id = victim.id();
    const int winner_id = winner.id();
    victim.flying_out = false;
    victim.eliminated_over_side = side;
    ++eliminations_;
    // CRoyalRumbleController::FallOut: he is out of the match the moment he
    // clears the ropes - no pin, no count, no getting back up.
    victim.knockOut(*this);
    emit({EventType::KnockedOut, loser_id, winner_id, static_cast<float>(side), state::LOSE});
}

void Match::updateRumbleTossOuts() {
    if (!isRumble() || phase_ != MatchPhase::Running || data_ == nullptr) return;
    const float margin = data_->common().use_toss_out_area_width;
    for (int i = 0; i < wrestlerCount(); ++i) {
        Wrestler *w = wrestler(i);
        if (w == nullptr || w->isDead() || !w->active) continue;
        if (!w->flying_out && !ring_.outsideRope(w->z, w->x, margin)) continue;
        // He is only gone once he has actually cleared the ropes: the slide
        // over the top is what the player has to see.
        if (!ring_.outsideRope(w->z, w->x, 2.0f)) continue;

        Wrestler *attacker = wrestler(w->opponentId());
        const int side = w->x < ring_.centerX() ? 1 : 2;
        if (attacker == nullptr || attacker->isDead() || attacker == w) {
            // Nobody is credited: he went out on his own (a missed charge, a
            // rope bounce). He is still eliminated - that is the Rumble rule.
            if (w->flying_out || ring_.outsideRope(w->z, w->x, margin)) {
                w->flying_out = false;
                w->eliminated_over_side = side;
                ++eliminations_;
                w->knockOut(*this);
                emit({EventType::KnockedOut, w->id(), -1, static_cast<float>(side), state::LOSE});
            }
            continue;
        }
        if (!w->flying_out) {
            // Reached ringside without a decided throw (a whip that carried him
            // all the way): settle it with the same roll now.
            const int rolled = tossOutSide(*w, attacker);
            if (rolled == 0) {
                w->x = ring_.clampX(w->z, w->x);
                continue;
            }
        }
        eliminateOverRope(*attacker, *w, side);
    }
}

int Match::winnerIndex() const {
    if (phase_ != MatchPhase::Finished || winner_team_ < 0) return -1;
    int fallback = -1;
    for (int i = 0; i < wrestlerCount(); ++i) {
        const Wrestler *w = wrestler(i);
        if (w == nullptr || w->team != winner_team_) continue;
        if (!w->isDead()) return i;
        if (fallback < 0) fallback = i;
    }
    return fallback;
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
    referee_.onMatchFinished(*this);
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
    if (phase_ == MatchPhase::Running) {
        clock_ += dt;
    }
    updateRumbleEntries(dt);
    updateOpponents();
    tryTags(inputs);
    checkExhausted();

    if (phase_ == MatchPhase::Intro && auto_start_) {
        intro_timer_ += dt;
        if (intro_timer_ >= 1.6f) start();
    }

    // Pre-tick solid body block so close-quarters movement/strikes resolve from
    // non-overlapping positions every single frame.
    if (phase_ == MatchPhase::Running) {
        resolveBodySeparation();
    }

    // 1. Simulation first (the original ticks actors before the AI).
    for (int i = 0; i < wrestlerCount(); ++i) {
        Wrestler *w = wrestler(i);
        if (w == nullptr) continue;
        if (isWaitingEntrant(i) || isOnApron(i)) continue;
        InputFrame input;
        if (phase_ == MatchPhase::Running) {
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
        }
        w->tick(dt, *this, input);
        if (w->region == region::IN_RING || w->region == region::IN_RING_ALONE) {
            w->z = ring_.clampZ(w->z);
            // The ropes only hold a man who is on his feet: a body that has
            // been thrown at them goes over the top and down to ringside.
            if (w->flying_out || w->canLeaveRing()) {
                w->x = ring_.clampXArena(w->z, w->x);
            } else {
                w->x = ring_.clampX(w->z, w->x);
            }
        }
    }

    // 2. AI writes the inputs for the next frame.
    if (phase_ == MatchPhase::Running) {
        for (int i = 0; i < wrestlerCount(); ++i) {
            if (i >= static_cast<int>(ai_controlled_.size())) continue;
            if (!ai_controlled_[static_cast<std::size_t>(i)]) continue;
            if (isWaitingEntrant(i) || isOnApron(i)) continue;
            Wrestler *w = wrestler(i);
            if (w == nullptr || w->isDead()) continue;
            InputFrame frame;
            ai_[static_cast<std::size_t>(i)].tick(dt, *this, &frame);
            if (i < static_cast<int>(pending_inputs_.size())) {
                pending_inputs_[static_cast<std::size_t>(i)] = frame;
            }
        }
    }

    // 3. Referee, announcer, grapple beats, solid separation, region tracking.
    referee_.tick(dt, *this);
    announcer_.tick(dt, *this);
    if (phase_ == MatchPhase::Running) {
        updateGrappleBeats(dt);
        resolveCollisions();
        resolveBodySeparation();
        updateRumbleTossOuts();
        updateRegions();
    }

    // 4. Win conditions.
    if (phase_ == MatchPhase::Running) checkWinConditions(dt);
}

int Match::partnerIndex(int index) const {
    if (!tag_mode_) return -1;
    const Wrestler *self = wrestler(index);
    if (self == nullptr || (self->team != 0 && self->team != 1)) return -1;
    for (int i = 0; i < wrestlerCount(); ++i) {
        const Wrestler *other = wrestler(i);
        if (other != nullptr && i != index && other->team == self->team) return i;
    }
    return -1;
}

float Match::apronX(int team) const {
    return team == 0 ? ring_.leftX(ring_.centerZ()) - 46.0f
                     : ring_.rightX(ring_.centerZ()) + 46.0f;
}

void Match::setupTagTeams() {
    apron_.assign(wrestlers_.size(), false);
    if (!tag_mode_) return;
    bool team_has_active[2] = {false, false};
    for (int i = 0; i < wrestlerCount(); ++i) {
        Wrestler &w = wrestlers_[static_cast<std::size_t>(i)];
        if (w.team != 0 && w.team != 1) continue;
        const int team = w.team;
        if (!team_has_active[team]) {
            team_has_active[team] = true;
            continue;
        }
        apron_[static_cast<std::size_t>(i)] = true;
        w.active = false;
        w.x = apronX(team);
        w.z = apronZ();
        w.facing = team == 0 ? 1.0f : -1.0f;
        w.region = region::RINGSIDE;
    }
}

void Match::tryTags(const std::vector<InputFrame> &inputs) {
    if (!tag_mode_ || phase_ != MatchPhase::Running) return;
    for (int i = 0; i < wrestlerCount(); ++i) {
        Wrestler *w = wrestler(i);
        if (w == nullptr || !w->active || w->isDead() || w->play_dead) continue;
        if (w->isBusy() || w->held || w->pinned) continue;
        const int partner = partnerIndex(i);
        if (partner < 0 || !isOnApron(partner)) continue;
        Wrestler *mate = wrestler(partner);
        if (mate == nullptr || mate->isDead()) continue;
        if (std::fabs(w->x - mate->x) > tuning::kTagReach ||
            std::fabs(w->z - mate->z) > tuning::kTagReach) {
            continue;
        }

        InputFrame frame;
        if (i < static_cast<int>(ai_controlled_.size()) &&
            ai_controlled_[static_cast<std::size_t>(i)]) {
            if (i < static_cast<int>(pending_inputs_.size())) {
                frame = pending_inputs_[static_cast<std::size_t>(i)];
            }
        } else if (i < static_cast<int>(inputs.size())) {
            frame = inputs[static_cast<std::size_t>(i)];
        }
        if (!frame.btn1_edge) continue;

        const int team = w->team;
        w->active = false;
        w->held = false;
        w->x = apronX(team);
        w->z = apronZ();
        w->region = region::RINGSIDE;
        w->changeState(state::IDLE, 0, *this);
        apron_[static_cast<std::size_t>(i)] = true;

        mate->active = true;
        mate->region = region::IN_RING;
        mate->x = ring_.clampX(ring_.centerZ(), apronX(team) + (team == 0 ? 40.0f : -40.0f));
        mate->z = apronZ();
        mate->facing = team == 0 ? 1.0f : -1.0f;
        mate->changeState(state::IDLE, 0, *this);
        apron_[static_cast<std::size_t>(partner)] = false;

        if (i < static_cast<int>(pending_inputs_.size())) {
            pending_inputs_[static_cast<std::size_t>(i)].btn1_edge = false;
        }
        if (partner < static_cast<int>(pending_inputs_.size())) {
            pending_inputs_[static_cast<std::size_t>(partner)].btn1_edge = false;
        }

        updateOpponents();
        emit({EventType::Announcer, partner, i, 0.0f, state::IDLE});
        emit({EventType::Tag, partner, i, 0.0f, state::IDLE});
        return;
    }
}

void Match::updateGrappleBeats(float dt) {
    grapple_beat_timer_ += dt;

    for (int i = 0; i < wrestlerCount(); ++i) {
        Wrestler *w = wrestler(i);
        if (w == nullptr) continue;
        const int s = w->subState();
        if (s != state::GRAPPLE_IDLE && s != state::GRAPPLE_HIGH && s != state::GRAPPLE_LOW) {
            continue;
        }
        if (w->state_timer < tuning::kGrappleBeat) continue;

        Wrestler *other = wrestler(w->opponentId());
        if (other == nullptr) continue;

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

        Wrestler *beat_winner = win ? w : other;
        Wrestler *beat_loser = win ? other : w;
        if (isRumble()) {
            // The man who lost the clinch is hurled at the nearest rope; over
            // the top he goes if the toss roll comes up (canTossOpponentOut
            // OfRing + IsFlyOut). attemptRumbleToss owns the state change.
            const float dir = beat_loser->x >= ring_.centerX() ? 1.0f : -1.0f;
            if (attemptRumbleToss(*beat_winner, *beat_loser, dir)) {
                emit({EventType::GrappleWon, beat_winner->id(), beat_loser->id(), dir,
                      beat_loser->subState()});
                continue;
            }
        }

        auto launchRandomGrappleMove = [&](Wrestler *attacker) -> bool {
            if (attacker->profile() == nullptr || data_ == nullptr) return false;
            const StatusSlot &slot = attacker->profile()->status[1];
            std::array<int, 4> valid{};
            int valid_count = 0;
            for (int k = 0; k < slot.list_count && k < 4; ++k) {
                const int cand = slot.attack[static_cast<std::size_t>(k)];
                if (cand >= 0 && data_->moveInfo(cand) != nullptr) {
                    valid[static_cast<std::size_t>(valid_count++)] = cand;
                }
            }
            if (valid_count <= 0) return false;
            const int chosen = valid[static_cast<std::size_t>(rng_.range(valid_count))];
            const MoveInfo *move = data_->moveInfo(chosen);
            attacker->beginMove(chosen);
            attacker->useWrestleStyle(*move, *this);
            return true;
        };

        if (win) {
            if (launchRandomGrappleMove(w)) {
                emit({EventType::GrappleWon, w->id(), other->id(), 0.0f, w->subState()});
            } else {
                w->changeState(state::PUSHOFF, 0, *this);
                other->changeState(state::PUSHOFF, 0, *this);
            }
        } else {
            emit({EventType::GrappleLost, w->id(), other->id(), 0.0f, w->subState()});
            if (!launchRandomGrappleMove(other)) {
                w->changeState(state::PUSHOFF, 0, *this);
                other->changeState(state::PUSHOFF, 0, *this);
            }
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
            if (lengthX(a->x, b->x) > tuning::kBodySeparation + 10.0f) continue;

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

void Match::resolveBodySeparation() {
    // Solid collision layer pass: enforces kBodySeparation across ALL active
    // StandingSolid states (including PUNCH, KICK, HITREACT_MID/HIGH, PUSHOFF,
    // IDLE, WALK, RUN, STAND_TIRED) with rope-clamp remainder transfer so two
    // standing sprites never overlap even in tight corners.
    const float depth_range = data_ != nullptr ? data_->common().collision_range_z : 40.0f;
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < wrestlerCount(); ++i) {
            Wrestler *a = wrestler(i);
            if (a == nullptr || !a->active || a->isDead()) continue;
            if (a->region != region::IN_RING && a->region != region::IN_RING_ALONE) continue;
            const CollisionLayer layer_a = stateCollisionLayer(a->subState());
            for (int j = i + 1; j < wrestlerCount(); ++j) {
                Wrestler *b = wrestler(j);
                if (b == nullptr || !b->active || b->isDead()) continue;
                if (b->region != region::IN_RING && b->region != region::IN_RING_ALONE) continue;
                const CollisionLayer layer_b = stateCollisionLayer(b->subState());

                if (layer_a == CollisionLayer::StandingSolid &&
                    layer_b == CollisionLayer::StandingSolid) {
                    // Don't split two opposite-team runners right as their clash
                    // enters DOGFIGHT_STANDS within the same frame.
                    if (a->subState() == state::RUN && b->subState() == state::RUN &&
                        a->team != b->team) {
                        continue;
                    }
                    if (!depthOverlap(a->z, b->z, depth_range)) continue;
                    const float fallback_dir = a->facing >= 0.0f ? 1.0f : -1.0f;
                    separateSolidPair(ring_, true, true, tuning::kBodySeparation,
                                      actorMobilityWeight(*a), actorMobilityWeight(*b),
                                      fallback_dir, a->x, a->z, b->x, b->z);
                } else if ((layer_a == CollisionLayer::StandingSolid &&
                            layer_b == CollisionLayer::Grounded && !b->pinned && !b->held) ||
                           (layer_b == CollisionLayer::StandingSolid &&
                            layer_a == CollisionLayer::Grounded && !a->pinned && !a->held)) {
                    if (!depthOverlap(a->z, b->z, depth_range)) continue;
                    const bool a_moves = layer_a == CollisionLayer::StandingSolid;
                    const float fallback_dir = a->facing >= 0.0f ? 1.0f : -1.0f;
                    separateSolidPair(ring_, true, true, 38.0f, a_moves ? 1.0f : 0.0f,
                                      a_moves ? 0.0f : 1.0f, fallback_dir, a->x, a->z, b->x, b->z);
                }
            }
        }
    }
}

void Match::updateRegions() {
    bool anyone_outside = false;
    for (int i = 0; i < wrestlerCount(); ++i) {
        Wrestler &w = wrestlers_[static_cast<std::size_t>(i)];
        // A knocked-out man, a tag partner on the apron and an entrant who has
        // not walked in yet are never counted out.
        if (w.isDead() || isOnApron(i) || isWaitingEntrant(i)) continue;
        if (w.region == region::RINGSIDE) continue;
        if (ring_.outsideRope(w.z, w.x, tuning::kOutsideLineWidth)) {
            w.region = region::OUTSIDE;
            anyone_outside = true;
        } else if (w.region == region::OUTSIDE) {
            // Back between the ropes: the referee's count starts again.
            w.region = region::IN_RING;
        }
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

void Match::checkExhausted() {
    if (rule_ != nullptr && rule_->can_pin) return;
    for (int i = 0; i < wrestlerCount(); ++i) {
        Wrestler *w = wrestler(i);
        if (w == nullptr || w->isDead() || isOnApron(i) || isWaitingEntrant(i)) continue;
        if (w->energy > 0.0f) continue;
        // In a Rumble an empty bar only ends a man's night once he is down: a
        // spent wrestler who is still on his feet can be thrown over the top
        // rope, which is the way the match is meant to be decided.
        if (isRumble() && !w->isDown() && w->canAct()) continue;
        w->knockOut(*this);
    }
}

void Match::checkWinConditions(float dt) {
    (void)dt;

    if (isRumble()) {
        if (!pending_entrants_.empty()) return;
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

    // Multi-man cards (the Road's Triple Threat and Fatal 4 Way give every man
    // his own team): the last side standing takes it. Two-sided matches fall
    // through to the pinfall and team-wipe tests below.
    {
        int sides = 0;
        int alive_sides = 0;
        int alive_team = -1;
        for (int team = 0; team < 8; ++team) {
            bool present = false;
            bool alive = false;
            for (const Wrestler &w : wrestlers_) {
                if (w.team != team) continue;
                present = true;
                if (!w.isDead()) alive = true;
            }
            if (!present) continue;
            ++sides;
            if (alive) {
                ++alive_sides;
                alive_team = team;
            }
        }
        if (sides > 2 && alive_sides <= 1) {
            finish(alive_team >= 0 ? alive_team : 0);
            return;
        }
        if (sides > 2) return;  // a multi-man card is not decided by pins yet
    }

    // 1. Pinfall: awarded strictly once the referee has arrived at the cover,
    //    dropped onto the canvas, counted all three manual slaps, and finished
    //    the 3rd mat-slap beat.
    if (rule_ != nullptr && rule_->can_pin && referee_.pinAwarded()) {
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

    // 3. Count-out: the referee reaches MaxOutsideCount and the man on the
    //    floor has lost (the original calls it there and then).
    if (outside_count_ >= (rule_ != nullptr ? rule_->max_outside_count : 20)) {
        for (int i = 0; i < wrestlerCount(); ++i) {
            const Wrestler *w = wrestler(i);
            if (w == nullptr || w->isDead() || w->region != region::OUTSIDE) continue;
            finish(w->team == 0 ? 1 : 0);
            return;
        }
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
