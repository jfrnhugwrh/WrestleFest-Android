// Headless acceptance tests for the simulation (no GPU, no Android headers).
//
// Run: ./sim_test <assets data/Data directory>

#include "../data/game_data.h"
#include "collision.h"
#include "match.h"
#include "wrestler.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace wrestlefest;

#ifndef WRESTLEFEST_DATA_ROOT
#define WRESTLEFEST_DATA_ROOT "android_project/app/src/main/assets/data/Data"
#endif

namespace {

int g_failures = 0;

void check(bool condition, const std::string &what) {
    if (condition) {
        std::printf("  ok   %s\n", what.c_str());
    } else {
        std::printf("  FAIL %s\n", what.c_str());
        ++g_failures;
    }
}

void checkNear(float actual, float expected, float tolerance, const std::string &what) {
    const bool ok = std::fabs(actual - expected) <= tolerance;
    if (!ok) {
        std::printf("  FAIL %s (got %.4f, expected %.4f +/- %.4f)\n", what.c_str(), actual,
                    expected, tolerance);
        ++g_failures;
    } else {
        std::printf("  ok   %s (%.4f)\n", what.c_str(), actual);
    }
}

Wrestler makeWrestler(const GameData &data, const RingGeometry &ring, const char *folder) {
    const WrestlerProfile *profile = data.wrestlerByFolder(folder);
    Wrestler w;
    w.setup(profile, 0, 0, &data, &ring);
    return w;
}

}  // namespace

