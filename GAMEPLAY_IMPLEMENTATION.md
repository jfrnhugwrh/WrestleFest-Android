# WrestleFest Android — Gameplay Implementation Design

**Goal:** rebuild the *WWE WrestleFest* (THQ, iOS 1.05) match simulation on Android, faithful to the original's rules, numbers and data, and structured to fit this repository.

**Sources used**

| Source | What it gave us |
|---|---|
| `WrestleFest Premium.c` (Ghidra C, 151,654 lines, 3,416 functions) | Control flow, constants, state/AI enums |
| `assets/data/Data/**/*.sml` (273 files) | All tuning tables, anim table, move tables |
| `assets/graphics/Shaders/*` + `master.tyFX` | Rendering pipeline contract |
| Existing C++/Java in this repo | Module style, asset access, viewport, audio |

Confidence markers used below: **[V]** = verified against the decompilation and/or the SML data;
**[D]** = design decision for the port (not in the original). Anything neither verified nor decided is
called out explicitly as `TODO(verify)` and collected in §11 — nothing in this document is guessed
without saying so.

---

## 0. Executive summary

The original is a **data-driven 2.5D fighting game**. Nearly every number — animation, damage, state transitions, move lists, AI timers, match rules — comes from the SML tables that are already in this repository. The engine (in the decompiled binary) supplies only:

1. a **4-deep sub-state stack** per wrestler (`ChangeState` / `NextState`),
2. a **1-D collision model** on X with a Z-depth gate, inside a **trapezoid ring**,
3. a **117-entry state table** + **269-entry animation table** binding,
4. **12 AI states** built on top of the same wrestler states,
5. **rules** (timer, outside count, pin/submission, elimination) from `InGameTable.sml`.

Porting this faithfully means: **build the data layer first, then a headless deterministic simulation, then rendering on top.** The renderer is the flashiest part but not the hardest; the simulation is where fidelity lives, and it can be developed and unit-tested on the host exactly like `viewport_manager_test.cpp` already is.

**Estimated scope:** ~9–12k lines of new C++ plus ~1.5k of host tests, in 6 phases. Phase 1–2 (data + headless sim) are the foundation and are testable without a GPU or a device.

---

## 1. How the original is put together

### 1.1 Frame flow **[V]**

```
CGame::onTick(dt)                      → per-frame game logic
  ├─ CAutoJoinInvitationHandler::Tick
  ├─ state function pointer (fade in/out)   DAT_000dbae0
  ├─ CGameObjectManager::Tick               (all actors/items, by draw layer)
  │    └─ CWrestler::Tick(dt)
  │         ├─ CWrestler::StateTick(dt)     → StateTickPlayer(dt) → per-state handler
  │         ├─ CWrestler::TickButtonCounter(dt)  (mash detection)
  │         ├─ animation advance  (frame = clamp(elapsed*fps, 0, frames-1))
  │         └─ ghost-bar drain (display −= 10·dt, one-way, §1.4)
  ├─ tyCAIController::onTick(dt)            → one tyCAIEntity::onTick per AI wrestler
  ├─ CRef::Tick / CHUDNumber (referee count)
  └─ CGame::onDraw                          → scene draw, HUD, energy bars
```

Note the ordering: **simulation before AI**, and AI reads the resulting wrestler states. The AI never drives the wrestler directly; it only calls `tyCAIEntity::setState(...)`, which maps to wrestler states/inputs.

### 1.2 Coordinates and the ring **[V]**

- Positions are `(x, y, z)`: **x = horizontal**, **y = height (jump/air)**, **z = depth** (toward/away from camera).
- The original's logical canvas is **960×640** (`tyCViewport::New(0,0,960,640,0x10,0)` in
  `CGame::onCreate`). **Every distance in the SML is in that space** — the 280 px referee stand-off,
  the 300 px rope check, the 315/600 px/s speeds, all of it.
  The repo's `ViewportManager` defaults to `480×320`, but it takes the size as a constructor
  argument, so the fix is one line: `ViewportManager(960, 640)`. Do **not** halve the data — you
  would lose a bit of precision on every constant and break the 20 px step clamp.
- The ring is a **trapezoid in screen space**: left/right edges are interpolated by `z`
  (`CActor::GetRingLeftX(z)`, `GetRingRightX(z)`), using six globals:
  `RingTopZ`, `RingBottomZ`, `RingTopLeftX`, `RingTopRightX`, `RingBottomLeftX`, `RingBottomRightX`.
  `ClampActorToRing()` maps x into that trapezoid; `ClampActorToRingSide()`, `ClampActorOutside()`,
  `ClampActorInRingAlone()` are the other three clamps used by the four "regions".
- Depth collisions use `CollisionRangeZ = 40` from `CommonVariableInfo.sml` **[V]**.

**Port consequence [D]:** put ring geometry in one place (`RingGeometry`) with the six values per arena, and expose `leftX(z)`, `rightX(z)`, `clampX(z, x)`. The values are not in the shipped `arena.sml` (which only has `Name` + `Image`), so add a small `data/Data/ArenaGeometry.sml` **authored by us** and calibrate once from an arena screenshot — see §11, item 2.

### 1.3 Actor model **[V]**

The original `CActor`/`CWrestler` uses fixed offsets. We do not need to copy offsets, but they are the best cross-reference when reading the decompiler:

| Offset | Field | Meaning |
|---|---|---|
| `+0x04 / 0x08 / 0x0c` | `x / y / z` | world position (float) |
| `+0x28` | `facing` | `-1` (left) or `+1` (right) |
| `+0x2c` | `frame` | current anim frame index |
| `+0x30` | `anim_time` | seconds elapsed in current anim |
| `+0x48` | `layer` | draw layer |
| `+0x54` | `state_stack[4]` | sub-state stack (ints) |
| `+0x64` | `state_depth` | stack top, 0..3 |
| `+0x6c` | `wrestler_state` | 0..10 region/KO enum (see §1.3 below) |
| `+0x70` | `energy_floor` | minimum energy clamp |
| `+0x74` | `energy_display` | bar value, lerps toward real at **10/s** |
| `+0x78` | `energy` | real energy |
| `+0x7c / 0x80` | `motion_max / motion` | "MotionPower" meter |
| `+0x84` | `team` | 0 / 1 (`0xff` = neutral) |
| `+0x90` | `target` | current opponent pointer |
| `+0x94` | `active` | "can be targeted" |
| `+0xa0` | `is_pinned` | being pinned |
| `+0xa1` | `is_held` | in a submission/hold |
| `+0xa2…0xa9` | `input` | **the 8 input booleans** — left, right, up, down, btn0, btn1, btn0-edge, btn1-edge (§2.4) |
| `+0xcf40` | `move_id` | index into `WrestleStyleList` currently being executed |
| `+0xcf42` | `move_sub_type` | cached `SubTypeID`, indexes the family table |
| `+0xcf45` | `strength` | 1..11, drives the reaction beats (§8) |
| `+0xcf48` | `game_clock` | seconds since the bell — the timing source for power-up |
| `+0xcf5c` | `powerup_started_at` | `game_clock` captured by `PowerUp` |
| `+0xcfe0 / 0xcfe4` | `powerup_duration / powerup_interval` | copied from `MaxPowerUpDuration` / `MaxPowerUpIntervalTime` |
| `+0xd184 / 0xd188` | `powerup_max / powerup_value` | the third meter |
| `+0xcf4c` | `actor_id` | index into actor list |
| `+0xcf55` | `powered_up` | bool |
| `+0xcf64` | `play_dead` | no inputs, no mash |
| `+0xcf68` | `suffocation` | hold damage accumulator |
| `+0xcf6c` | `pin_attempts` | # of "no-energy" pins |
| `+0xcf9c / 0xcfa0` | `walk_factor_x / z` | from `BaseInfo.sml` (315 / 270) |
| `+0xcfd0` | `gbh_value` | "got beaten hard" threshold (24) |

**State stack [V]:** depth 4, `0xff` means "empty / no animation / hidden". `ChangeState(s, chain=0)` replaces the top; `chain=1` pushes. When an animation ends on a chained state, `NextState()` pops and resets the timer. This is how "punch → hit react → recovery" and "grapple → slam → on ground" sequences are expressed, and it must be reproduced exactly, including the depth-4 assert.

**Wrestler region state (`+0x6c`) [V]:** `0` = in ring (fighting), `1` = ringside, `2` = ringside transition, `4` = outside, `6` = in ring alone, `10` = KO/dead. `SearchEnemyState` maps these to AI states (§8), and `WalkState`/`AttackState` branch on `1` and `10`. Values `3,5,7,8,9` appear only in `MasterStateOnEnter`; name them `RESERVED_3…9` in code until a use is found, rather than guessing.

### 1.4 Energy model **[V]**

Three meters, all from `BaseInfo.sml` per wrestler:

