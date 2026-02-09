#!/usr/bin/env bash
set -euo pipefail

# Reuse existing Linux binaries, but recompile main game binary only.
# Usage:
#   ./build/linux-reuse.sh
#   RUN=1 ./build/linux-reuse.sh
#   SYNC_ANDROID=1 ./build/linux-reuse.sh

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

PLATFORMER_BIN="${PLATFORMER_BIN:-$ROOT_DIR/platformer}"
SHEET_BIN="${SHEET_BIN:-$ROOT_DIR/sheet_config}"
RUN="${RUN:-0}"
SYNC_ANDROID="${SYNC_ANDROID:-0}"
FAST="${FAST:-0}"

if [ "$FAST" = "1" ]; then
  CXXFLAGS="-std=c++17 -O1 -fno-plt"
  echo "[INFO] FAST build enabled"
else
  CXXFLAGS="-std=c++17 -O3 -DNDEBUG -flto -fno-plt"
fi

resolve_pkg() {
  local resolved=""
  for p in "$@"; do
    if pkg-config --exists "$p"; then
      resolved="$p"
      break
    fi
  done
  if [ -n "$resolved" ]; then
    echo "$resolved"
  fi
  return 0
}

PKG_SDL3="$(resolve_pkg sdl3)"
PKG_IMAGE="$(resolve_pkg sdl3_image sdl3-image)"
PKG_TTF="$(resolve_pkg sdl3_ttf sdl3-ttf)"
if [ -n "$PKG_SDL3" ] && [ -n "$PKG_IMAGE" ] && [ -n "$PKG_TTF" ]; then
  SDL_CFLAGS="$(pkg-config --cflags "$PKG_SDL3" "$PKG_IMAGE" "$PKG_TTF" 2>/dev/null || true)"
  SDL_LIBS="$(pkg-config --libs "$PKG_SDL3" "$PKG_IMAGE" "$PKG_TTF" 2>/dev/null || true)"
else
  SDL_CFLAGS=""
  SDL_LIBS=""
fi
if [ -z "$SDL_CFLAGS" ] || [ -z "$SDL_LIBS" ]; then
  echo "[ERROR] Missing pkg-config entries for sdl3/sdl3_image/sdl3_ttf"
  exit 1
fi
# Compat include shim for distros that ship uppercase SDL3 include dirs.
mkdir -p "$ROOT_DIR/.build/include/sdl3"
if [ -d /usr/include/SDL3 ]; then
  for h in /usr/include/SDL3/*.h; do
    [ -f "$h" ] || continue
    ln -sf "$h" "$ROOT_DIR/.build/include/sdl3/$(basename "$h")"
  done
fi
if [ -d /usr/include/SDL3_image ]; then
  for h in /usr/include/SDL3_image/*.h; do
    [ -f "$h" ] || continue
    ln -sf "$h" "$ROOT_DIR/.build/include/sdl3/$(basename "$h")"
  done
fi
if [ -d /usr/include/SDL3_ttf ]; then
  for h in /usr/include/SDL3_ttf/*.h; do
    [ -f "$h" ] || continue
    ln -sf "$h" "$ROOT_DIR/.build/include/sdl3/$(basename "$h")"
  done
fi
SDL_CFLAGS="$SDL_CFLAGS -I$ROOT_DIR/.build/include"
MIXER_LIBS=""
PKG_MIXER="$(resolve_pkg sdl3_mixer sdl3-mixer)"
if [ -n "$PKG_MIXER" ]; then
  MIXER_LIBS="$(pkg-config --libs "$PKG_MIXER")"
fi
JSON_CFLAGS=""
if [ -f "third_party/nlohmann/json.hpp" ]; then
  JSON_CFLAGS="-Ithird_party"
fi

echo "[INFO] Recompiling main binary only: $PLATFORMER_BIN"
g++ $CXXFLAGS \
  $SDL_CFLAGS \
  $JSON_CFLAGS \
  src/main.cpp \
  src/TileMap.cpp \
  src/AssetPath.cpp \
  src/LevelLoader.cpp \
  src/TextRenderer.cpp \
  src/LevelSelect.cpp \
  src/PlayerController.cpp \
  src/LevelManager.cpp \
  src/GameSupport.cpp \
  src/CrashReporter.cpp \
  src/FrontendMenu.cpp \
  $SDL_LIBS \
  $MIXER_LIBS \
  -o "$PLATFORMER_BIN"

chmod +x "$PLATFORMER_BIN" || true

echo "[OK] Main binary rebuilt. Reusing existing extras:"
echo "     platformer   -> $PLATFORMER_BIN"
if [ -f "$SHEET_BIN" ]; then
  echo "     sheet_config -> $SHEET_BIN (reused)"
else
  echo "     sheet_config -> missing (not rebuilt by this script)"
fi

if [ "$SYNC_ANDROID" = "1" ]; then
  if [ -x "./build/update-android-app.sh" ]; then
    echo "[INFO] Syncing Android app assets/libs from existing outputs..."
    ./build/update-android-app.sh
  else
    echo "[ERROR] Missing executable script: ./build/update-android-app.sh"
    exit 1
  fi
fi

if [ "$RUN" = "1" ]; then
  echo "[INFO] Launching platformer..."
  exec "$PLATFORMER_BIN"
fi

echo "[DONE] Reuse flow complete."
