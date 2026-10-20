#ifndef WRESTLEFEST_SIM_TUNING_H
#define WRESTLEFEST_SIM_TUNING_H

// Engine constants recovered from the iOS binary (GAMEPLAY_IMPLEMENTATION 8).
// Everything defined here is *not* present in the SML data: it is either a float
// literal in the decompilation or a documented port decision. Anything that
// exists in the data is read from GameData instead - do not duplicate it here.

namespace wrestlefest {
namespace tuning {

// --- AI beats (seconds) ------------------------------------------------------
// Reaction beats are "base - perStrength * (strength - 1)". At strength 1 they
// are the base; at strength 11 a wrestler reacts ~4x faster (0.30 s -> 0.07 s).
constexpr float kGrappleMashStep = 0.08f;           // a press brings the next beat forward
constexpr float kRumbleTossRange = 150.0f;           // toss-outs only at the ropes
constexpr float kAiRunRange = 260.0f;               // AI sprints to close a gap this big
constexpr float kTagReach = 70.0f;                  // how close the corner must be to tag
constexpr float kTagEnergyFraction = 0.35f;         // AI tags out below this energy
constexpr float kGrappleBeat = 0.30f;              // 0x3e99999a GrappleIdleState::onEnter
constexpr float kReactionBeat = 0.30f;             // 0x3e99999a BeAttackedState::onEnter
constexpr float kReactionBeatPerStrength = -0.023f;  // 0xbcbc6a7f
constexpr float kDogfightBeat = 0.18f;             // 0x3e3851ec DogfightState::onEnter
constexpr float kDogfightBeatPerStrength = -0.0126f;  // 0xbc4e703b
constexpr float kHeldBeat = 0.10f;                 // 0x3dcccccd BeHeldState::onEnter
constexpr float kPinEscapeBase = 0.12f;            // 0x3df5c28f BePinState::onEnter
constexpr float kPinEscapeRand = 7.63e-7f;         // 0x354cce67 * (rand() & 0x7fff)

// --- Grapple struggle --------------------------------------------------------
// The original's 300 px rope force is measured in its wider ring space; at the
// 960x640 logical scale our trapezoid is ~520-680 px wide, so 300 px would make
// every clinch an automatic win. 90 px is the ropes in practice.
constexpr float kGrappleBeatForcedRange = 90.0f;  // 0x43960000 collisionRing(actor,300,facing)
constexpr int kGrappleWinRollModulus = 101;        // umod 0x65
constexpr int kGrappleWinRollThreshold = 49;       // cmp 0x31 -> win when roll > 49
constexpr int kGrappleWinRollRumble = 90;          // cmp 0x5a -> win when roll < 90

// --- Movement ---------------------------------------------------------------
// HandleAutoWalk clamps a step to 20 px per *frame* at 30 fps = 600 px/s; the
// per-wrestler WalkFactorX/RunFactorX values are the real limiter.
constexpr float kMaxWalkStepPerSecond = 600.0f;
constexpr float kSourceFps = 30.0f;  // animation frame rate of the original

// --- Energy ------------------------------------------------------------------
// The HUD bar is a one-way ghost: it only ever lags behind a *drop* (CWrestler::Tick).
constexpr float kEnergyBarDrainRate = -10.0f;  // 0xc1200000

// --- Timing / math -----------------------------------------------------------
constexpr float kFixedDelta = 1.0f / 60.0f;
constexpr int kMaxStepsPerFrame = 4;
constexpr int kStateStackDepth = 4;
constexpr int kNoState = 0xff;

// --- AI reaction windows (GAMEPLAY_IMPLEMENTATION 5.1) ------------------------
constexpr float kRingSideReaction = 0.5f;   // RingSideState delay before climbing in
constexpr float kSearchEnemyInterval = 0.25f;
constexpr float kOutsideCountReaction = 10.0f;  // ref count > 10 -> AI leaves the floor

// --- Port decisions ----------------------------------------------------------
// Strike impact frame: the original's per-state tick bodies are jump tables, so
// the frame at which a punch lands is calibrated (GAMEPLAY_IMPLEMENTATION 11.3).
// 2 of 4 frames matches the arm crossing the target's head.
constexpr float kStrikeImpactFraction = 0.5f;
// Damage applies at this fraction of a signature move when HurtTimming is absent.
constexpr float kForceImpactFraction = 0.45f;

}  // namespace tuning
}  // namespace wrestlefest

#endif  // WRESTLEFEST_SIM_TUNING_H
