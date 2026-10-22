# Software preview

`preview.cpp` composites the in-ring layout with a software blitter, using the
same numbers as `game/game_renderer.cpp` (art scale, arena origin, sprite
anchoring, depth order) plus the HUD and virtual pad. It exists because the host
machine has no GPU: it is the only way to eyeball the placement without a device.

It also draws the front-end screens, from the same shared geometry the game uses
(`game/menu_layout.h`) and the same tyF font metrics, so a menu change can be
checked against the reference screenshots without an APK:

```
g++ -std=c++17 -O1 -Iandroid_project/app/src/main/cpp tools/preview/preview.cpp \
    android_project/app/src/main/cpp/data/{asset_source,game_data,sml_parser}.cpp \
    android_project/app/src/main/cpp/sim/collision.cpp \
    android_project/app/src/main/cpp/render/{image,zip_reader}.cpp \
    -Itools/host_check -o /tmp/preview -lz
```

`-lz` needs the zlib development package; on a machine that only has the
runtime library, link `/lib/*/libz.so.1` (or `/usr/lib/*/libz.so.1`) directly.

## In-ring view

```
/tmp/preview android_project/app/src/main/assets /tmp/preview.png "00_John Cena" "03_BigShow"
```

Output is a 960x640 PNG of the SmackDown arena with both wrestlers idling, the
HUD bars, the timer and the virtual pad.

Any other venue can be composited with `--arena=<FolderName>` (the folders come
from `AllArenaTable.sml`, e.g. `ARENA03` Raw, `ARENA04` WrestleMania):

```
/tmp/preview android_project/app/src/main/assets /tmp/arena07.png \
    "00_John Cena" "03_BigShow" --arena=ARENA07
```

Every `arena0.png` is 1482x678-679, so the one calibration in `game_renderer.cpp`
holds for all eleven arena folders.

## Entrance cutscene

```
/tmp/preview <assets> /tmp/entrance.png --entrance=0 "00_John Cena" "03_BigShow"
/tmp/preview <assets> /tmp/entrance.png --entrance=1 --arena=ARENA03 "00_John Cena"
```

`--entrance=<id>` composites one entrance stage, where the id is the
`AllEntranceSceneTable` record (0 Basic, 1 Modern Raw, 2 Modern SmackDown): the
arena's crowd sheet, the stage art and its attach objects, the crowd in front of
it, and the men walking out on it with their own entrance animations. Pass four
wrestler folders to see a tag-team entrance.

`--entrance-time=<steps>` runs the entrance animation for more or fewer 60 Hz
steps before the frame is captured (90 by default).

## Front-end screens

```
/tmp/preview <assets> /tmp/main.png   --screen=main    [--selected=N]
/tmp/preview <assets> /tmp/modes.png  --screen=modes   [--selected=N]
/tmp/preview <assets> /tmp/select.png --screen=select  [--page=N] [--selected=SLOT]
/tmp/preview <assets> /tmp/arena.png  --screen=arena   [--page=N] [--selected=SLOT]
/tmp/preview <assets> /tmp/loading.png --screen=loading
/tmp/preview <assets> /tmp/title.png   --screen=title
/tmp/preview <assets> /tmp/road.png    --screen=road   [--selected=SLOT]
```

`--selected` is the highlighted row (main/modes) or card slot (selectors);
`--page` is the zero-based page of the selector grids. The output is 960x640,
the game's logical canvas. On the Road to WrestleMania ladder `--selected` is
the card, and `--road-level=N` / `--road-stage=N` place the run's cursor.

## Comparing with a screenshot of the original

The captures of the original that the screens were matched against are not
uniformly scaled: the back button (`back.png`, a square 78x78) comes out 68x57
in them, so they are wider horizontally than vertically. Squash the preview by
the capture's aspect ratio before comparing (`preview.png` resized from 960x640
to the capture's size) rather than reading positions off a naive stretch.

Even then the aspect is slightly different from one capture to the next, so the
numbers in `game/menu_layout.h` are anchored on the parts of a screen that do
not depend on it - the heights of the strips (which have no top/bottom in the
art to stretch) and the horizontal widths of the captions - and on the
original's own `tyCFontTT::Print` coordinates from `TitleMenu.cpp`.