int main(int argc, char **argv) {
    const std::string assets = argc > 1 ? argv[1] : WRESTLEFEST_DATA_ROOT;

    // ------------------------------------------------------------------
    std::printf("[data]\n");
    DirectoryAssetSource source(assets);
    GameData data;
    std::string error;
    // Empty data root: the AssetSource already points at data/Data.
    if (!data.load(&source, "", &error)) {
        std::printf("  FAIL loading %s: %s\n", assets.c_str(), error.c_str());
        return 1;
    }
    check(data.states().size() == 117, "StateTable has 117 states");
    check(data.anims().size() == 269, "WrestlerAT has 269 animations");
    check(data.moves().size() == 95, "WrestleStyleList has 95 moves");
    check(data.rules().size() == 5, "InGameTable has 5 rule sets");
    check(data.wrestlers().size() == 38, "roster loaded");

    const StateInfo *punch = data.stateInfo(state::PUNCH);
    check(punch != nullptr && std::fabs(punch->collision_range_x - 110.0f) < 0.01f,
          "PUNCH CollisionRangeX == 110");
    const MoveInfo *punch_move = data.moveInfo(0);
    check(punch_move != nullptr && punch_move->name == "WS_PUNCH", "move 0 is WS_PUNCH");
    check(punch_move != nullptr && punch_move->entry_state == state::PUNCH,
          "WS_PUNCH resolves through the family table to state 4");

    const WrestlerProfile *cena = data.wrestlerByFolder("00_John Cena");
    check(cena != nullptr, "John Cena present");
    if (cena != nullptr) {
        check(cena->status[0].list_count == 2, "Cena status 0 lists 2 moves");
        check(cena->status[1].attack[1] == 15, "Cena's second throw is VERTICAL_SUPLEX");
        check(cena->sprites.size() > 100, "Cena sprite table loaded");
        checkNear(cena->base.walk_factor_x, 315.0f, 0.01f, "Cena WalkFactorX");
        checkNear(cena->base.heal_eps, 1.4f, 0.01f, "Cena HealEPS");
    }

    // Mash goals come straight from ButtonCountTable.
    check(data.buttonCountGoal(state::BE_PINNED, 60.0f, 0) == 9, "pin goal at 60 energy == 9");
    check(data.buttonCountGoal(state::BE_PINNED, 60.0f, 1) == 14,
          "pin goal after one failed pin == 14");
    check(data.buttonCountGoal(state::ONGROUND, 5.0f, 0) == 100,
          "a downed wrestler under 5 energy needs 100 presses");

    const RingGeometry ring;
    // ------------------------------------------------------------------
    std::printf("[reaction beats]\n");
    {
        // 0.30 - 0.023*(strength-1): strength 11 -> 0.07 s.
        Wrestler strong = makeWrestler(data, ring, "00_John Cena");
        AiEntity entity;
        checkNear(tuning::kReactionBeat + tuning::kReactionBeatPerStrength * 10.0f, 0.07f, 0.001f,
                  "BeAttacked beat at strength 11 == 0.070 s");
        checkNear(tuning::kReactionBeat, 0.30f, 0.0001f, "BeAttacked beat at strength 1 == 0.300 s");
        (void)strong;
        (void)entity;
    }

    // ------------------------------------------------------------------
    std::printf("[animation clock]\n");
    {
        // Locomotion animations loop: the walk cycle has to keep advancing (and
        // wrap) for as long as the stick is held, otherwise the sprite freezes
        // on its last frame.
        Match match;
        match.configure(&data, ring, 0, 0x1u);
        const WrestlerProfile *cena = data.wrestlerByFolder("00_John Cena");
        match.addWrestler(cena, 0, ring.centerX(), ring.centerZ(), 1.0f);
        // No opponent: the walk cycle test must not be interrupted by a hit.
        match.start();
        std::vector<InputFrame> inputs(2);
        inputs[0].right = true;
        const int first = match.wrestler(0)->spriteId();
        for (int i = 0; i < 12; ++i) match.update(1.0f / 60.0f, inputs);
        const int later = match.wrestler(0)->spriteId();
        check(match.wrestler(0)->subState() == state::WALK, "holding the stick walks");
        check(first != later, "the walk animation advances its frame");
        // Walk is 8 frames at 30 fps; two full cycles must not latch "finished".
        for (int i = 0; i < 120; ++i) match.update(1.0f / 60.0f, inputs);
        check(match.wrestler(0)->subState() == state::WALK, "the walk cycle keeps looping");
        check(match.wrestler(0)->animFraction() >= 0.0f &&
                  match.wrestler(0)->animFraction() <= 1.0f,
              "anim fraction stays normalised");
    }

    // ------------------------------------------------------------------
    std::printf("[entrance staging]\n");
    {
        Match match;
        match.configure(&data, ring, 0, 0x77u);
        match.addWrestler(data.wrestlerByFolder("00_John Cena"), 0, ring.centerX() - 100.0f,
                          ring.centerZ(), 1.0f);
        match.addWrestler(data.wrestlerByFolder("03_BigShow"), 1, ring.centerX() + 100.0f,
                          ring.centerZ(), -1.0f);
        std::vector<InputFrame> inputs(2);
        match.allowAutoStart(false);
        match.playEntrance();
        for (int i = 0; i < 60; ++i) match.update(1.0f / 60.0f, inputs);
        check(match.wrestler(0)->subState() == state::ENTRANCE, "entrance holds the wrestler");
        check(match.phase() == MatchPhase::Intro, "no bell before start()");
        match.start();
        check(match.phase() == MatchPhase::Running, "start() rings the bell");
        check(match.wrestler(0)->subState() == state::IDLE, "wrestlers go to IDLE on the bell");

        // The announced names come from each wrestler's SFXTable.
        const WrestlerProfile *cena = data.wrestlerByFolder("00_John Cena");
        check(cena != nullptr && cena->introCue() != nullptr &&
                  cena->introCue()->file_name == "RA_JR_SSN_JOHNCENA_0.wav",
              "Cena's announcer cue comes from his SFXTable");
        check(cena != nullptr && !cena->entrance.anims.empty(),
              "EntranceInfo lists animation ids");
    }

    // ------------------------------------------------------------------
    std::printf("[collision]\n");
    {
        check(canReach(400.0f, 450.0f, 1.0f, 500.0f, 450.0f, 110.0f, 40.0f, true),
              "punch reaches at 100 px");
        check(!canReach(400.0f, 450.0f, 1.0f, 520.0f, 450.0f, 110.0f, 40.0f, true),
              "punch misses at 120 px");
        check(!canReach(400.0f, 450.0f, 1.0f, 500.0f, 520.0f, 110.0f, 40.0f, true),
              "depth gate rejects dz=70");
        check(!canReach(400.0f, 450.0f, 1.0f, 300.0f, 450.0f, 110.0f, 40.0f, true),
              "facing gate rejects a target behind");
        check(ring.rightX(ring.bottom_z) - ring.leftX(ring.bottom_z) >
                  ring.rightX(ring.top_z) - ring.leftX(ring.top_z),
              "ring widens towards the camera");
    }

    // ------------------------------------------------------------------
    std::printf("[grapple roll distribution]\n");
    {
        // 10,000 seeded rolls, 1v1 vs Rumble, must match 51/101 and ~94.6%.
        auto run = [](bool rumble) {
            Rng rng(0xabcdefu);
            int wins = 0;
            for (int i = 0; i < 10000; ++i) {
                bool win = rng.range(tuning::kGrappleWinRollModulus) >
                           tuning::kGrappleWinRollThreshold;
                if (rumble && !win) {
                    win = rng.range(tuning::kGrappleWinRollModulus) < tuning::kGrappleWinRollRumble;
                }
                if (win) ++wins;
            }
            return static_cast<float>(wins) / 10000.0f;
        };
        checkNear(run(false), 51.0f / 101.0f, 0.01f, "1v1 grapple win rate ~50.5%");
        checkNear(run(true), 0.946f, 0.01f, "Rumble grapple win rate ~94.6%");
    }

    // ------------------------------------------------------------------
    std::printf("[energy ghost bar]\n");
    {
        Wrestler w = makeWrestler(data, ring, "00_John Cena");
        w.energy = 100.0f;
        w.energy_display = 100.0f;
        w.applyDamage(20.0f);
        checkNear(w.energy, 80.0f, 0.001f, "damage subtracts 20 energy");
        checkNear(w.energy_display, 100.0f, 0.001f, "the bar lags behind the hit");
        w.energy_display = 100.0f;  // pretend the bar lagged behind
        for (int i = 0; i < 30; ++i) w.tickEnergyDisplay(1.0f / 60.0f);
        checkNear(w.energy_display, 95.0f, 0.05f, "ghost bar drains at 10/s");
        w.energy = 100.0f;
        w.tickEnergyDisplay(1.0f / 60.0f);
        checkNear(w.energy_display, 100.0f, 0.001f, "healing snaps the bar up instantly");
    }

    // ------------------------------------------------------------------
    std::printf("[full AI vs AI match]\n");
    {
        Match match;
        match.configure(&data, ring, 0, 0x5eed1234u);
        const WrestlerProfile *a = data.wrestlerByFolder("00_John Cena");
        const WrestlerProfile *b = data.wrestlerByFolder("03_BigShow");
        const int id_a = match.addWrestler(a, 0, ring.centerX() - 90.0f, ring.centerZ(), 1.0f);
        const int id_b = match.addWrestler(b, 1, ring.centerX() + 90.0f, ring.centerZ(), -1.0f);
        check(id_a == 0 && id_b == 1, "two wrestlers added");
        match.setAiControlled(0, true);
        match.setAiControlled(1, true);
        match.start();

        std::vector<InputFrame> inputs(2);
        int hits = 0;
        int grapples = 0;
        int pins = 0;
        const float dt = 1.0f / 60.0f;
        float simulated = 0.0f;
        for (int step = 0; step < 60 * 400 && match.phase() != MatchPhase::Finished; ++step) {
            match.update(dt, inputs);
            simulated += dt;
            for (const SimEvent &event : match.events()) {
                switch (event.type) {
                    case EventType::Hit: ++hits; break;
                    case EventType::GrappleWon: ++grapples; break;
                    case EventType::PinStarted: ++pins; break;
                    default: break;
                }
            }
            match.clearEvents();
        }
        std::printf("  info match ran %.1fs, %d hits, %d grapple wins, %d pins, phase %d\n",
                    simulated, hits, grapples, pins, static_cast<int>(match.phase()));
        check(hits > 0, "AI lands hits");
        check(match.phase() == MatchPhase::Finished, "match reaches a decision");
        check(match.winnerTeam() >= 0, "a winner is declared");
        for (int i = 0; i < match.wrestlerCount(); ++i) {
            const Wrestler *w = match.wrestler(i);
            check(w->energy >= 0.0f && w->energy <= 100.0f, "energy stays in range");
            check(w->state_depth >= 0 && w->state_depth < tuning::kStateStackDepth,
                  "state stack stays within 4");
        }
    }

    // ------------------------------------------------------------------
    std::printf("[royal rumble]\n");
    {
        // Rule 2: no time limit, 30 % toss-out on every lost grapple beat, and
        // the last man standing wins.
        const RuleInfo *rumble = data.rule(2);
        check(rumble != nullptr && rumble->max_match_time >= 9000.0f, "the rumble has no clock");
        check(rumble != nullptr && std::fabs(rumble->toss_out_percentage - 30.0f) < 0.01f,
              "the rumble tosses out on 30 %");

        Match match;
        match.configure(&data, ring, 2, 0x1234u);
        const char *const folders[4] = {"00_John Cena", "03_BigShow", "05_Kane", "06_Bartista"};
        for (int i = 0; i < 4; ++i) {
            // Everyone for himself: the team index is meaningless in a rumble.
            match.addWrestler(data.wrestlerByFolder(folders[i]), i,
                              ring.centerX() - 150.0f + 100.0f * i, ring.centerZ(), 1.0f);
        }
        match.setAiControlled(0, true);
        match.setAiControlled(1, true);
        match.setAiControlled(2, true);
        match.setAiControlled(3, true);
        match.start();
        std::vector<InputFrame> inputs(4);
        int eliminations = 0;
        float seconds = 0.0f;
        for (int step = 0; step < 60 * 600 && match.phase() != MatchPhase::Finished; ++step) {
            match.update(1.0f / 60.0f, inputs);
            for (const SimEvent &event : match.events()) {
                if (event.type == EventType::KnockedOut) ++eliminations;
            }
            match.clearEvents();
            seconds += 1.0f / 60.0f;
        }
        std::printf("  info rumble ran %.1fs, %d eliminations, phase %d\n", seconds, eliminations,
                    static_cast<int>(match.phase()));
        check(eliminations >= 1, "wrestlers are thrown out over the top rope");

        // The rule itself: with one man left the match must be over and he must
        // be the winner. Force the remaining eliminations to keep the test fast.
        for (int i = 0; i < 3 && match.phase() != MatchPhase::Finished; ++i) {
            Wrestler *w = match.wrestler(i);
            if (w != nullptr && !w->isDead()) w->knockOut(match);
            std::vector<InputFrame> idle(4);
            match.update(1.0f / 60.0f, idle);
        }
        check(match.phase() == MatchPhase::Finished, "the rumble ends with one man standing");
        int standing = 0;
        int survivor = -1;
        for (int i = 0; i < 4; ++i) {
            if (!match.wrestler(i)->isDead()) {
                ++standing;
                survivor = i;
            }
        }
        check(standing == 1, "exactly one wrestler is left in the ring");
        check(survivor >= 0 && match.winnerTeam() == match.wrestler(survivor)->team,
              "the survivor's side wins");
    }

    // ------------------------------------------------------------------
    std::printf("[determinism]\n");
    {
        auto play = [&](std::uint32_t seed) {
            Match match;
            match.configure(&data, ring, 0, seed);
            match.addWrestler(data.wrestlerByFolder("00_John Cena"), 0, ring.centerX() - 90.0f,
                              ring.centerZ(), 1.0f);
            match.addWrestler(data.wrestlerByFolder("01_TripleH"), 1, ring.centerX() + 90.0f,
                              ring.centerZ(), -1.0f);
            match.setAiControlled(0, true);
            match.setAiControlled(1, true);
            match.start();
            std::vector<InputFrame> inputs(2);
            std::uint32_t hash = 2166136261u;
            for (int step = 0; step < 60 * 60; ++step) {
                match.update(1.0f / 60.0f, inputs);
                for (const SimEvent &event : match.events()) {
                    hash = (hash ^ static_cast<std::uint32_t>(event.type)) * 16777619u;
                    hash = (hash ^ static_cast<std::uint32_t>(event.actor + 2)) * 16777619u;
                }
                match.clearEvents();
            }
            return hash;
        };
        const std::uint32_t first = play(0x1111u);
        const std::uint32_t second = play(0x1111u);
        const std::uint32_t third = play(0x2222u);
        check(first == second, "same seed -> identical event hash");
        check(first != third, "different seed -> different match");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
