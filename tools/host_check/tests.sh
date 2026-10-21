#!/bin/sh
# Build and run every host test with plain g++, the same flags CMake uses when
# VIEWPORT_BUILD_TESTS is on. Useful when cmake/NDK are not installed.
#
#   tools/host_check/tests.sh
set -e
cd "$(dirname "$0")/../.."

SRC=android_project/app/src/main/cpp
ASSETS="$PWD/$SRC/../../main/assets"
OUT=${TMPDIR:-/tmp}/wrestlefest_host_tests
mkdir -p "$OUT"

CXX=${CXX:-g++}
COMMON="-std=c++17 -O1 -Wall -Wextra -I$SRC"
DATA_SRC="$SRC/data/asset_source.cpp $SRC/data/game_data.cpp $SRC/data/sml_parser.cpp"

echo "== data_test"
$CXX $COMMON -DWRESTLEFEST_DATA_ROOT="\"$ASSETS/data/Data\"" \
    $DATA_SRC "$SRC/data/data_test.cpp" -o "$OUT/data_test"
"$OUT/data_test"

echo "== menu_layout_test"
# menu_layout.h pulls in gl_sprite.h, so the GLES stub in tools/host_check is
# what lets this compile on a machine without the NDK.
$CXX $COMMON -Itools/host_check \
    "$SRC"/game/menu_layout_test.cpp -o "$OUT/menu_layout_test"
"$OUT/menu_layout_test"

echo "== sim_test"
$CXX $COMMON -DWRESTLEFEST_DATA_ROOT="\"$ASSETS/data/Data\"" \
    $DATA_SRC "$SRC"/sim/collision.cpp "$SRC"/sim/wrestler.cpp "$SRC"/sim/match.cpp \
    "$SRC"/sim/ai.cpp "$SRC"/sim/referee.cpp "$SRC"/sim/sim_test.cpp -o "$OUT/sim_test"
"$OUT/sim_test"

echo "== image_test"
ZFLAGS=""
if [ ! -f /usr/include/zlib.h ]; then
    ZFLAGS="-Itools/host_check"
fi
# Link against zlib: -lz when the dev package is installed, otherwise the
# runtime library directly (some machines ship libz.so.1 without libz.so).
ZLIB="-lz"
if ! echo 'int main(){return 0;}' | $CXX -x c++ - -o "$OUT/zprobe" -lz 2>/dev/null; then
    ZLIB=$(ls /usr/lib/*/libz.so.1 /lib/*/libz.so.1 2>/dev/null | head -1)
fi
$CXX $COMMON $ZFLAGS -DWRESTLEFEST_ASSET_ROOT="\"$ASSETS\"" \
    "$SRC"/render/image.cpp "$SRC"/render/zip_reader.cpp "$SRC"/render/image_test.cpp \
    $DATA_SRC \
    -o "$OUT/image_test" $ZLIB
"$OUT/image_test"

echo
echo "all host tests passed"