| Meter | Max field | Current | Regen | Notes |
|---|---|---|---|---|
| Energy (HP) | `Energy` (100) | `+0x78` | `HealEPS` (1.4/s) | ghost bar drains at 10/s |
| MotionPower | `MotionPower` (100) | `+0x80` | `HealMPPS` (5/s) | regen gated by `onlyWhenTired` |
| PowerUpValue | `MaxPowerUpValue` (100) | `0xd188` | `HealPUPS` (2/s) | triggers PowerUp at max |

Damage: `SetEnergyDamage()` snaps `energy_display = energy`, subtracts, clamps to `[energy_floor, max]`.

`CWrestler::Tick` (line 18393) then runs, every frame:

```c
if (energy >= display) display += dt * (-10.0f);   // 0xc1200000
if (display <  energy) display  = energy;          // clamp
```

So the bar is a **one-way ghost**: it only ever lags *behind* a drop, never eases upward. Healing
snaps it instantly because `display < energy` forces equality. Reproduce that asymmetry — it is the
reason a big slam visibly "empties" over a few frames.
Healing: `AddEnergy(dt * rate)` = `AutoReplyEnergy`. `AutoReplyMotionPower(dt, rate, onlyWhenTired)` only
regenerates while `energy < TiredValue` when the flag is set.
Power-up is a small, exact state machine worth copying verbatim (`AutoReplyPowerUpValue` 17891,
`TickPowerUp` 18069, `CWrestler::PowerUp` 18715):

```c
// accumulate while not powered, clamp to max
if (!powered_up) powerup_value = min(powerup_value + dt*HealPUPS, powerup_max);

// once per tick
if (!powered_up) {
    if (powerup_value < powerup_max) return;
    if (game_clock <= powerup_started_at + powerup_interval) return;  // 40 s cooldown
    PowerUp(true);
} else {
    if (game_clock <= powerup_started_at + powerup_duration) return;  // 20 s active
    powerup_value = max(powerup_value - powerup_max, 0);
    PowerUp(false);
}

void PowerUp(bool on) { powered_up = on; powerup_started_at = game_clock;
                        if (on) listener->onPowerUp(this); }
```

`PowerUpBonus = 10` is applied by the caller that grants meter on a hit, not inside this machine.
While powered, `CWrestler::Draw` sets the shader constant `PaletteOffset` — that is the 6-colour
blink, and it is the only visual cue the original gives.

**Port consequence [D]:** keep three plain floats + a lerp for the bar, in `actor.h`, with all rates from `BaseInfo.sml`.

### 1.5 Animation **[V]**

`WrestlerAT.sml` = 269 entries:

```
ID, AnimName, FrameCount, FrameArray[24] (frame indices, -1 padded),
xOffset, OpposingAnim, Facing, OpposingFacing,
BottomLayerPrority, UpperLayerPrority, HurtTimming
```

- Frame selection: `frame = clamp(int(anim_time * fps), 0, FrameCount-1)`, `FrameArray` maps anim frame → SST sprite index.
- `ChangeState` multiplies the actor's facing by the anim's `Facing` and applies `xOffset`
  to the opponent (`AdjustOpponentXZ`) so paired animations line up.
- `HurtTimming` is only set on **18 signature moves** (e.g. Tombstone 1.0 s, Pedigree 1.333 s,
  Million Dollar Dream 1.5 s) — it is the moment the opponent takes the hit mid-animation.
- Anim indices are shared across **all** wrestlers; the SST sprite names come from each
  wrestler's `wrestlerData.sml` (`SpriteOffset` → `"idle.sst"`, `"walk_a.sst"`, … plus x/y offsets).

### 1.6 The 117 wrestler states **[V]**

`StateTable.sml` is the state machine's vocabulary. Runtime layout (stride `0x1c`):
`+0 AnimId (S16)`, `+2 OpponentState (S16, 0xff = none)`, `+4 CollisionRangeX (F32)`,
`+8 CheckRopeHit (U8)`. `ChangeState` reads exactly these: `AnimId >= 0` multiplies the actor's
facing by `AnimTable[AnimId].Facing`, and `OpponentState != 0xff` drives the paired-actor call.

Grouped by role:

| Group | IDs | Notes |
|---|---|---|
| Locomotion | 1 `IDLE`, 2 `WALK`, 3 `RUN`, 43 `STAND_TIRED`, 98/99 `*_WITH_WEAPON` | `CollisionRangeX` 60–120 |
| Strikes | 4 `PUNCH`, 5 `KICK` | range 110 / 90 |
| Grapple | 6 `GRAPPLE_IDLE`, 7 `GRAPPLE_HIGH`, 8 `GRAPPLE_LOW` | `OpponentState` 6/8 |
| Holds | 9 `HEADLOCK`, 10 `HEADLOCKED`, 11 `HEADLOCK_TAG`, 13 `HEADLOCK_WAIT` | |
| Reactions | 14 `SLAMMED`, 15 `ONGROUND`, 16 `HITREACT_MID`, 17 `HITREACT_HIGH`, 32 `STANDUP`, 33 `ROLL`, 87 `ONGROUND_HURT` | |
| Ring movement | 18 `CLIMBTOP`, 19 `CLIMB_BOTTOM`, 20 `CLIMBOUT`, 21 `CLIMBIN`, 24 `ROPEFLING`, 88–97 climb/tag variants | |
| Rope-interacting | 27 `AIRBORNE`, 31 `PUSHOFF`, 49 `KNOCKED_BACK`, 50 `SB_CROSSEDBODY`, 56 `BE_USED_CL_DROP`, 62 `BE_USED_F_WS`, 70 `USE_SB_WS`, 80 `FA_SLAMMED_FLY` | the only 8 states with `CheckRopeHit = 1` (verified over all 117 rows) |
| Pins | 22 `JUMP_FOR_PIN`, 23 `PIN`, 85 `PINNING`, 86 `BE_PINNED` | |
| Whips / air | 25 `IRISH_WHIP`, 26 `IRISH_WHIPPED`, 27 `AIRBORNE`, 28 `AIRBORNE_OUT`, 29 `PICKUP`, 30 `PICKUPED`, 31 `PUSHOFF` | |
| Power moves | 51–80 `USE_*_WS`, 109–116 `USE_HTWS_PIN`, `USE_RFWS_*` | `USE_*` states have `AnimId = -1` → animation comes from the move table |
| Dogfight | 44–48 `DOGFIGHT_*` | the "trading blows" struggle |
| Tag | 90 `TAG_IN`, 91 `TAG_OUT` | |
| Match | 103 `LOSE`, 104 `VICTORY`, 105 `ENTRANCE` | used by intro/outro |

The table declares **117 rows, IDs 0–116** (row `0` is a `TEST` row and is never entered; row `110`
`USE_RFWS_BEGIN` is real but is easily missed when parsing because of a double space in the file).

Any `USE_*_WS` state means: **the animation and the opponent reaction come from the move table
entry**, not from the anim table — every one of them has `AnimId = -1`. That is the core of the move
system (§3).

---

## 2. Repository integration

### 2.1 What already exists and is reused as-is

| Existing | Role in the port |
|---|---|
| `asset_loader.{h,cpp}` | All data/shader/sprite reads: `readAsset("data/Data/StateTable.sml", &bytes)` |
| `obb_archive.{h,cpp}` | Ships the 373 MiB payload out of the APK (already works) |
| `viewport_manager.{h,cpp}` | Letterboxing + touch → logical mapping (already tested); construct it with `960×640` for this game (§1.2) |
| `audio_player.{h,cpp}` | SFX; needs a multi-voice mixer and a music stream (§6) |
| `main.cpp` | Becomes the app shell: owns `SceneStack`, drives EGL, forwards input |
| `CrashLogHandler` | Unchanged |
| `viewport_manager_test.cpp` | Pattern to copy for headless sim tests |

### 2.2 Proposed layout

The repo currently keeps 6 flat files in `cpp/`. Adding ~25 flat files would be unreadable, so I propose three folders — same naming style (`snake_case.cpp`, `WRESTLEFEST_*` guards, `namespace wrestlefest`):

