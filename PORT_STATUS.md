# Port status

How the code in `android_project/app/src/main/cpp/` maps onto the phases in
`GAMEPLAY_IMPLEMENTATION.md`, what is verified, and what is still open.

Run the host tests with:

```
tools/host_check/tests.sh     # data_test, sim_test, image_test
tools/host_check/check.sh     # syntax-check every NDK-only translation unit
tools/preview/preview <assets> <out.png>   # software composite of the in-ring view
```

## Phase 1 - Data layer (done)

`data/sml_parser.*` reads the SML tag stream (multiple top-level arrays, `TYPEAn`
vector fields, quoted strings, the `-1s` suffix junk the exporter emits).
`data/game_data.*` binds every table the simulation needs: `StateTable` (117),
`WrestlerAT` (269), `WrestleStyleList` (95), the nine `WSTable` families,
`InGameTable` (5 rules), `CommonVariableInfo`, `CommonDamageInfo`,
`ButtonCountTable`, `AllArenaTable`, and per wrestler `BaseInfo`,
`SuperStarWSTable`, `wrestlerData`, `SFXTable` and `EntranceInfo`.

Verified by `data_test`: row counts, spot values (PUNCH range 110, Cena's
second throw, HealEPS 1.4), the mash goals, and the `.tyF` font descriptor
(Header + 256 CharTable rows).

## Phase 2 - Headless simulation (done)

`sim/actor.h` is `CActor` (x/y/z, facing, the 4-deep state stack, energy,
energy_display ghost bar, motion power, power-up). `sim/wrestler.cpp` is
`CWrestler`: `changeState` with the chain flag, `OpponentState` pairing,
`Facing`/`xOffset` snapping, the input-driven locomotion, strikes, grapples,
dogfights, whips, ground game, pins, submissions, and the animation clock with
looping locomotion states.

`sim/collision.*` holds the trapezoid ring and the `CollisionRangeX` /
`CollisionRangeZ` model. Ranges now come from `StateTable` through
`Wrestler::collisionRange()`; only the documented fallbacks remain literals.

Verified by `sim_test`: reaction beats, the 51/101 grapple roll (and the
~94.6 % Rumble variant), the ghost bar rate, a full AI-vs-AI 1v1 to a decision
(360 s, ~600 hits, 2 pins), a looping animation clock, and seed determinism.

## Phase 3 - Rules, referee, AI (done for 1v1, rumble tested)

`sim/match.*` owns the frame order of the original (actors, then AI, then
referee/counts, then the win check), the opponent targeting, the pin gate and
cooldown, the outside count, the rule timer/decision, and the Royal Rumble
toss-out + last-man-standing win condition. The Rumble also staggers its
entrants: `enableStaggeredEntry(interval, initial)` parks everyone past the
first pair in the aisle (off camera, not targetable, not ticked) and walks them
in one at a time; the win check stays closed until the aisle is empty. `sim/referee.*` runs the four master
states and the pin/give-up/tag sub-states. `sim/ai.*` implements the twelve AI
states with the reaction beats from `tuning.h`.

Verified by `sim_test`: entrance staging, the rumble rule and its toss-out,
pins, and the full match.

## Phase 4 - Renderer (implemented, needs a device for the final check)

`render/image.cpp` decodes PNG and `.sst` (palette at +20, indices at +276,
RGBA4444/RGB565 palettes), `render/zip_reader.cpp` reads the bundle zips (both
whole-buffer and streaming via a range reader), `render/gl_sprite.cpp` is the
GLES3 sprite batch + palette shader + `tyF` bitmap font, `game/game_renderer.cpp`
draws the arena (crowd sheet, ring, front ropes), depth-sorted wrestlers with
shadows and the power-up palette blink, the referee, and the HUD (energy bars
with the ghost drain, names, timer, referee count, mash prompt).

Verified on the host: 101,422 animation frames across 38 bundles all resolve to
sprite files that exist and decode; arena/HUD/font art decodes; the font UVs
sample ink where the glyphs are; the streaming zip path matches the in-memory
one; `tools/preview` composites the ring view (scale 0.72, origin
`480 - 742*0.72, 560 - 430*0.72`) and shows the wrestlers standing correctly on
the mat. What cannot be checked here is the GL upload/draw itself - that needs
the APK on a device.

## Phase 5 - Screens and flow (first pass)

`game/app.*` runs Title -> Select -> Entrance/announcer/bell -> Match ->
Result, with the shipped front-end art, the virtual pad from the control sheet
(d-pad bottom left, blue and red buttons on the right, pause button), a pause
menu (`ui/Sprites/UI/pause/`) that freezes the sim, a rule chip on the select
screen (1 VS 1 / Royal Rumble, six entrants with staggered entry and a HUD
countdown), and event-driven audio cues through `AudioPlayer::playAsset`.

Still open: arena selection and tag-team selection, the post-match report (`Score` values
from `SuperStarWSTable`), entrances walking in from the stage, tag and rumble
flow in the UI, music streaming, and save/profile.

## Phase 6 - Modes and polish (open)

Tag flow, Gauntlet,
Road to WrestleMania, shop, crowd reactions, replays.

## Known gaps / next steps

1. Device check of the renderer (this sandbox has no GPU): confirm the arena
   placement, the palette shader and the HUD on real hardware.
2. Entrance walk-in: `playEntrance()` currently plays the `ENTRANCE` animation
   standing in the ring; the Rumble aisle walk-in is in place, so the same path
   can be reused for the entrance walk; `EntranceInfo.anims` and `WalkFactor`
   are loaded and ready to drive it.
3. Tag-team rules: `Match` is team-aware, but the tag-in/tag-out AI routing is
   not written yet.
4. Music: `audio/Music` is untouched; `AudioPlayer` handles WAV only.
5. Report screen and profile/save data.
