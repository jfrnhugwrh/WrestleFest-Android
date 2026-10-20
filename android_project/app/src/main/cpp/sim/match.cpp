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
    entry_interval_ = 0.0f;
    entry_timer_ = 0.0f;
    pending_entrants_.clear();
    apron_.clear();
    tag_mode_ = false;
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
    apron_.push_back(false);
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

    // Royal Rumble: hold the late entrants off the camera until their cue.
    // They are parked in the aisle, not targetable, so the AI never looks at a
    // wrestler who is not in the match yet.
    parkLateEntrants();
    setupTagTeams();
    entry_timer_ = 0.0f;

    for (int i = 0; i < static_cast<int>(wrestlers_.size()); ++i) {
        Wrestler &w = wrestlers_[static_cast<std::size_t>(i)];
        if (isWaitingEntrant(i)) continue;
        w.changeState(state::IDLE, 0, *this);
    }
}

void Match::enableStaggeredEntry(float interval, int initial_entrants) {
    entry_interval_ = std::max(0.0f, interval);
    entry_initial_ = initial_entrants;
    // Before the bell the card can already be parked, so the entrance never
    // shows six men standing in the ring at once.
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
        // Walk in from the aisle to the left apron, facing the ring.
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
    updateRumbleEntries(dt);
    updateOpponents();
    // Tags are resolved before the actors tick so the blue press makes the
    // swap instead of turning into a strike. Exhaustion is checked on the same
    // pre-tick meters so a heal cannot mask an empty bar.
    tryTags(inputs);
    checkExhausted();

    if (phase_ == MatchPhase::Intro && auto_start_) {
        intro_timer_ += dt;
        // Entrance/FIGHT beat: ~1.6 s, then the bell.
        if (intro_timer_ >= 1.6f) start();
    }

    // 1. Simulation first (the original ticks actors before the AI).
    for (int i = 0; i < wrestlerCount(); ++i) {
        Wrestler *w = wrestler(i);
        if (w == nullptr) continue;
        // An entrant still in the aisle and the man on the apron are frozen:
        // no ticking, no AI, no clamp.
        if (isWaitingEntrant(i) || isOnApron(i)) continue;
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
        // Keep all in-ring states on the inside of the sloped rope plane. Most
        // movement clamps locally, but paired move offsets and rebound states
        // can also displace actors; this final sweep prevents a frame of rope
        // penetration without affecting rumble eliminations or apron actors.
        if (w->region == region::IN_RING || w->region == region::IN_RING_ALONE) {
            w->z = ring_.clampZ(w->z);
            w->x = ring_.clampX(w->z, w->x);
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
    // Just outside the near corner, where the aisle walk-in starts.
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
        // The team mate starts on the apron, frozen and untargetable.
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
        if (w->isBusy() || w->held || w->pinned) continue;  // no tag mid-move
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

        // Swap: the tagger steps onto the apron, the partner climbs in.
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

        // Drop the stale press that made the tag, otherwise the fresh man is
        // still holding blue on his first frame and tags straight back.
        if (i < static_cast<int>(pending_inputs_.size())) {
            pending_inputs_[static_cast<std::size_t>(i)].btn1_edge = false;
        }
        if (partner < static_cast<int>(pending_inputs_.size())) {
            pending_inputs_[static_cast<std::size_t>(partner)].btn1_edge = false;
        }

        // The fresh man faces the nearest enemy straight away.
        updateOpponents();
        emit({EventType::Announcer, partner, i, 0.0f, state::IDLE});
        emit({EventType::Tag, partner, i, 0.0f, state::IDLE});
        return;  // one tag per frame
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

        // Royal Rumble: over the top rope. Whoever loses the exchange goes out
        // when the rule's TossOutPercentage roll succeeds and the pair is at
        // the ropes - the only way a Rumble ends (GAMEPLAY_IMPLEMENTATION 4.4).
        Wrestler *beat_winner = win ? w : other;
        Wrestler *beat_loser = win ? other : w;
        if (isRumble() && rule_ != nullptr) {
            const float toss_roll = static_cast<float>(rng_.range(1000)) * 0.1f;  // 0.0-99.9
            if (toss_roll < rule_->toss_out_percentage &&
                ring_.nearRope(beat_loser->z, beat_loser->x, tuning::kRumbleTossRange)) {
                const int loser_id = beat_loser->id();
                const int winner_id = beat_winner->id();
                beat_loser->knockOut(*this);
                emit({EventType::KnockedOut, loser_id, winner_id, toss_roll, state::LOSE});
                continue;
            }
        }

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

void Match::checkExhausted() {
    for (int i = 0; i < wrestlerCount(); ++i) {
        Wrestler *w = wrestler(i);
        if (w == nullptr || w->isDead() || isOnApron(i) || isWaitingEntrant(i)) continue;
        if (w->energy > 0.0f) continue;
        w->knockOut(*this);
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