```
android_project/app/src/main/cpp/
├── main.cpp                    (existing, shrinks to shell + loop)
├── asset_loader.*              (existing)
├── obb_archive.*               (existing)
├── audio_player.*              (existing, extended)
├── viewport_manager.*          (existing)
├── data/
│   ├── sml_parser.{h,cpp}      SML → SmlNode tree (no engine deps)
│   ├── sml_parser_test.cpp     host test over all 273 files
│   ├── game_data.{h,cpp}       typed tables + lookup
│   ├── game_data_test.cpp      schema/round-trip tests
│   └── tuning.h                engine constants (§8) with provenance comments
├── sim/
│   ├── actor.{h,cpp}           CActor: meters, facing, region state
│   ├── wrestler.{h,cpp}        CWrestler: state stack, anim, damage
│   ├── wrestle_style.{h,cpp}   move tables → state sequences
│   ├── collision.{h,cpp}       X-interval + Z gate, ring trapezoid
│   ├── referee.{h,cpp}         CRef master 0..3 + pin/give-up/tag sub-states (§4.2)
│   ├── match.{h,cpp}           InGameTable rules, timer, win conditions
│   ├── ai.{h,cpp}              12 AI states
│   └── sim_test.cpp            golden scenarios (headless)
├── render/
│   ├── sst_decoder.{h,cpp}     .sst → RGBA texture
│   ├── sprite_batch.{h,cpp}    Sprite/SpritePalette shaders, one VBO
│   ├── font_renderer.{h,cpp}   tyF bitmap fonts
│   └── arena_renderer.{h,cpp}  layered arena + ropes + crowd
└── game/
    ├── scene.{h,cpp}           scene stack (intro/entrance/match/outro/report)
    └── scenes/*.cpp            one file per screen
```

CMake changes follow the file's existing shape — the same `STATIC`-library-plus-optional-test
pattern that `viewport_manager` already uses:

```cmake
# new, above the if(ANDROID) block: the host-testable core
add_library(wrestlefest_sim STATIC
    data/sml_parser.cpp data/game_data.cpp
    sim/actor.cpp sim/wrestler.cpp sim/wrestle_style.cpp
    sim/collision.cpp sim/referee.cpp sim/match.cpp sim/ai.cpp
)
target_include_directories(wrestlefest_sim PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(wrestlefest_sim PUBLIC cxx_std_17)

if(ANDROID)
    # ... existing game-activity setup unchanged, one added link:
    target_link_libraries(wrestlefest PRIVATE
        viewport_manager wrestlefest_sim
        game-activity::game-activity_static EGL GLESv3 OpenSLES android log z
    )
    # keep the two -Wl,-u pins exactly as they are
endif()

# host tests, alongside the existing viewport test
if(VIEWPORT_BUILD_TESTS)
    add_executable(sml_parser_test data/sml_parser_test.cpp)
    add_executable(game_data_test  data/game_data_test.cpp)
    add_executable(sim_test        sim/sim_test.cpp)
    foreach(t sml_parser_test game_data_test sim_test)
        target_link_libraries(${t} PRIVATE wrestlefest_sim)
        add_test(NAME ${t} COMMAND ${t})
    endforeach()
endif()
```

`wrestlefest_sim` must have **no Android dependencies at all** — no `#include <android/*>`, no JNI, no
EGL. That mirrors how `viewport_manager` is already split out and is what makes the sim testable.

That single rule — *no Android headers in the simulation* — is what keeps it testable at 60 Hz on
the host, exactly like the viewport math is today.

### 2.3 Class model **[D]**

Small, flat, and matching the original's division of labour. No inheritance beyond the one place the
original has it (`Referee` and `Wrestler` are both actors):

```
game_data.h        GameData ──── owns every parsed .sml table (read-only after load)
                     ├─ WrestleStyleDb   95 moves + 14-entry family→state map
                     ├─ WrestlerDb       39 wrestlers: BaseInfo, 9 status rows, anim sprites
                     └─ RuleDb           5 InGameTable rows

sim/actor.h        Actor            x,y,z · facing · layer · team · energy/motion/powerup meters
                     │               ghost bar · region state (+0x6c) · InputFrame input
                     ├─ Wrestler      4-deep state stack · anim clock · move_id/subtype
                     │                pin/held/suffocation flags · button counter
                     └─ Referee        master 0..3 · pin/give-up/tag sub-states · CHUD count

sim/match.h        Match            vector<Actor*> · RuleRow · clock · rng · win check
                                     owns the update order from §1.1
sim/ai.h           AiController     one AiEntity per AI wrestler
                     └─ AiEntity     type 0..12 · timer · target · writes InputFrame
sim/collision.h    RingGeometry     the six floats · leftX(z)/rightX(z)/clampX(z,x)
sim/wrestle_style.h MoveResolver    move id → state sequence + damage, per §3
```

Three rules that keep this faithful and testable:

* **`Actor` has no `virtual draw()`.** Drawing lives in `render/` and reads actor state; the sim never
  knows a renderer exists. (The original does the opposite and it is why it cannot be tested.)
* **`Match::update(const InputFrame& player)`** is the only entry point; it returns a `MatchEvent`
  list (hit, pin, count, bell) that the presentation layer consumes for SFX and HUD. That is also
  what makes the headless event log in Phase 3 possible.
* **`AiEntity` writes into an `InputFrame`, never into the wrestler.** §2.4.

### 2.4 Input contract — the best structural fit in the whole original **[V]**

The original funnels *both* the player and the AI through the **same eight booleans stored on the
actor** (`+0xa2…+0xa9`). `CGame::onTick` fills them from the touchscreen every frame:

```c
if (playerControlEnabled) {
  tyCJoystick::ReadStick1(&x, &y);              // both in [-1, 1]
  for (actor in actors) {
    if (actor->inactive || actor->controlByte != 1 || !IsMainWrestler(actor)) continue;
    if (actor->region < 7 && ((1 << actor->region) & 0x53)) {   // regions 0,1,4,6 only
      if (x >= -0.35f) actor->b_a2 = 1;
      if (x <=  0.35f) actor->b_a3 = 1;
      if (y >= -0.35f) actor->b_a4 = 1;
      if (y <=  0.35f) actor->b_a5 = 1;
      actor->b_a6 = IsButtonDown(0);
      actor->b_a7 = IsButtonDown(1);
      actor->b_a8 = IsButtonPressed(0);         // rising edge, this frame
      actor->b_a9 = IsButtonPressed(1);
    }
  }
}
```

And `AttackState::onTick` — an **AI** state — writes the very same bytes (`pCVar5[0xa5] = 1;
pCVar5[0xa3] = 1;` to walk to the tag corner, for example). The AI does not call wrestler methods; it
presses buttons.

Five details to carry over:

* **deadzone 0.35 per axis**, and the comparisons are `>=` / `<=`, so *inside* the deadzone **both**
  bits of the axis are set. `a2 + a3` together therefore means "no horizontal input", not "two keys
  held". Consume each axis as a signed value (`right − left`, `down − up`), never as two independent
  keypresses;
* which polarity is visually "left" is **not** labelled anywhere in the binary — `a2` is set when
  `x >= −0.35` and `a3` when `x <= 0.35`, which reads backwards unless x grows leftward. Fix the sign
  in the first playable build and record it; do not trust a guess from the decompiler. `TODO(verify)`
* input is only accepted in actor regions **0, 1, 4, 6** (in ring, ringside, outside, in ring alone) —
  the mask is `0x53`;
* only the **main** wrestler of team 0 is human-controlled (`IsMainWrestler`, team `0`);
* `*_edge` (pressed this frame) and level (held) are both exposed, and the mash counter uses the
  *level* bits while move selection uses the *edge* bits.

**Port consequence [D]:** make this the sim's only input surface —

```cpp
struct InputFrame { bool left, right, up, down, btn0, btn1, btn0Edge, btn1Edge; };
```

`main.cpp` produces one per touch event through the existing `ViewportManager::mapTouchToLogical`;
`ai.cpp` produces one per tick per AI actor; `Wrestler::tick(const InputFrame&)` is the single
consumer. That keeps human and AI play on exactly the same code path, which is what makes the
original's AI feel like a player mashing buttons.

### 2.5 Threading and determinism **[D]**

- One simulation step per render frame, **fixed 60 Hz** with an accumulator (`max 4 steps` to avoid
  spiral-of-death), plus `alpha` interpolation for drawing.
- `dt` passed into sim is always `1/60`; all rates from SML are per-second except where the
  original multiplies per-frame (documented per constant in `tuning.h`).
- Randomness goes through a single seeded `xorshift` inside `Match` (`rand() % N` in the original is
  `tyCMath::Rand()`), so replays/tests are deterministic.

---

## 3. The move system (the heart of it)

### 3.1 How a move is stored **[V]**

`WrestleStyleList.sml` holds **95 moves**, one row each. Parsing all 95 and cross-checking
against all 39 wrestlers' `SuperStarWSTable.sml` gives 578 move usages that resolve — so this
table is complete and self-consistent.

Each row becomes a **0x88-byte record** at load (`LoadWrestleStyleTable`, line 14251), laid out:

| Offset | Field | Type | Role |
|---|---|---|---|
| `+0x00` | `MoveName` | `char[128]` | debug/localisation only |
| `+0x80` | `TypeID` | `u16` | **overwritten at load** with the state to enter (see §3.2) |
| `+0x82` | `SubTypeID` | `u16` | index into that family's own table |
| `+0x84` | `EnergyDamage` | `u8` | damage to apply on hit |
| `+0x85` | `MotionPowerDamage` | `u8` | motion-power drained from the victim |
| `+0x86` | `Rare` | `u8` | asserted `< 5`; indexes an SFX probability table |
| `+0x87` | `AttackPosition` | `u8` | target stance / post-move handling (§3.9) |

