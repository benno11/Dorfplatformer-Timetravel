#!/usr/bin/env bash
set -euo pipefail

# Force rebuild SDL stack for Android staging root.
# Usage:
#   ./build/recompile-sdl.sh
#   ABI=arm64-v8a API=24 SDL_REF=release-3.4.x ./build/recompile-sdl.sh
#   SDL_REQUIRED_VERSION=3.4.0 ./build/recompile-sdl.sh
#   CLEAN_SDL_SOURCES=1 ./build/recompile-sdl.sh
#   REBUILD_GAME=1 SYNC_APP=1 ./build/recompile-sdl.sh
#   RECOMPILE_PC=1 ./build/recompile-sdl.sh

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

ABI="${ABI:-arm64-v8a}"
API="${API:-24}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
SDL_REF="${SDL_REF:-release-3.4.x}"
SDL_REQUIRED_VERSION="${SDL_REQUIRED_VERSION:-3.4.0}"
FORCE_REBUILD_SDL=1
CLEAN_SDL_SOURCES="${CLEAN_SDL_SOURCES:-0}"
REBUILD_GAME="${REBUILD_GAME:-0}"
SYNC_APP="${SYNC_APP:-0}"
RECOMPILE_PC="${RECOMPILE_PC:-0}"

echo "[INFO] Recompiling SDL stack"
echo "       ABI=$ABI API=$API JOBS=$JOBS"
echo "       SDL_REF=$SDL_REF"
echo "       SDL_REQUIRED_VERSION=$SDL_REQUIRED_VERSION"
echo "       CLEAN_SDL_SOURCES=$CLEAN_SDL_SOURCES"
echo "       REBUILD_GAME=$REBUILD_GAME SYNC_APP=$SYNC_APP"
echo "       RECOMPILE_PC=$RECOMPILE_PC"

export ABI API JOBS SDL_REF SDL_REQUIRED_VERSION FORCE_REBUILD_SDL

# Clear stale per-ABI build/install trees before reconfigure.
for d in \
  "$ROOT_DIR/deps/android-build/$ABI/SDL" \
  "$ROOT_DIR/deps/android-build/$ABI/SDL_image" \
  "$ROOT_DIR/deps/android-build/$ABI/SDL_ttf" \
  "$ROOT_DIR/deps/android-build/$ABI/SDL_mixer" \
  "$ROOT_DIR/deps/android-install/$ABI/SDL" \
  "$ROOT_DIR/deps/android-install/$ABI/SDL_image" \
  "$ROOT_DIR/deps/android-install/$ABI/SDL_ttf" \
  "$ROOT_DIR/deps/android-install/$ABI/SDL_mixer"; do
  rm -rf "$d"
done

if [ "$CLEAN_SDL_SOURCES" = "1" ]; then
  echo "[INFO] CLEAN_SDL_SOURCES=1 -> removing cached SDL source repos"
  rm -rf \
    "$ROOT_DIR/deps/android-src/SDL" \
    "$ROOT_DIR/deps/android-src/SDL_image" \
    "$ROOT_DIR/deps/android-src/SDL_ttf" \
    "$ROOT_DIR/deps/android-src/SDL_mixer"
fi

./build/setup-android-sdl.sh

if [ -f "$ROOT_DIR/build/android.env" ]; then
  # shellcheck disable=SC1091
  . "$ROOT_DIR/build/android.env"
fi

echo "[INFO] SDL staging root: ${SDL3_ANDROID_ROOT:-$ROOT_DIR/deps/android}"
STAGE_ROOT="${SDL3_ANDROID_ROOT:-$ROOT_DIR/deps/android}"
require_any_file() {
  local label="$1"
  shift
  for p in "$@"; do
    if [ -f "$p" ]; then
      return 0
    fi
  done
  echo "[ERROR] Missing rebuilt artifact ($label). Checked:"
  for p in "$@"; do
    echo "        $p"
  done
  exit 1
}

require_any_file "SDL core header" \
  "$STAGE_ROOT/include/SDL3/SDL.h"
require_any_file "SDL_image header" \
  "$STAGE_ROOT/include/SDL3/SDL_image.h" \
  "$STAGE_ROOT/include/SDL3_image/SDL_image.h"
require_any_file "SDL_ttf header" \
  "$STAGE_ROOT/include/SDL3/SDL_ttf.h" \
  "$STAGE_ROOT/include/SDL3_ttf/SDL_ttf.h"
require_any_file "SDL_mixer header" \
  "$STAGE_ROOT/include/SDL3/SDL_mixer.h" \
  "$STAGE_ROOT/include/SDL3_mixer/SDL_mixer.h"

for f in \
  "$STAGE_ROOT/lib/$ABI/libSDL3.so" \
  "$STAGE_ROOT/lib/$ABI/libSDL3_image.so" \
  "$STAGE_ROOT/lib/$ABI/libSDL3_ttf.so" \
  "$STAGE_ROOT/lib/$ABI/libSDL3_mixer.so"; do
  if [ ! -f "$f" ]; then
    echo "[ERROR] Missing rebuilt artifact: $f"
    exit 1
  fi
done

if [ "$REBUILD_GAME" = "1" ]; then
  echo "[INFO] Rebuilding native game library against rebuilt SDL"
  ./build/android.sh
fi

if [ "$SYNC_APP" = "1" ]; then
  echo "[INFO] Syncing native libraries into Android app"
  ./build/update-android-app.sh
fi

if [ "$RECOMPILE_PC" = "1" ]; then
  echo "[INFO] Recompiling SDL for PC/Linux"
  SDL3_REQUIRED_VERSION="$SDL_REQUIRED_VERSION" \
  SDL3_SOURCE_REF="$SDL_REF" \
  AUTO_BUILD_SDL3_FROM_SOURCE=1 \
  ./build/dep-linux.sh

  if [ ! -f "$ROOT_DIR/deps/linux-sdl3-src/build/sdl3.pc" ] && [ ! -f "$ROOT_DIR/deps/linux-sdl2-src/build/sdl3.pc" ]; then
    echo "[ERROR] PC SDL rebuild did not produce sdl3.pc in deps/linux-sdl3-src/build or deps/linux-sdl2-src/build"
    exit 1
  fi
fi

echo "[OK] SDL recompile complete."
