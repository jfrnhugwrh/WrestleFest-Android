#!/bin/sh
# Syntax-check every C++ translation unit that normally needs the NDK.
#
#   tools/host_check/check.sh
#
# The GLES3 / android/log / zlib headers in this directory only declare the
# symbols the code uses, so this catches typos, bad signatures and missing
# includes on a machine without the Android toolchain. Nothing is linked.
set -e
cd "$(dirname "$0")/../.."
SRC=android_project/app/src/main/cpp
FLAGS="-std=c++17 -Wall -Wextra -Wno-unused-parameter -fsyntax-only -I$SRC -Itools/host_check"
status=0
for f in \
    "$SRC"/render/image.cpp \
    "$SRC"/render/gl_sprite.cpp \
    "$SRC"/render/zip_reader.cpp \
    "$SRC"/obb_archive.cpp \
    "$SRC"/asset_loader.cpp \
    "$SRC"/game/game_renderer.cpp \
    "$SRC"/game/menu_screens.cpp \
    "$SRC"/game/road.cpp \
    "$SRC"/game/app.cpp \
    "$SRC"/main.cpp
do
    printf '%s\n' "check $f"
    if ! g++ $FLAGS "$f"; then
        status=1
    fi
done
exit $status