`StateID` (the first numeric column in the SML) is **not read at runtime** in any path traced.
Empirically it is the *status slot* the move belongs to: for 578 of 584 move usages the move's
`StateID` equals the `StatusID` of the `SuperStarWSTable` row that lists it (the 6 exceptions are
authored `StateID 1` in status 2/3 rows — an authoring quirk, e.g. `WS_Spear`). So treat `StateID`
as an authoring hint to validate the data, not as engine logic.

### 3.2 `_stateMaping`: the one table we must reconstruct **[V + open]**

`LoadWrestleStyleTable` rewrites `TypeID` in place:

```c
uVar4 = *(ushort *)(table + i*0x88 + 0x80);      // TypeID
uVar3 = 1;                                        // default → state 1 IDLE
if (uVar4 < 0xe) uVar3 = *(u32 *)(&_stateMaping + uVar4*4);
*(ushort *)(table + i*0x88 + 0x80) = uVar3;       // now the state to enter
CWrestler::ChangeState(actor, state, 0);          // in UseWrestleStyle
```

So a **14-entry `TypeID → StateTable state` table** is built inside the binary's `.data`.
`_stateMaping` has no initialiser in the decompilation, so its 14 values are the **one piece of
engine data not present in the shipped SML**. Twelve of the fourteen are pinned down by the state
names and by the two special cases the code hard-codes:

| TypeID | Family (verified move names) | n | Entry state | Confidence |
|---:|---|---:|---|---|
| 0 | `WS_CLOTHESLINE`, `SWINGING_`, `FLYING_CLOTHESLINE` | 3 | `51 USE_CL_WS` | high |
| 1 | `SHOULDER_TACKLE`, `HIGH_KNEE`, `BACK_BODY_DROP`, `DROPKICK` | 4 | `57 USE_RAM_WS` **or** `51` | **ambiguous** |
| 2 | 49 throws: `BODY_SLAM`, `PILEDRIVER`, `TOMBSTONE`, `ROCK_BOTTOM`, `STUNNER`, … | 49 | `60 USE_F_WS` | **verified** (code tests `0x3c`) |
| 3 | 11 submissions: `SHARPSHOOTER`, `BOSTON_CRAB`, `HELLS_GATE`, `STFU`, `SLEEPER_HOLD`… | 11 | `64 USE_S_WS` | **verified** (code tests `0x40`) |
| 4 | 8 ground strikes: `ELBOW_DROP`, `LEG_DROP`, `STOMP`, `FIVE_KNUCKLE_SHUFFLE`… | 8 | `66 USE_HT_WS` | high |
| 5 | `SPRINGBOARD_CROSSBODY`, `SPRINGBOARD_MOONSAULT` | 2 | `70 USE_SB_WS` | high |
| 6 | 7 diving moves: `BIONIC_ELBOW`, `AXEHANDLE`, `CROSSBODY`, `DIVING_LEG_DROP`… | 7 | `74 USE_TB_WS` | high |
| 7 | `WS_RESTRAIN` | 1 | `36 RESTRAIN` | high |
| 8 | `WS_LOU_THESZ_PRESS` | 1 | `111 USE_RFWS_RUSH` | medium |
| 9 | `WS_PUNCH`, `WS_RINGSIDE_PUNCH` | 2 | `4 PUNCH` | high |
| 10 | `WS_KICK` | 1 | `5 KICK` | high |
| 11 | `WS_TAG_ELBOW_DROP`, `WS_TAG_LEG_DROP` | 2 | `77 USE_TA_WS` | high |
| 12 | `WS_Spear`, `WS_Trouble_In_Paradise`, `WS_Corkscrew_Scissors_Kick` | 3 | `57 USE_RAM_WS` **or** `51` | **ambiguous** |
| 13 | `WS_USE_WEAPON` | 1 | `100 USE_WEAPON` | high |

**Port decision [D]:** author this as a 14-entry table in `game_data.cpp`
(`kMoveFamilyToEntryState`), mark rows 1 and 12 with a `TODO(verify)`, and settle them in the
first playable build — the difference is only whether those four moves use the ram table or the
clothesline table, and it is visible in one match.

Also verified in `UseWrestleStyle`, and easy to miss:

```c
// choose a "rarity" voice/sfx on every move use
if (Rare < 5 && rand() % 101 <= kRareProbability[Rare]) CSFX::Play(0x3b + Rare);
// family 2 with NeedBegin == 0 skips the wind-up state
if (state == 0x3c && ForceWSTable[SubTypeID].NeedBegin == 0) { UseForceWrestleStyle(a); return; }
// submissions hand the opponent a mirrored move id and pre-align both bodies
if (state == 0x40) { ChangeState(a, 0x40); a = opponent; ChangeState(a, 0x41); }
```

### 3.3 Resolution pipeline **[V]**

```
input: attack button (+ direction)
  1. pick the status slot from the current stance/region      (0..8, see §3.4)
  2. pick one of Attack1..4 from SuperStarWSTable[status]     (-99 = empty slot)
  3. range/facing/depth test                                  StateTable.CollisionRangeX + CollisionRangeZ
  4. roll success                                             Show/Boost percentage, Rare
  5. attacker ChangeState(entry state)                        via _stateMaping
  6. victim  ChangeState(OpponentState / table OnExit state)  often chain=1
  7. apply damage at the impact frame                         EnergyDamage + EDamage, MotionPowerDamage + MDamage
  8. queue exit states and X offsets                          OnExitSelfState/OnExitOpponentState/XOffset
```

Step 8 is why the original feels heavy: a bodyslam ends with the attacker in `STAND_TIRED (43)`
and the victim in `SLAMMED (14)` offset by `OnExitOpponentXOffset`, reversed when
`IsOpponentReverse` is set.

### 3.4 Which moves are available: the 9 status slots **[V]**

`SuperStarWSTable.sml` is 9 rows per wrestler (§4.1 of the record layout below), each holding up to
four move ids plus **per-slot damage and score overrides**:

```
struct SuperStarWSInfo {           // 0x24 bytes, one per StatusID 0..8
  u16 StatusID, ListCount;         // ListCount = number of valid Attack slots
  s16 Attack1..4;                  // move ids into WrestleStyleList, -99 = empty
  s16 EDamage1..4, MDamage1..4;    // per-slot damage override (may be negative)
  s16 Score1..4;                   // points awarded on a successful hit
};
```

Cena, status 0 (`[V]`): `ListCount 2`, attacks `2 (SHOULDER_TACKLE)`, `3 (CLOTHESLINE)`,
`EDamage 6/6`, `MDamage 0/0`, `Score 250/250`. Status 1: four grapple throws
`9 BODY_SLAM / 15 VERTICAL_SUPLEX / 22 FISHERMAN_SUPLEX / 23 ATTITUDE_ADJUSTMENT`,
`EDamage 7/1/8/8`, `MDamage 2/-10/4/4`, `Score 420/…`.

Two consequences for the port:

* **`MDamage` is signed and can be negative** (Cena's Vertical Suplex is `-10`) — it is an
  adjustment, not a cost. Do not clamp it to zero.
* `Score` is the report-screen currency, so faithful scoring comes free once the table is loaded.

Status slot semantics (consistent with the `StateID` cross-check in §3.1): `0` running attack,
`1` grapple throw, `2` counter against a running opponent, `3` ground attack, `4` running power /
boost, `5` hold, `6` top-rope, `7` diving, `8` tag. Slots `9`, `11`, `14` never appear in a
`SuperStarWSTable` — they are the always-available strikes, ringside moves and weapon moves.

### 3.5 Strikes **[V]**

`PUNCH` (`StateTable 4`, anim `3`, 4 frames, `CollisionRangeX 110`) and `KICK` (state `5`,
anim `4`, 4 frames, range `90`) are plain states. Reaction selection is by attacker/victim state
pair and lands on `HITREACT_MID (16)`, `HITREACT_HIGH (17)`, `KNOCKED_BACK (49)` or
`PUSHOFF (31)`. `WS_PUNCH`/`WS_KICK` add 2 energy damage each.

### 3.6 Grapple struggle — corrected model **[V]**

This is the mechanic most likely to be mis-implemented, and the first draft of this document got it
wrong. The decompiled `GrappleIdleState::onTick` (line 23584) shows it is **not** a one-shot input
window — it is a **repeating beat while the attacker holds `GRAPPLE_HIGH (7)`**:

```c
// case 7 of the sub-state switch, executed every tick
timer += dt;
if (timer < beat) return;              // beat stored at aiState+8
timer -= beat;
if (collisionRing(actor, 300.0f, facing)) { flag = 0xa8; goto done; }   // near ropes → forced
if (rand() % 101 > 49)                    { flag = 0xa8; goto done; }   // 51/101 ≈ 50.5 %
if (gameData[0] == 2 /* Royal Rumble */ &&
    rand() % 101 < 90)                    { flag = 0xa8; goto done; }   // rumble: ~94.6 % total
flag = 0xa9;
```

`beat` is the state's tick interval (`0.30 s` for grapple idle, lowered by the strength-scaled
reaction constants in §8) and `flag 0xa8/0xa9` records who won the exchange, which the exit code
then turns into `KNOCKED_BACK (49)` / attempted throw / `PUSHOFF (31)`.

