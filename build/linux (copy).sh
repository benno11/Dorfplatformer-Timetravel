#!/usr/bin/env bash
set -euo pipefail
# ---- CONFIG ----
CXX=g++
FAST="${FAST:-0}"
SDL_REQUIRED_VERSION="${SDL_REQUIRED_VERSION:-3.4.0}"
if [ "$FAST" = "1" ]; then
  CXXFLAGS="-std=c++17 -O1 -fno-plt"
  echo "[INFO] FAST build enabled"
else
  CXXFLAGS="-std=c++17 -O3 -DNDEBUG -flto -fno-plt"
fi
SRC_DIR="src"
OUT_PLATFORMER="platformer"
OUT_SHEET="sheet_config"
MIXER_LIBS=""
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# Prefer locally rebuilt SDL3 pkg-config metadata when present, but keep
# image/ttf resolution on normal system pkg-config paths.
LOCAL_PKGCONFIG_PATHS=()
for d in \
  "$ROOT_DIR/deps/linux-sdl3-src/build" \
  "$ROOT_DIR/deps/linux-sdl2-src/build"; do
  if [ -f "$d/sdl3.pc" ]; then
    LOCAL_PKGCONFIG_PATHS+=("$d")
  fi
done
LOCAL_JOINED=""
if [ ${#LOCAL_PKGCONFIG_PATHS[@]} -gt 0 ]; then
  LOCAL_JOINED="$(IFS=:; echo "${LOCAL_PKGCONFIG_PATHS[*]}")"
  echo "[INFO] Using local SDL pkg-config path: $LOCAL_JOINED"
fi
SDL3_PKG_CONFIG_PATH="${PKG_CONFIG_PATH:-}"
if [ -n "$LOCAL_JOINED" ]; then
  SDL3_PKG_CONFIG_PATH="$LOCAL_JOINED${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
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

resolve_sdl3_pkg() {
  local resolved=""
  for p in sdl3 SDL3; do
    if PKG_CONFIG_PATH="$SDL3_PKG_CONFIG_PATH" pkg-config --exists "$p"; then
      resolved="$p"
      break
    fi
  done
  if [ -n "$resolved" ]; then
    echo "$resolved"
  fi
  return 0
}

PKG_SDL3="$(resolve_sdl3_pkg)"
PKG_IMAGE="$(resolve_pkg sdl3_image sdl3-image SDL3_image)"
PKG_TTF="$(resolve_pkg sdl3_ttf sdl3-ttf SDL3_ttf)"
SDL_CFLAGS=""
SDL_LIBS=""

if [ -n "$PKG_SDL3" ]; then
  SDL_CFLAGS+=" $(PKG_CONFIG_PATH="$SDL3_PKG_CONFIG_PATH" pkg-config --cflags "$PKG_SDL3" 2>/dev/null || true)"
  SDL_LIBS+=" $(PKG_CONFIG_PATH="$SDL3_PKG_CONFIG_PATH" pkg-config --libs "$PKG_SDL3" 2>/dev/null || true)"
elif [ -f "/usr/include/SDL3/SDL.h" ]; then
  SDL_CFLAGS+=" -I/usr/include/SDL3"
  SDL_LIBS+=" -lSDL3"
else
  echo "[ERROR] Missing SDL3 development files (pkg-config or /usr/include/SDL3/SDL.h)."
  exit 1
fi

if [ -n "$PKG_IMAGE" ]; then
  SDL_CFLAGS+=" $(pkg-config --cflags "$PKG_IMAGE" 2>/dev/null || true)"
  SDL_LIBS+=" $(pkg-config --libs "$PKG_IMAGE" 2>/dev/null || true)"
elif [ -f "/usr/include/SDL3_image/SDL_image.h" ]; then
  SDL_CFLAGS+=" -I/usr/include/SDL3_image"
  SDL_LIBS+=" -lSDL3_image"
else
  echo "[ERROR] Missing SDL3_image development files (pkg-config or /usr/include/SDL3_image/SDL_image.h)."
  exit 1
fi

if [ -n "$PKG_TTF" ]; then
  SDL_CFLAGS+=" $(pkg-config --cflags "$PKG_TTF" 2>/dev/null || true)"
  SDL_LIBS+=" $(pkg-config --libs "$PKG_TTF" 2>/dev/null || true)"
elif [ -f "/usr/include/SDL3_ttf/SDL_ttf.h" ]; then
  SDL_CFLAGS+=" -I/usr/include/SDL3_ttf"
  SDL_LIBS+=" -lSDL3_ttf"
else
  echo "[ERROR] Missing SDL3_ttf development files (pkg-config or /usr/include/SDL3_ttf/SDL_ttf.h)."
  exit 1
fi
# Compat include shim for distros that ship uppercase SDL3 include dirs.
mkdir -p "$PWD/.build/include/sdl3"
if [ -d /usr/include/SDL3 ]; then
  for h in /usr/include/SDL3/*.h; do
    [ -f "$h" ] || continue
    ln -sf "$h" "$PWD/.build/include/sdl3/$(basename "$h")"
  done
fi
if [ -d /usr/include/SDL3_image ]; then
  for h in /usr/include/SDL3_image/*.h; do
    [ -f "$h" ] || continue
    ln -sf "$h" "$PWD/.build/include/sdl3/$(basename "$h")"
  done
fi
if [ -d /usr/include/SDL3_ttf ]; then
  for h in /usr/include/SDL3_ttf/*.h; do
    [ -f "$h" ] || continue
    ln -sf "$h" "$PWD/.build/include/sdl3/$(basename "$h")"
  done
fi
SDL_CFLAGS="$SDL_CFLAGS -I$PWD/.build/include"
if [ -n "$PKG_SDL3" ] && ! PKG_CONFIG_PATH="$SDL3_PKG_CONFIG_PATH" pkg-config --atleast-version="$SDL_REQUIRED_VERSION" "$PKG_SDL3"; then
  echo "[WARN] sdl3 ${SDL_REQUIRED_VERSION}+ not found. Continuing with installed version: $(PKG_CONFIG_PATH="$SDL3_PKG_CONFIG_PATH" pkg-config --modversion "$PKG_SDL3" 2>/dev/null || echo unknown)"
fi
PKG_MIXER="$(resolve_pkg sdl3_mixer sdl3-mixer)"
if [ -n "$PKG_MIXER" ]; then
  MIXER_LIBS="$(pkg-config --libs "$PKG_MIXER")"
fi
JSON_CFLAGS=""
if [ -f "third_party/nlohmann/json.hpp" ]; then
  JSON_CFLAGS="-Ithird_party"
fi

# ---- BUILD ----
"$CXX" $CXXFLAGS \
  $SDL_CFLAGS \
  $JSON_CFLAGS \
  "$SRC_DIR/main.cpp" \
  "$SRC_DIR/GameApp.cpp" \
  "$SRC_DIR/AndroidEntrypoints.cpp" \
  "$SRC_DIR/TileMap.cpp" \
  "$SRC_DIR/AssetPath.cpp" \
  "$SRC_DIR/LevelLoader.cpp" \
  "$SRC_DIR/TextRenderer.cpp" \
  "$SRC_DIR/LevelSelect.cpp" \
  "$SRC_DIR/PlayerController.cpp" \
  "$SRC_DIR/LevelManager.cpp" \
  "$SRC_DIR/GameSupport.cpp" \
  "$SRC_DIR/CrashReporter.cpp" \
  "$SRC_DIR/FrontendMenu.cpp" \
  "$SRC_DIR/AudioSystem.cpp" \
  "$SRC_DIR/ParallaxRenderer.cpp" \
  "$SRC_DIR/World3PatternBackground.cpp" \
  $SDL_LIBS \
  $MIXER_LIBS \
  -o "$OUT_PLATFORMER"

"$CXX" $CXXFLAGS \
  $SDL_CFLAGS \
  "$SRC_DIR/SheetConfigTool.cpp" \
  $SDL_LIBS \
  -o "$OUT_SHEET"

echo "Build OK -> ./$OUT_PLATFORMER and ./$OUT_SHEET"
