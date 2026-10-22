#ifndef WRESTLEFEST_SIM_MATCH_H
#define WRESTLEFEST_SIM_MATCH_H

// CGame: owns the actors, the rules, the referee and the frame order of
// GAMEPLAY_IMPLEMENTATION 1.1:
//
//   for each wrestler: tick(state, animation, meters)
//   solid collision pass (pre-resolution + post-resolution)
//   then the AI writes the next InputFrame
//   then the referee / announcer / counts
//   then the win check
//
// Match is the only entry point: update(dt, inputs) - inputs has one entry per
// wrestler (player or AI), exactly like the original's eight actor booleans.

#include "../data/game_data.h"
#include "ai.h"
#include "collision.h"
#include "referee.h"
#include "wrestler.h"

#include <string>
#include <vector>

namespace wrestlefest {

enum class MatchPhase {
    Intro,     // bell / "FIGHT"
    Running,
    Finished,  // a winner exists, cutscene states playing
};

class Match {
public:
    Match() = default;

    void configure(const GameData *data, const RingGeometry &ring, int rule_id,
                   std::uint32_t seed = 0x1234567u);

    // Adds a wrestler; returns its index. Teams are 0/1 (0xff = neutral).
    int addWrestler(const WrestlerProfile *profile, int team, float x, float z, float facing);

    // Starts the match (bell). Until then the sim only runs the entrance.
    void start();

    // Entrance staging: puts every wrestler into ENTRANCE (105). The flow layer
    // holds the match in MatchPhase::Intro until it calls start().
    void playEntrance();
    // Ends the entrance cutscene: everyone back to IDLE and in his corner, but
    // the bell has not rung yet - the ring announcer still has to introduce
    // both men. start() is what rings it.
    void endEntrance();
    void setAnnouncerCue(AnnouncerCue cue) { announcer_.setCue(cue); }

    // Royal Rumble entrant timing: the first `initial_entrants` start in the
    // ring and the rest walk in from the aisle one every `interval` seconds.
    void enableStaggeredEntry(float interval, int initial_entrants);
    bool staggeredEntry() const { return entry_interval_ > 0.0f; }
    int waitingEntrants() const { return static_cast<int>(pending_entrants_.size()); }
    float nextEntryIn() const;

    // 2 VS 2 tag rules.
    void enableTagTeams(bool enabled = true) { tag_mode_ = enabled; }
    bool tagEnabled() const { return tag_mode_; }
    bool isOnApron(int index) const {
        return index >= 0 && index < static_cast<int>(apron_.size()) &&
               apron_[static_cast<std::size_t>(index)];
    }
    int partnerIndex(int index) const;
    float apronX(int team) const;
    float apronZ() const { return ring_.centerZ(); }

    void allowAutoStart(bool allow) { auto_start_ = allow; }

    // One fixed step. `inputs` is indexed like wrestlers(); missing entries are
    // treated as neutral input.
    void update(float dt, const std::vector<InputFrame> &inputs);

    // --- accessors used by Wrestler -----------------------------------------
    const GameData &data() const { return *data_; }
    const RuleInfo &rule() const { return *rule_; }
    const RingGeometry &ring() const { return ring_; }
    const CommonVariables &common() const { return data_->common(); }
    const CommonDamage &damage() const { return data_->damage(); }
    Rng &rng() { return rng_; }
    float clock() const { return clock_; }
    Wrestler *wrestler(int index);
    const Wrestler *wrestler(int index) const;
    int wrestlerCount() const { return static_cast<int>(wrestlers_.size()); }
    int mainWrestler() const { return main_wrestler_; }

    void emit(const SimEvent &event) { events_.push_back(event); }
    const std::vector<SimEvent> &events() const { return events_; }
    void clearEvents() { events_.clear(); }

    MatchPhase phase() const { return phase_; }
    int winnerTeam() const { return winner_team_; }
    int winnerIndex() const;
    int loserTeam() const { return loser_team_; }
    float timeLeft() const;
    bool isRumble() const { return rule_ != nullptr && rule_->id == 2; }
    bool isTag() const { return rule_ != nullptr && rule_->id == 1; }

    const Referee &referee() const { return referee_; }
    const RingAnnouncer &announcer() const { return announcer_; }
    int outsideCount() const { return outside_count_; }
    bool refereePinTarget(int wrestler) const { return referee_.watchingPinFor(wrestler); }

    // --- Royal Rumble elimination -------------------------------------------
    // canTossOpponentOutOfRing (Wrestler.cpp:0x2168) + IsFlyOut (0x220e).
    //
    //   side = canTossOpponentOutOfRing(victim):
    //     0  the man is still between the ropes
    //     1  he is out past the left rope   2 out past the right one
    //   The percentage roll is part of the same function in the original:
    //     chance = TossOutPercentage + (100 - TossOutPercentage) * (1 - ratio)
    //   with `ratio` the thrower's Energy/EnergyMax (TossOutDynamicBy 1) or
    //   MotionPower/MotionPowerMax (2). So a spent field tosses people out far
    //   more easily than a fresh one, which is what makes a Rumble converge.
    int tossOutSide(const Wrestler &victim, const Wrestler *attacker);
    // Rolls the toss for a hit that is driving `victim` over the ropes on side
    // `dir` (-1 left, +1 right). On success the victim starts the slide over
    // the top rope and true is returned; the caller skips its normal reaction.
    bool attemptRumbleToss(Wrestler &attacker, Wrestler &victim, float dir);
    // Number of men thrown over the top rope so far.
    int eliminations() const { return eliminations_; }

    // Ends the match immediately (used by tests and by the flow layer).
    void finish(int winner_team);

    void setAiControlled(int index, bool controlled);
    int aiDebug(int index) const { return index >= 0 && index < (int)ai_.size() ? (int)ai_[index].state : -1; }

private:
    void checkWinConditions(float dt);
    void updateGrappleBeats(float dt);
    void resolveCollisions();
    void resolveBodySeparation();
    void updateRegions();
    void updateOpponents();
    void updateRumbleEntries(float dt);
    void updateRumbleTossOuts();
    void eliminateOverRope(Wrestler &winner, Wrestler &victim, int side);
    void checkExhausted();
    void parkLateEntrants();
    void setupTagTeams();
    void tryTags(const std::vector<InputFrame> &inputs);
    bool isWaitingEntrant(int index) const;

    const GameData *data_ = nullptr;
    const RuleInfo *rule_ = nullptr;
    RingGeometry ring_{};
    std::vector<Wrestler> wrestlers_;
    Referee referee_;
    RingAnnouncer announcer_;
    Rng rng_;
    std::vector<SimEvent> events_;
    MatchPhase phase_ = MatchPhase::Intro;
    int winner_team_ = -1;
    int loser_team_ = -1;
    int main_wrestler_ = 0;
    int eliminations_ = 0;
    float clock_ = 0.0f;
    float intro_timer_ = 0.0f;
    int outside_count_ = 0;
    float outside_timer_ = 0.0f;
    bool outside_warned_ = false;
    float grapple_beat_timer_ = 0.0f;
    bool grapple_resolved_ = false;
    bool auto_start_ = true;
    float entry_interval_ = 0.0f;
    int entry_initial_ = 2;
    float entry_timer_ = 0.0f;
    std::vector<int> pending_entrants_;
    bool tag_mode_ = false;
    std::vector<bool> apron_;

    std::vector<AiEntity> ai_;
    std::vector<InputFrame> pending_inputs_;
    std::vector<bool> ai_controlled_;
    float ai_tick_ = 0.0f;
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_SIM_MATCH_H