Note the two surprises, both real:

1. **Being near the ropes forces a win** for the actor facing them (`collisionRing(…,300,…)`),
   i.e. grapple struggles near the ropes resolve deterministically.
2. **In a Royal Rumble the win branch is ~94.6 %**, because a second roll (`< 90`) also wins.
   That is exactly the arcade feel of rumble brawls resolving fast.

The same function also gives the authoritative **sub-state → AI-state routing** (§5.1).

### 3.7 Running attacks and the Irish whip **[V]**

* `WalkFactorX 315`, `RunFactorX 600` (px/s), `WalkFactorZ 270`; `HandleAutoWalk` clamps a step to
  **20 px/frame**, so at 30 fps the cap is 600 px/s and the table values are the limiter.
* `IRISH_WHIP (25)` → `IRISH_WHIPPED (26)`; the whipped wrestler bounces off the rope and comes
  back. States that set `CheckRopeHit = 1` (verified): **`27 AIRBORNE`, `31 PUSHOFF`,
  `49 KNOCKED_BACK`, `50 SB_CROSSEDBODY`, `56 BE_USED_CL_DROP`, `62 BE_USED_F_WS`,
  `70 USE_SB_WS`, `80 FA_SLAMMED_FLY`** — these are the only states that interact with the ropes.
* Reach of running counters depends on the opposing style, not a constant: `_getRunningWrestleStyle`
  and `_getAntiRunningWrestleStyle` return **670 / 450 / 400 / 500 / 630 / 440 / 340 / 380 px**
  depending on the opponent. Copy the switch verbatim; do not flatten it to one radius.
* `BounceOffRopeTimming = 150` (from `BaseInfo.sml`) gates how early a rope bounce may start.

### 3.8 Pins and kick-outs **[V]**

`GetButtonCountGoal` (line 13359) + `ButtonCountTable.sml` fully determine the mash:

```
goal = table[stateRow][energyBand] + (pinned ? pin_attempts * AddCounterPerNoEnergyPin : 0)
```

* `stateRow` is found by `StateTableID`: `86 BE_PINNED`, `15 ONGROUND`, `37 RESTRAINED`, …
* energy bands run `>95, 95–90, 90–80, 80–70, 70–60, 60–50, 50–40, 40–30, 30–20, 20–10, 10–5, <5`
  (`Energy95Up … Energy5Down`).
* Verified rows — `BE_PINNED`: `2 3 4 5 8 9 10 11 12 14 15 15`;
  `ONGROUND`: `2 2 2 4 4 4 8 8 8 8 100 100` (a downed wrestler at under 10 energy cannot get up).
* `AddCounterPerNoEnergyPin = 5` → every failed pin adds 5 presses to the next attempt.
* Kick-out heals `HealEnergyByKickOutOpponent = 15`; a free pin heals `HealEnergyByFreePin = 30`.
* `TickButtonCounter` only counts while `!play_dead && button_counter_active && (button0||button1)`;
  `IsReachGoal` then ends the state.
* Referee count: `CHUDNumber::Restart` + `GetNumber`, limit `MaxRefereeCount = 3` (`InGameTable`).

### 3.9 Submissions, holds and `AttackPosition` **[V]**

* Submission family (`TypeID 3`) enters `USE_S_WS (64)`, and `EnterUseSubmissionWrestleStyle`
  (line 15310) mirrors the move id and subtype onto the opponent before aligning them with
  `AdjustOpponentXZ`, then drives `TickUseSubmissionWrestleStyle` toward a give-up check.
* Energy damage is tiny (`1–2`) — submissions win on **suffocation**, not damage:
  `GetSuffocationRate = time / MaxSuffocationTime` with `MaxSuffocationTime = 1.0`.
* `AttackPosition` (checked against all 14 nonzero rows; `0` is the default for the other 81):
  `0` = standing target;
  `1` = target is down, and on exit the attacker **lifts them back up** — `UseWrestleStyleForOnGround`
  checks `AttackPosition == 1` and switches the attacker to `PICKUP (29)` / victim `PICKUPED (30)`;
  `3` = target is down and **stays down** (all stomps and the ground submissions:
  `SHARPSHOOTER`, `BOSTON_CRAB`, `STFU`, `FORCE_STOMP`); `2` = springboard moonsault only
  (aerial entry). Mark `2` as `TODO(verify)`.
* Breaking a hold applies `DamageBeBrokenSleepHold 3` / `DamageBeBrokenRestrain 3` (energy, motion)
  pairs from `CommonDamageInfo.sml`.

### 3.10 Dogfight **[V]**

Two wrestlers running into each other both enter `DOGFIGHT_STANDS (44)`; the pair then routes to
`DOGFIGHT_HIGHER/LOWER (45/46)` or `DOGFIGHT_PUNCH/PUNCHED (47/48)` by
`DogfightPercentage 75`, and the loser goes to `KNOCKED_BACK (49)`. All five dogfight states
(44–48) map to AI state 2 in the routing table.

### 3.11 Damage pairs **[V]**

`CommonDamageInfo.sml` stores every entry as an `F32A2` — **(energy, motion power)** — e.g.
`DamageRunRam 2 / 0`, `DamageBeRunRammed 5 / 0`. Model damage as a two-component struct from day
one; collapsing it to a single float will drift from the original as soon as motion power matters.

## 4. Match rules and flow

### 4.1 Rules are data **[V]**

`InGameTable.sml` has 5 rule sets:

| ID | Rule | Match time | Outside time | Ref count | Toss-out % | Pin | Submission | Referee |
|---|---|---|---|---|---|---|---|---|
| 0 | `RULE_1VS1` | 360 s | 30 s | 3 | 100 | yes | yes | yes |
| 1 | `RULE_2VS2` | 720 s | 30 s | 3 | 100 | yes | yes | yes |
| 2 | `RULE_ROYAL_RUMBLE` | ∞ | 30 s | 3 | **30** | no | no | no |
| 3 | `RULE_BASIC_NON_ELIMINATION` | ∞ | 30 s | 3 | 100 | yes | yes | yes |
| 4 | `RULE_BASIC_ELIMINATION` | 360 s | 30 s | 3 | 100 | yes | yes | yes |

`MaxOutsideCount = 20` is the count-out limit (referee reaches 20 → eliminated).

### 4.2 Referee state machine **[V]**

The referee is a full `CActor` with its own two-level state machine — and it is **not** the 0..6
enumeration a first read suggests. Verified from `CRef::StateTick` (6801),
`CRef::StateOnEnterRef` (6263) and `CRef::MasterStateOnEnter` (7132):

**Master state** (`+0x6c`) — only four:

| Value | Meaning | Verified behaviour |
|---:|---|---|
| 0 | IDLE | resets sub-state, exits to watch |
| 1 | WATCH | every tick: re-scan the match for something to do (below) |
| 2 | MOVE | walk to the computed standing spot, then continue into the sub-state |
| 3 | OUTSIDE | count an outside wrestler; `ClampActorInRingAlone` keeps the ref in the ring |

**Sub-states** (the 4-deep stack), pushed from WATCH:

| Value | Trigger (all verified in the WATCH scan) | Behaviour |
|---:|---|---|
| 3 | a **main** wrestler with master `0` has `is_pinned (0xa0)` | go to MOVE, then 3-count via `CHUDNumber` |
| 4 | `IsNeedAskForGiveUp(w)` true | go to MOVE, then the give-up check |
| 5 | the pinned wrestler's **partner** reached the tag-break condition | go to MOVE, then break the pin |
| 2 | (entry) move to position | see the positioning rules below |

Positioning when the ref arrives (`StateOnEnterRef` case 2), which is worth copying exactly because it
is what makes the count look hand-placed rather than random:

```c
z = target->z;
x = target->x - target->facing * 280.0f;            // 0x438c0000
if (x > GetRingRightX(z)) x = target->opponent->x + 280.0f;
if (x < GetRingLeftX(z))  x = target->opponent->x + 280.0f;
// entering from sub-state 4: z = target->z + 150 (clamped to RingBottomZ)
// entering from sub-state 5: z = target->z - 150 (clamped to RingTopZ)
```

