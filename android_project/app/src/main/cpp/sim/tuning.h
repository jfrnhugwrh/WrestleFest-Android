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
// A Rumble elimination only happens at the ropes: the man has to be within
// this many pixels of one (about a quarter of the ring's width - a throw
// carries him the rest of the way) and be driven into it for the toss to be on.
constexpr float kRumbleTossRange = 140.0f;
// In a Rumble the AI lines up on the side of its victim that faces the middle
// of the ring, so its throws drive him outwards. This is how far short of the
// grapple's reach it stands (a fraction of the grapple range).
constexpr float kRumbleHerdOffset = 0.75f;

// --- Ring exit / Royal Rumble elimination -------------------------------------
// A wrestler who is knocked towards a rope can be sent over the top rope. He
// then slides out to ringside; `kRingsideWidth` is how far past the rope he
// lands, `kFlyOutSpeed` the speed of the slide (a fraction of the shipped
// AirborneFactorX 550, the same number the Irish whip uses).
constexpr float kRingsideWidth = 120.0f;
constexpr float kFlyOutSpeedFactor = 0.75f;
// How far past the rope a wrestler has to be before he counts as "outside"
// (region::OUTSIDE): the ropes themselves are not enough, he has to be down on
// the floor. Used for the outside count in the pinning rules.
constexpr float kOutsideLineWidth = 45.0f;
constexpr float kAiRunRange = 260.0f;               // AI sprints to close a gap this big
constexpr float kTagReach = 70.0f;                  // how close the corner must be to tag
constexpr float kTagEnergyFraction = 0.35f;         // AI tags out below this energy
constexpr float kGrappleBeat = 0.30f;               // 0x3e99999a GrappleIdleState::onEnter
constexpr float kReactionBeat = 0.30f;              // 0x3e99999a BeAttackedState::onEnter
constexpr float kReactionBeatPerStrength = -0.023f;  // 0xbcbc6a7f
constexpr float kDogfightBeat = 0.18f;              // 0x3e3851ec DogfightState::onEnter
constexpr float kDogfightBeatPerStrength = -0.0126f;  // 0xbc4e703b
constexpr float kHeldBeat = 0.10f;                  // 0x3dcccccd BeHeldState::onEnter
constexpr float kPinEscapeBase = 0.12f;             // 0x3df5c28f BePinState::onEnter
constexpr float kPinEscapeRand = 7.63e-7f;          // 0x354cce67 * (rand() & 0x7fff)

// --- Grapple struggle & anti-soft-lock caps ----------------------------------
constexpr float kGrappleBeatForcedRange = 90.0f;    // 0x43960000 collisionRing(actor,300,facing)
constexpr int kGrappleWinRollModulus = 101;         // umod 0x65
constexpr int kGrappleWinRollThreshold = 49;        // cmp 0x31 -> win when roll > 49
constexpr int kGrappleWinRollRumble = 90;           // cmp 0x5a -> win when roll < 90
constexpr float kMaxClinchDuration = 2.0f;          // BaseInfo MaxBeHeadlockedTime upper bound
constexpr int kMaxNeutralComboHits = 3;             // standing strike string knockdown cap
constexpr int kMaxClinchStrikes = 2;                // neutral clinch knee/punch cap before break
constexpr float kComboResetWindow = 0.85f;          // combo counter decay between exchanges
constexpr float kGrappleReengageCooldown = 0.35f;   // prevents frame-1 re-lock after pushoff

// --- Movement ---------------------------------------------------------------
constexpr float kGameSpeedScale = 0.78f;
constexpr float kMaxWalkStepPerSecond = 600.0f;
constexpr float kSourceFps = 30.0f;  // animation frame rate of the original

// --- Energy ------------------------------------------------------------------
constexpr float kEnergyBarDrainRate = -10.0f;  // 0xc1200000

// --- Timing / math -----------------------------------------------------------
constexpr float kFixedDelta = 1.0f / 60.0f;
constexpr int kMaxStepsPerFrame = 4;
constexpr int kStateStackDepth = 4;
constexpr int kNoState = 0xff;

// --- AI reaction windows (GAMEPLAY_IMPLEMENTATION 5.1) -----------------------
constexpr float kRingSideReaction = 0.5f;
constexpr float kSearchEnemyInterval = 0.25f;
constexpr float kOutsideCountReaction = 10.0f;

// --- Solid body & close-quarters combat geometry (0.72x logical space) -------
// At kArtScale = 0.72, standing wrestler sprites span ~84-91 px across the
// shoulders/arms, while punch_c/kick_b reach +97..+103 px and StateTable reach
// values are GRAPPLE=100, PUNCH=110, KICK=120. Keeping solid standing bodies at
// 80 px prevents torso clipping while placing fists/boots squarely on target.
constexpr float kStrikeImpactFraction = 0.5f;
constexpr float kForceImpactFraction = 0.45f;
constexpr float kVictimHoldMax = 3.0f;
constexpr float kBodySeparation = 80.0f;
constexpr float kCloseStrikeOverlap = 24.0f;
constexpr float kStrikePushback = 14.0f;
constexpr float kKnockdownPushback = 36.0f;

// --- Referee movement & officiating (CRef::StateOnEnterRef / StateTickRef) ---
constexpr float kRefWalkSpeedX = 225.0f;
constexpr float kRefWalkSpeedZ = 250.0f;
constexpr float kRefTrackDeadbandX = 75.0f;   // 0x43160000 (150) scaled to 960-wide ring
constexpr float kRefTrackDeadbandZ = 65.0f;   // 0x432f0000 (175) scaled to 192-deep ring
constexpr float kRefArriveEpsilon = 8.0f;     // 0x41200000 arrival gate on both X and Z
constexpr float kRefPinOffsetX = 132.0f;      // places kneeling hand cleanly beside shoulders
constexpr float kRefCanvasDropTime = 0.20f;   // 6 frames at 30 fps for countdown (1).png
constexpr float kPairedOffsetScale = 0.78f;   // aligns paired grapple/throw hands at 0.72x scale

// The original's input bits 0xa8/0xa9 are *edge* pulses, so its mash counters
// count real presses, not held frames.
constexpr float kAiMashPeriod = 0.12f;

}  // namespace tuning
}  // namespace wrestlefest

#endif  // WRESTLEFEST_SIM_TUNING_H
