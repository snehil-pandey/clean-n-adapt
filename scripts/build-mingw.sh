#!/usr/bin/env bash
# Builds cna.exe, cna-widget.exe and the installer into ./dist using MinGW-w64 + NSIS.
# Works on Linux (cross-compile) and inside MSYS2/MinGW on Windows. No CMake needed.
#
#   Linux:  sudo apt install g++-mingw-w64-x86-64-posix nsis        (then: scripts/build-mingw.sh)
#   MSYS2:  pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-nsis
#
# Overrides: CXX=... WINDRES=... MAKENSIS=...   SKIP_INSTALLER=1
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

VER="$(tr -d '[:space:]' < VERSION)"
IFS=. read -r V1 V2 V3 <<<"$VER"
CXX="${CXX:-x86_64-w64-mingw32-g++}"
WINDRES="${WINDRES:-x86_64-w64-mingw32-windres}"
MAKENSIS="${MAKENSIS:-makensis}"
OUT="$ROOT/build-mingw"
DIST="$ROOT/dist"
mkdir -p "$OUT" "$DIST"

echo "== Clean-n-Adapt $VER =="

# version info shared by both resource files
printf '#define CNA_VER_STR "%s"\n#define CNA_VER_COMMA %s,%s,%s,0\n' "$VER" "$V1" "${V2:-0}" "${V3:-0}" > "$OUT/version_info.h"

echo "-- resources"
"$WINDRES" -I "$OUT" -I "$ROOT/assets" -O coff "$ROOT/assets/cna.rc"    -o "$OUT/cna.res.o"
"$WINDRES" -I "$OUT" -I "$ROOT/assets" -O coff "$ROOT/assets/widget.rc" -o "$OUT/widget.res.o"

echo "-- cna.exe"
"$CXX" -std=c++17 -Os -Wall -Wextra -static -s -DCNA_VERSION="\"$VER\"" \
  -o "$OUT/cna.exe" "$ROOT"/src/*.cpp "$OUT/cna.res.o" -lpsapi -ladvapi32 -lshell32

echo "-- cna-widget.exe"
"$CXX" -std=c++17 -Os -Wall -Wextra -static -s -municode -mwindows \
  -o "$OUT/cna-widget.exe" "$ROOT/widget/widget.cpp" "$OUT/widget.res.o" \
  -lgdiplus -ldwmapi -lshell32 -luser32 -lgdi32 -ladvapi32 -lole32 -luuid

cp "$OUT/cna.exe" "$OUT/cna-widget.exe" "$DIST/"

if [ "${SKIP_INSTALLER:-0}" = "1" ]; then
  echo "-- installer skipped (SKIP_INSTALLER=1)"
elif command -v "$MAKENSIS" >/dev/null 2>&1; then
  echo "-- installer"
  "$MAKENSIS" -V2 "-DVERSION=$VER" "-DCNA_EXE=$OUT/cna.exe" "-DWIDGET_EXE=$OUT/cna-widget.exe" \
    "-DROOT=$ROOT" "-DOUT_DIR=$DIST" "$ROOT/installer/cna.nsi"
else
  echo "!! makensis not found - installer NOT built (install NSIS, or set MAKENSIS=/path/to/makensis)" >&2
  exit 2
fi

echo "== done =="
ls -la "$DIST"