The tag-break condition (sub-state 5) is arcade logic that is easy to miss. The ref keeps a per-team
accumulator (`ref + team*4 + 0x3fc`); on first use it is seeded from the partner's `+0xcf48`, then
each tick it grows by **5.0**, and when `partner->+0xcf48` falls at or below it the ref goes to break
the pin. `+0xcf48` is also the clock source `PowerUp` snapshots into `+0xcf5c`, so the practical
reading is *"a partner who has been powered up long enough earns the save"* — implement it as a
time-based rule and calibrate the constant in game (the exact unit is the one loose end in this
function).

Outside count (master 3): uses the `CHUDNumber` slot at `gameData + 0x16c` against the limit at
`gameData + 0x174` (loaded from `InGameTable.MaxOutsideCount = 20`). When the count is reached the
slot is reset to `-1` and the ref distinguishes two cases: if **every** main wrestler is outside, it
fires the "all outside" listener events instead of eliminating anyone.

The audio assets corroborate the limit: `audio/sfx/` contains
`SVR11_Ref_CntOut_V2_One.wav` … `_Nineteen.wav` and **no** `_Twenty.wav`, so the spoken count runs
1–19 and the 20th beat is the elimination.

### 4.3 Match flow **[V]**

```
Title → Mode select → Superstar select → Arena select → CGameEntrance (105 ENTRANCE)
      → CGameIntro (bell, "FIGHT") → CGame (sim)
      → win condition → CGameOverCutsceneController::Start (loser → LOSE 103, winner → VICTORY 104)
      → CGameOutro → ReportScene (scores from SuperStarWSTable.Score)
```

`CGameOverCutsceneController::Start` **[V]**:
1. clears hints, disables all buttons except pause,
2. `CSFX::Play(0x31)` (crowd pop),
3. every live actor `ChangeState(1, 0)` (idle),
4. picks the loser by team, `ChangeState(0x67)` = **103 `LOSE`**,
5. winner gets the camera-push + `VICTORY (104)`.

**Win conditions [D implemented from data]:** pinfall (3-count), submission (tap/goal met),
count-out (outside > 20), Royal Rumble toss-out (`TossOutPercentage`, `TossOutDynamicBy`),
time limit → decision by remaining energy (the original has a judge; we mirror it with the
`Score` values plus energy, and expose it as a rule flag).

### 4.4 Royal Rumble **[V]**

`CRoyalRumbleController` is a separate controller with:
`Create`, `Tick`, `_LoadBenchWarmer`, `_AppearOnTheStage`, `DisappearOnTheStage`, `FallOut`, `KickOutActor`.
Entering wrestlers come from the bench in order; elimination happens by being tossed over the top
rope. The table gives `TossOutPercentage = 30` and `TossOutDynamicBy = 1`; the first names the base
chance, the second is not traced to a use site, so implement the toss as 30 % baseline and treat
`TossOutDynamicBy` as a `TODO(verify)` modifier hook rather than inventing a formula.

Implement the mode as a `MatchMode` subclass rather than sprinkling `if (mode == 2)` through the sim —
the original does exactly that with a separate `CRoyalRumbleController`.

---

## 5. AI

### 5.1 The 12 AI states **[V]**

`tyCAIState::getType()` values:

| Type | Class | Enter → | Exit conditions |
|---|---|---|---|
| 0 | `SearchEnemyState` | picks a target (`_searchEnemy`), then routes by the wrestler's region state | 0 → `Walk(1)`, 1 → `RingSide(9)`, 2/3/5/7/8/9/10 → `None(12)`, 4 → `OutSide(10)`, 6 → `InRingAlone(11)` |
| 1 | `WalkState` | walks to the target, decides engage | 4 → `Attack`, 0 → `Search` |
| 2 | `DogfightState` | strength window, then win/lose | → `Attack`/`Search` |
| 3 | `GrappleIdleState` | repeats the strength-beat roll while in `GRAPPLE_HIGH (7)` (§3.6, §5.2) | → `Attack(4)`, `BeHeld(7)`, `BeAttacked(5)`, `Dogfight(2)` |
| 4 | `AttackState` | chooses strike/grapple/run by state + energy, 50/50 whips | → `BeAttacked(5)` on the victim |
| 5 | `BeAttackedState` | holds `0.30 − 0.023(s−1)` s, then reacts | → `None` |
| 6 | `OnGroundAttackState` | ground attacks on a downed foe | → `None` |
| 7 | `BeHeldState` | 0.1 s, then escape | → `None`/`Attack` |
| 8 | `BePinState` | 0.12–0.145 s beat, then kick out | → `None` |
| 9 | `RingSideState` | apron behaviour, climb-in | → `Walk`/`Attack` |
| 10 | `OutSideState` | outside fighting, count awareness | → `Walk`/`RingSide` |
| 11 | `InRingAloneState` | taunt/wait | → `Walk` |
| 12 | `NoneState` | no-op until the wrestler state changes | — |

Every AI state reads the **wrestler state** each tick and re-routes; the AI never runs an animation
itself. `GrappleIdleState::onTick` is small enough to transcribe completely and shows exactly how
blunt the original AI is — one switch on the AI's own sub-state, one on the target's sub-state:

### 5.2 The routing switch, verbatim **[V]**

```c
// GrappleIdleState::onTick(this)  — 'a' is the AI's actor pointer
if (a->held)        return setState(BeHeld   /*7*/);
if (a->pinned)      return setState(BePin    /*8*/);
switch (a->substate) {
  case 1: case 2: case 3:                       // IDLE / WALK / RUN
      return setState(SearchEnemy /*0*/);
  case 7:                                        // GRAPPLE_HIGH → run the beat roll (§3.6)
      return;                                    //   no AI change
  case 9: case 25: case 60: case 61: case 64:    // HEADLOCK, IRISH_WHIP, USE_F_WS(_PIN), USE_S_WS
      return setState(Attack      /*4*/);
  case 10: case 15: case 26: case 62: case 63:
  case 65: case 41:                              // HEADLOCKED, ONGROUND, WHIPPED, BE_USED_*, BROKEN
      return setState(BeAttacked  /*5*/);
  case 44: case 45: case 46: case 47: case 48:   // all five DOGFIGHT_* states
      return setState(Dogfight    /*2*/);
}
```

Three things worth internalising, because they shape the port's `sim/ai.cpp`:

* the AI is **stateless** — it re-derives everything from the actor each tick, so a port needs no
  AI-side save state beyond a timer and a target id;
* it reacts to **being on the ground (`15`) by entering `BeAttacked`**, not by entering a
  get-up state: getting up is the wrestler state machine's job (`STANDUP 32`), not the AI's;
* the dogfight states collapse into one AI state, so the AI never chooses a dogfight — the
  collision layer starts it.

### 5.3 Decision constants **[V]**

| Decision | Formula | Source |
|---|---|---|
| Win a grapple beat | `rand()%101 > 49` → 51/101 ≈ 50.5 % | `GrappleIdleState::onTick` case 7 |
| Win a grapple beat in a Rumble | second roll `rand()%101 < 90` also wins → ≈ 94.6 % overall | same, `gameData[0]==2` |
| Grapple beat forced | `collisionRing(actor, 300, facing)` true → win | same |
| Irish whip vs strike | `rand()%11 < 5` → one branch, else the other | `AttackState` case 1 |
| Ground-attack AI bonus | `SetupAIBonus()` | `CWrestler` table |
| Target validity | alive, not same team, `active`, not `held`, region-compatible | `_searchEnemy` |
| "Needs help" (tag) | partner energy ≥ +10 of own | `CRef::StateTick` case 1 |

Add a difficulty knob **[D]** only as a multiplier on *reaction delay*, never on damage or windows —
that keeps fidelity for normal play while allowing an easier mode.

---

## 6. Audio and announcer

The existing `AudioPlayer` is single-voice and WAV-only. The sim needs three things from it:
concurrent SFX (impact + crowd + announcer), one music stream (the **11 AAC tracks** in
`audio/Music/`), and correct pause/resume on app lifecycle.

**[D] Plan:** keep `AudioPlayer` as the WAV decoder and add two layers around it:

- `AudioMixer` — N preloaded voices with a priority/steal policy (a slam must not be cut by a
  footstep);
- `MusicPlayer` — streaming decode, or pre-converted WAV via the existing `scripts/convert-audio.ps1`.

The asset naming already encodes the two lookup tables, so no filenames need to be hard-coded:

| Pattern | Count | Used for |
|---|---:|---|
| `RA_JR_SSN_<WRESTLER>_<n>.wav` | 76 | ring announcer name calls (2 takes per wrestler) |
| `SVR11_Ref_CntOut_V2_<Word>.wav` | 19 | referee outside count, `One` … `Nineteen` |
| `50_G_sys_5001_OneRingBellMatch.wav` | 1 | bell (match start / end) |
| `9007_Small_Crowd`, `9010_Short_Crowd`, `9011_Large_Crowd_F01/F02` | 4 | crowd beds/ reactions |

