# Port status

How the code in `android_project/app/src/main/cpp/` maps onto the phases in
`GAMEPLAY_IMPLEMENTATION.md`, what is verified, and what is still open.

Run the host tests with:

```
tools/host_check/tests.sh     # data_test, sim_test, image_test
tools/host_check/check.sh     # syntax-check every NDK-only translation unit
tools/preview/preview <assets> <out.png>   # software composite of the in-ring view
tools/preview/preview <assets> <out.png> --screen=main   # front-end screens
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

The clinch resolves through the beat roll in `Match::updateGrappleBeats` for
`GRAPPLE_IDLE` as well as the high/low stances: a button press only brings the
next beat forward (`kGrappleMashStep`), it no longer wins the exchange. The
original's 300 px rope force is measured in its own (wider) ring space; at the
960x640 logical scale it would cover the whole trapezoid and make every clinch
an automatic win, so it is clamped to 90 px (`tuning.h` documents this).

An empty energy bar is a loss (`Match::checkExhausted`): without it a wrestler
drained to zero can sit in `STAND_TIRED` forever - a Rumble cannot pin him, so
the match would never end. This was the reason six-man Rumbles used to run
until the test cap without a winner; they now play out in ~3-4 minutes.

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

## Phase 3 - Rules, referee, AI (done for 1v1, tag and rumble)

`sim/match.*` owns the frame order of the original (actors, then AI, then
referee/counts, then the win check), the opponent targeting, the pin gate and
cooldown, the outside count, the rule timer/decision, and the Royal Rumble
toss-out (the loser of a clinch at the ropes) + last-man-standing win
condition. The Rumble also staggers its
entrants: `enableStaggeredEntry(interval, initial)` parks everyone past the
first pair in the aisle (off camera, not targetable, not ticked) and walks them
in one at a time; the win check stays closed until the aisle is empty.

2 VS 2 tag rules (`enableTagTeams`): the second man of each team waits on the
apron (frozen, untargetable), and the active wrestler swaps him in with the blue
button next to the corner; the simulation resolves tags before the actors tick,
so the press is a tag and not a strike. The AI tags out on its own once its
energy drops under 35 % and the apron man has more left. The team is out when
both of its men are down, which is the existing team win check. `sim/referee.*` runs the four master
states and the pin/give-up/tag sub-states. `sim/ai.*` implements the twelve AI
states with the reaction beats from `tuning.h`.

Verified by `sim_test`: entrance staging, the rumble rule and its toss-out,
pins, the full match, the power-up meter, exhaustion, the tag rules and a
six-man Rumble played to a winner.

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

## Phase 5 - Screens and flow (front end rebuilt against the reference screens)

`game/app.*` runs Loading -> Title -> Main menu -> Game modes -> Select
superstar -> Select arena -> Entrance/announcer/bell -> Match -> Result, which
is the original's screen order (`state_main`, `MainScene`, `ModeScene`,
`SuperstarScene`, `ArenaScene` in `TitleMenu*.cpp`).

The front end is a separate, GL-only translation unit (`game/menu_screens.*`)
over the geometry in `game/menu_layout.h`, and every screen is drawn from the
shipped art at its native size, positioned with the numbers the original's scene
code uses:

* **Loading** - `ui/Sprites/UI/background_2.png` *is* the loading card (the
  menu backdrop with the tilted WrestleFest logo already ghosted into it), plus
  `Loading ...` in the bottom right in the light grey the capture uses. An
  earlier pass drew a second logo over it and dimmed the plate; both are gone.
* **Title** - `title_menu/title.png` and the blinking `taptostart_en.png`.
* **Main menu** - `MAIN MENU` at the original's print origin (glyph-cell top
  y = 18), the four `main_button_*.png` plates (776x49 at x = 91) stepping the
  original's 83 px from y = 167, the WWE mark beside SHOP (which is the one row
  printed left aligned, at x = 440, to leave room for the mark), the back button
  and the selected row in orange (`CColor::SelectedString`).
* **Game modes** - the six `mode_button_*.png` plates at the original's own y
  values (161, 216, 273, 329, 385, 441). The plate is picked by caption length:
  875 px for ROAD TO WRESTLEMANIA and the Saturday Night's Main Event
  tournament, 558 px for the rest, each centred on the canvas. Exhibition starts
  1 VS 1, Royal Rumble the six-man rumble and Tag Team the 2 VS 2 match; the
  other three report that they are not in this build.
* **Select superstar** - 4x2 `superstar_frame.png` cards (155 px, 92.5/202 and
  140/216 spacing) with the wrestler's `avatar.png` fitted into the frame's
  109x109 window, names under the card in two lines, page arrows and dots. The
  picked card swaps to the orange `superstar_select.png` frame.
* **Select arena** - 2x2 `arena_frame.png` cards (236x169) with the arena's own
  `scene.png` (212x146) fitted into the frame's 190x125 window, names under the
  card, page arrows and dots; the picked card uses `arena_select.png`.

The card art used to be drawn at its native size, which spilled the avatars and
the arena scenes over the frames' borders; the windows above are measured from
the shipped PNGs and `menu::fitInWindow` centres the art inside them, so a card
can never cover its own frame. Both are covered by `menu_layout_test`.

The select screen's first page also fixes the roster order: the reference shows
Jake Roberts over Randy Orton, John Cena over Randy Savage, Rey Mysterio over
Stone Cold and The Rock over The Undertaker, i.e. a legend paired with a modern
superstar in each of the four columns (`kRosterFolders`).

Captions are set in Century Gothic, the second tyF face that ships with the
game (`graphics/Fonts/Century Gothic.tyF`), in the plain atlas - the reference
captions have no outline and no extra weight, so the faux-bold ring an earlier
pass stamped around every glyph is off (`menu::kBoldPx` is 0) - with the
original's drop shadow and colours (`LightRed` for MAIN MENU / GAME MODES,
`Orange` for the select screens, white for rows, orange for the selected row).
Sizes are fitted to the widths of the captions on the reference screenshots.
`BitmapFont` measures the cap height from the atlas, so a row of text is centred
inside its plate instead of being positioned from the top of its glyph cell.

Everything else from the first pass is unchanged: the post-match report
(`ui/Sprites/UI/report/frame.png`), the virtual pad, the pause menu that freezes
the sim, and the event-driven audio cues through `AudioPlayer::playAsset`. The
options screen toggles that mixer (`AudioPlayer::setMuted`).

Still open: the post-match report's `Score` values from `SuperStarWSTable`,
entrances walking in from the stage, music streaming, and save/profile.

## Phase 6 - Modes and polish (open)

Road to WrestleMania, Saturday Night's Main Event tournament, Gauntlet, the
shop, multiplayer, crowd reactions, replays.

## Known gaps / next steps

1. Device check of the renderer (this sandbox has no GPU): confirm the arena
   placement, the palette shader and the HUD on real hardware.
2. Entrance walk-in: `playEntrance()` currently plays the `ENTRANCE` animation
   standing in the ring; the Rumble aisle walk-in is in place, so the same path
   can be reused for the entrance walk; `EntranceInfo.anims` and `WalkFactor`
   are loaded and ready to drive it.
3. Tag corners: the tag works from the apron spot, but there is no corner-post
   art or a climb-in animation yet (`CLIMB*` states exist in the table).
4. Music: `audio/Music` is untouched; `AudioPlayer` handles WAV only.
5. Report screen and profile/save data.
