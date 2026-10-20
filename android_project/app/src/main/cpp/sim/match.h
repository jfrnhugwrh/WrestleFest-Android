#ifndef WRESTLEFEST_SIM_MATCH_H
#define WRESTLEFEST_SIM_MATCH_H

// CGame: owns the actors, the rules, the referee and the frame order of
// GAMEPLAY_IMPLEMENTATION 1.1:
//
//   for each wrestler: tick(state, animation, meters)
//   then the AI writes the next InputFrame
//   then the referee / counts
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

    // When true (default) Match rings the bell itself after a short intro beat.
    // The screen flow disables it and calls start() when its own entrance and
    // announcer beats are done.
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
    int loserTeam() const { return loser_team_; }
    float timeLeft() const;
    bool isRumble() const { return rule_ != nullptr && rule_->id == 2; }
    bool isTag() const { return rule_ != nullptr && rule_->id == 1; }

    const Referee &referee() const { return referee_; }
    int outsideCount() const { return outside_count_; }

    // Ends the match immediately (used by tests and by the flow layer).
    void finish(int winner_team);

    // Makes a wrestler AI-driven (index 0 is human by default). Tests use this
    // to run AI vs AI.
    void setAiControlled(int index, bool controlled);
    int aiDebug(int index) const { return index >= 0 && index < (int)ai_.size() ? (int)ai_[index].state : -1; }

private:
    void updateReferee(float dt);
    void checkWinConditions(float dt);
    void updateGrappleBeats(float dt);
    void updateDogfights();
    void updateRegions();
    void tryPinAttempts();
    void resolveCollisions();

    const GameData *data_ = nullptr;
    const RuleInfo *rule_ = nullptr;
    RingGeometry ring_{};
    std::vector<Wrestler> wrestlers_;
    Referee referee_;
    Rng rng_;
    std::vector<SimEvent> events_;
    MatchPhase phase_ = MatchPhase::Intro;
    int winner_team_ = -1;
    int loser_team_ = -1;
    int main_wrestler_ = 0;
    float clock_ = 0.0f;
    float intro_timer_ = 0.0f;
    int outside_count_ = 0;
    float outside_timer_ = 0.0f;
    bool outside_warned_ = false;
    float grapple_beat_timer_ = 0.0f;
    float pin_cooldown_ = 0.0f;
    bool grapple_resolved_ = false;
    bool auto_start_ = true;

    // AI: one entity per wrestler, index-aligned. Entity 0 is the human unless
    // setAiControlled() is called for it.
    std::vector<AiEntity> ai_;
    // Inputs the AI wrote last frame; the original ticks the actors first and
    // lets the AI fill the next frame's booleans.
    std::vector<InputFrame> pending_inputs_;
    std::vector<bool> ai_controlled_;
    float ai_tick_ = 0.0f;
    float pin_interval_ = 2.2f;
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_SIM_MATCH_H