Note the **19**: there is deliberately no `Twenty.wav` — the count is spoken 1…19 and the 20th is the
`MaxOutsideCount` elimination, which corroborates §4.2. `data/Data/CommonSound/SFXTable.sml` (64 rows)
and each wrestler's `SFXTable.sml` give the volume/length metadata; the `FileName` fields are empty,
so the IDs are resolved by convention in code.

## 7. Rendering requirements the simulation imposes

The sim only needs the *renderer* to provide:

1. `SST` decoding → texture (23,696 `.sst` entries, one per animation frame, stored inside the
   53 zip bundles in `asset_bundles/Sprites/`).
2. Palette-swapped draws (`SpritePalette.fsh`) for power-up tint and team colours.
3. Sprite layering (`BottomLayerPrority`/`UpperLayerPrority` from the anim table) so a grapple
   draws the attacker's body under/over the defender correctly.
4. `DrawOrder` by draw layer (`CGameObjectManager::GetDrawLayer`) — the original draws the ring,
   then actors by layer (front ropes over wrestlers).
5. HUD: energy bars with the 10/s ghost decay, motion power, power-up meter, ref count digits
   (`ui/Sprites/UI/count/TIME_*.png`), mash prompt.

Those five are the contract. Everything else (menus, entrances, crowd) can come later.

---

## 8. Tuning constants extracted from the binary

Put these in `data/tuning.h` with the comment showing provenance. **Do not scatter them.**

| Constant | Value | Hex in binary | Meaning |
|---|---|---|---|
| `kGrappleBeat` | 0.30 s | `0x3e99999a` | `GrappleIdleState` beat — the roll repeats this often |
| `kGrappleBeatForcedRange` | 300 px | `0x43960000` | `collisionRing(actor, 300, facing)` forces a win |
| `kReactionBeat` | 0.30 s | `0x3e99999a` | `BeAttackedState` base |
| `kReactionBeatPerStrength` | −0.023 s | `0xbcbc6a7f` | × `(strength − 1)`, i.e. `0.30 − 0.023·(s−1)` |
| `kDogfightBeat` | 0.18 s | `0x3e3851ec` | `DogfightState` base |
| `kDogfightBeatPerStrength` | −0.0126 s | `0xbc4e703b` | × `(strength − 1)` |
| `kHeldBeat` | 0.10 s | `0x3dcccccd` | `BeHeldState` |
| `kPinEscapeBase` | 0.12 s | `0x3df5c28f` | `BePinState` |
| `kPinEscapeRand` | 7.63e-7 s | `0x354cce67` | × `(rand() & 0x7fff)` → 0.12–0.145 s |
| `kEnergyBarDrainRate` | −10 /s | `0xc1200000` | ghost bar **drains only** (see §1.4) |
| `kMaxWalkStep` | 20 px/frame | `0x41a00000` | `HandleAutoWalk` clamp |
| `kGrappleWinRoll` | `rand()%101 > 49` | `umod 0x65`, `cmp 0x31` | ≈ 50.5 % |
| `kGrappleWinRollRumble` | `rand()%101 < 90` | `umod 0x65`, `cmp 0x5a` | second chance, ≈ 94.6 % overall |
| `kDogfightWin` | 75 % | `CommonVariableInfo` | dogfight |
| `kStateStackDepth` | 4 | assert — stack at `+100` | sub-state stack |
| `kNoState` | 0xff | — | empty sub-state |

The three beat constants are **negative offsets applied to `strength − 1`**, where `strength` is the
byte at `actor + 0xcf45`. At strength 1 the beats are 0.30 s / 0.30 s / 0.18 s; at strength 11 they
are 0.30 s / 0.07 s / 0.054 s — strong wrestlers react roughly four times faster.

Constants that live in **SML** (do not hard-code): everything in §3–§5 plus
`CollisionRangeZ 40`, `TagAreaWidth/Height 80/80`, `SlammedHeight 100`, `TurnbuckleHeight 180`,
`OutsideBackToInRingWidth 300`, `KickOutLoseTime 0.75`, `KickOutJumpTime 0.5`,
`KickOutJumpFactorX 400`, `KickOutJumpHeight 180`, `KickOutDisappearTime 1.0`,
`AirborneFactorX 550`, `UseToseOutAreaWidth 50`, `CollisionFeetFactorX 80`, `CollisionHeadFactorX 60`,
`MaxSuffocationTime 1.0`, `CrowdVoicePercenage [30,50,100,100,100]`,
`HealEnergyByFreePin 30`, `HealEnergyByKickOutOpponent 15`, `AddCounterPerNoEnergyPin 5`.

---

## 9. Implementation phases

Each phase ends with something runnable and tested. Phases 1–3 need **no GPU**.

### Phase 1 — Data layer (1–2 weeks)

Files: `data/sml_parser.*`, `data/game_data.*`, `data/sml_parser_test.cpp`.

- Tokenizer/parser for the SML grammar: `<TYPE|name> value`, `<ARRAY|name> N { … } <ARRAYEND|name>`,
  quoted strings with `''` escapes, `TYPEAn` arrays, tabs *and* spaces, CRLF.
- Typed accessors: `u8/u16/u32/s16/f32/str/vec2/vecN`, with bounds checks and error logs.
- Typed tables from §3.1 + `WrestlerAT`, `BaseInfo`, `InGameTable`, `Common*`, `ButtonCount`.
- Host test parsing **all 273 SML files** and asserting row counts (117 states, 269 anims, 95 moves,
  5 rules, 39 wrestlers…) and a few spot values (PUNCH range 110, BE_PINNED goals).

**Acceptance:** `./sml_parser_test` passes on the full asset tree with zero missing fields.

### Phase 2 — Headless simulation (2–3 weeks)

Files: `sim/actor.*`, `sim/wrestler.*`, `sim/collision.*`, `sim/wrestle_style.*`, `sim/sim_test.cpp`.

- State stack, animation clock, energy/motion/powerup meters.
- 1-D collision + trapezoid clamp + region states.
- Strike/grapple/whip/pin resolution with the constants in §8.
- Deterministic RNG.

**Acceptance (golden tests):**
1. Two actors at 100 px, punch → defender enters `HITREACT`, energy −2.
2. Grapple beat, no input → wins in 51/101 of 10,000 seeded rolls (±1 %); in Rumble mode ≈ 94.6 %.
3. `BeAttacked` beat at strength 11 = 0.070 s ± 0.001; at strength 1 = 0.300 s.
3b. Actor inside 300 px of a rope facing it always wins the grapple beat.
4. Pin at energy 60 → goal 9; after one failed pin → 14.
5. Irish whip into ropes → `ROPEFLING` → rebound state within 1.2 s.

### Phase 3 — Rules, referee, AI (2 weeks)

Files: `sim/match.*`, `sim/referee.*`, `sim/ai.*`.

- Rule sets, timer, outside count, win conditions, elimination.
- Referee master states 0–3 (idle, watch, move, outside) plus the pin / give-up / tag sub-states.
- 12 AI states with the transitions in §5.1.

**Acceptance:** two AI wrestlers fight a full 1v1 to a decision in headless mode, with a readable
event log (`[12.4s] P1 punch → P2 HITREACT_MID hp 62`), no asserts, and a deterministic replay hash.

### Phase 4 — Renderer + SST (3–4 weeks, the biggest unknown)

Files: `render/*`, plus `game/scene.*`.

- SST decoder. The container magic is `00 74 73 73` and the header is plain little-endian: the crowd
  sheets parse as 80×80 with a byte-1 version flag, so this is a bounded format, not a compression
  research project. Verify by round-tripping one sprite to a PNG offline before writing any GL.
- Sprite batch with the existing `Sprite`/`SpritePalette`/`FontTT` shaders (run `upgrade_glsl.py` first).
- Arena layers, ring ropes, crowd, then HUD.

**Acceptance:** one wrestler's idle/walk/punch visible in the ring at 60 fps on device.

### Phase 5 — Screens & flow (2 weeks)

Title → select → entrance → match → outro → report; pause; options; save/profile.

### Phase 6 — Modes & polish (ongoing)

Tag, Royal Rumble, Gauntlet, Road to WrestleMania, shop; announcer; crowd reactions; replay/ghosts.

---

## 10. Fidelity checklist

Use this as the review gate for every PR against the sim:

