# Software preview

`preview.cpp` composites the in-ring layout with a software blitter, using the
same numbers as `game/game_renderer.cpp` (art scale, arena origin, sprite
anchoring, depth order) plus the HUD and virtual pad. It exists because the host
machine has no GPU: it is the only way to eyeball the placement without a device.

```
g++ -std=c++17 -O1 -Iandroid_project/app/src/main/cpp tools/preview/preview.cpp \
    android_project/app/src/main/cpp/data/{asset_source,game_data,sml_parser}.cpp \
    android_project/app/src/main/cpp/sim/collision.cpp \
    android_project/app/src/main/cpp/render/{image,zip_reader}.cpp \
    -Itools/host_check -o /tmp/preview -lz
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