- [ ] Every state change goes through `ChangeState` with the correct `chain` flag (stack depth 4).
- [ ] `OpponentState` from `StateTable.sml` is honoured on entry.
- [ ] `CollisionRangeX` comes from the table, never a hard-coded radius; negative = no collision.
- [ ] Depth gate uses `CollisionRangeZ`, not a guessed value.
- [ ] Reaction beats use `strength` and the exact negative coefficients (§8), and repeat.
- [ ] Grapple beat resolves with `rand()%101 > 49`, the Rumble second roll, and the 300 px rope force.
- [ ] Damage is stored as an (energy, motion) pair; negative `MDamage` is preserved.
- [ ] The HUD ghost bar drains at 10/s and never eases upward.
- [ ] `TypeID` is translated through `kMoveFamilyToEntryState`, never used raw as a state id.
- [ ] Mash goals come from `ButtonCountTable` + `AddCounterPerNoEnergyPin`.
- [ ] Damage comes from `WrestleStyleList`/`SuperStarWSTable`/`CommonDamageInfo`, never literals.
- [ ] Animation frame selection uses the table's `FrameCount` and the global fps.
- [ ] `Facing`/`OpposingFacing`/`xOffset` applied on state entry (paired moves stay aligned).
- [ ] Match rules read from `InGameTable` by rule id, not `if (mode == …)`.
- [ ] Randomness routed through the seeded RNG; no `rand()` in `sim/`.
- [ ] `sim/` compiles without `<android/*>` includes.

---

## 11. Open items and risks

1. **SST format** — the only genuinely unknown format. It is the gating item for Phase 4.
   Everything else can proceed in parallel, and the sim can be finished and validated before it.
2. **Ring geometry** — six floats per arena are not in the shipped SML. Calibrate once per arena from
   a screenshot (the ring is a trapezoid, so 4 corners + 2 z values is enough) and store in
   `data/Data/ArenaGeometry.sml` (our file).
3. **Impact timing inside a strike** — the state tick bodies are jump tables and are not recoverable,
   so the exact frame at which a plain punch/kick applies damage must be calibrated visually. Signature
   moves are fine: they use `HurtTimming` from `WrestlerAT.sml` (only 18 anims define it, all
   finishers/throws). Suggested approach: land damage on the frame where the attacking sprite's hand
   crosses the target's head — roughly frame 2 of 4 for the punch — then lock it in a golden test.
   (Regeneration units are **not** ambiguous: `AutoReplyEnergy` computes `dt * rate`, so
   `HealEPS/HealMPPS/HealPUPS` are per-second.)
4. **Strength source** — the 1..11 rating at `+0xcf45` is loaded from the wrestler tables; map it from
   `BaseInfo.sml`/`wrestlerData.sml` once the field is identified (it is the only ambiguous field in
   the reverse-window formula).
5. **Five `TODO(verify)` values**, all small, each settled by one play-test:
   `_stateMaping`'s 14 entries (2 of them ambiguous, §3.2), `AttackPosition == 2` (§3.9),
   `TossOutDynamicBy` (§4.4), the unit of the tag-break accumulator (§4.2), and the stick polarity
   (§2.4). Each is itemised at its section with the candidate value to start from.
6. **Logical resolution** — data is authored for 960×640 and the repo defaults to 480×320.
   Construct `ViewportManager(960, 640)` rather than rescaling the tables (see §1.2); the HUD art in
   `ui/` was authored for the same 960×640 space, which confirms it.
7. **Undocumented states** — `StateTable` IDs 3, 5, 7, 8, 9 as *wrestler region states* are unused by
   any function we traced. Keep them reserved; do not invent behaviour.
8. **Frame rate** — the original's rate lives in `PTR_fps`, a float set at runtime and not visible in
   the decompilation; the animation code divides by it, so it is a plain frame rate. Assume 30 fps
   (the sprite counts and the 20 px step cap both fit), and express per-frame constants as per-second
   rates in the port: `20 px/frame @30 fps` = **600 px/s**, applied as `600 * dt`, which stays correct
   at your fixed 60 Hz step. Do not copy the literal 20 into a 60 Hz loop.
9. **Save/profile** — the original uses `SaveFileTool_CPP` + AES; we do not need to read its saves,
   but a compatible profile format is future work.

---

## 12. What I would build first (concrete next step)

1. `data/sml_parser.{h,cpp}` + `sml_parser_test.cpp` (≈450 lines) — proves the data layer against all
   273 files and immediately de-risks Phases 2–3.
2. `data/game_data.{h,cpp}` with the five tables needed by combat (§3.1) (≈350 lines).
3. `sim/actor.*`, `sim/collision.*` and the first golden test (punch range + damage) (≈300 lines).

That is roughly one week of work and gives a headless, testable combat core that matches the original's
numbers — the right foundation before a single sprite is drawn.

---

## Appendix A — Useful entry points in `WrestleFest Premium.c`

| Symbol | Line | Why |
|---|---:|---|
| `CGame::onCreate` | 8563 | match bootstrap, mode dispatch, AI entity creation |
| `CGame::onTick` / `CGame::onDraw` | 9279 / 9832 | frame order |
| `CWrestler::ChangeState` | 15085 | the sub-state stack |
| `CWrestler::NextState` | 16066 | stack pop |
| `CWrestler::StateTickPlayer` | 16125 | state dispatch |
| `CWrestler::AdjustOpponentXZ` | 15185 | paired-animation alignment |
| `CWrestler::Collision` | 14856 | X-interval + Z gate |
| `CWrestler::LoadTableData` | 14555 | exact table list and roots |
| `CActor::SetEnergyDamage` / `AddEnergy` | 19295 / 19338 | damage & heal |
| `CRef::StateTick` | 6801 | referee / counts / pins |
| `GetButtonCountGoal` | 13359 | mash formula |
| `IsNeedCountdown` / `IsNeedAskForGiveUp` | 7061 / 7080 | ref triggers |
| `AttackState::onTick` | 23804 | strike routing table |
| `GrappleIdleState::onTick` | 23584 | reverse roll |
| `BeAttackedState::onEnter` | 24253 | reaction beat = 0.30 − 0.023·(s−1) |
| `DogfightState::onEnter` | 23444 | dogfight beat = 0.18 − 0.0126·(s−1) |
| `GrappleIdleState::onEnter` | 23549 | fixed 0.30 beat |
| `BeHeldState::onEnter` | 25038 | 0.10 s |
| `BePinState::onEnter` | 25145 | 0.12 + rand·7.6e-7 s |
| `LoadWrestleStyleTable` | 14251 | the `TypeID → state` remap (§3.2) |
| `UseWrestleStyle` | 16895 | move dispatch + the `0x3c`/`0x40` special cases |
| `UseWrestleStyleForOnGround` | 19087 | `AttackPosition == 1` → `PICKUP`/`PICKUPED` |
| `EnterUseSubmissionWrestleStyle` | 15310 | submission entry, opponent mirroring |
| `CWrestler::Tick` | 18393 | ghost-bar drain |
| `CWrestler::PowerUp` / `TickPowerUp` | 18715 / 18069 | power-up state |
| `CActor::ClampActorToRing` … `ClampActor` | 7958–8145 | the four region clamps |
| `HandleAutoWalk` | 16152 | 20 px/frame clamp |
| `BounceRope` | 16947 | rope bounce |
| `DogfightState::onTick` | 23485 | clash resolution |
| `SearchEnemyState::_searchEnemy` | 22829 | target selection |
| `WalkState::onTick` | 22916 | approach/engage logic |
| `CGameOverCutsceneController::Start` | 11194 | victory/lose |
| `CRoyalRumbleController::Create/Tick/FallOut/KickOutActor` | 10767 / 11460 / 13035 / 13230 | rumble mode |
| `CWrestler::GetWrestleStyleList` | 18596 | per-wrestler 9×0x24 status records |

## Appendix B — Data files the sim will read

`data/Data/`: `StateTable.sml`, `InGameTable.sml`, `CommonVariableInfo.sml`, `CommonDamageInfo.sml`,
`ButtonCountTable.sml`, `AirborneTable.sml`, `FallAwayTable.sml`, `SuperStarWSTable.sml`,
`AnimationTable/WrestlerAT.sml`, `AnimationTable/RefereeAT.sml`, `AnimationTable/AnnouncerAT.sml`,
`AnimationTable/ItemAT.sml`, `WSTable/*.sml` (11 files), `Contents/**/AllSuperStarTable*.sml`,
`Contents/**/AllArenaTable*.sml`, `Wrestler/<Name>/{BaseInfo,EntranceInfo,SFXTable,SuperStarWSTable,wrestlerData}.sml`,
`Localization/LanguageTable*.sml`.

All already present in the repository and reachable through the existing `AssetLoader`.

---

*This document is a design, not code. It is deliberately specific about numbers and state IDs so that
implementation can proceed without re-reading the decompiler for every detail. Every constant quoted
here was re-checked against the shipped SML files or the decompiled C while writing it; the handful
that could not be checked are marked `TODO(verify)` and listed in §11 rather than guessed.*
