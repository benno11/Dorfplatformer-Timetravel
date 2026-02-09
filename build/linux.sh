#!/usr/bin/env bash
set -euo pipefail

# Linux build helper for platformer + sheet_config.
# Modes:
#   ./build/linux.sh           -> build
#   ./build/linux.sh build     -> build
#   ./build/linux.sh check     -> dependency check only
#   ./build/linux.sh run       -> build then run platformer
#   ./build/linux.sh clean     -> remove built binaries

MODE="${1:-build}"
CXX="${CXX:-g++}"
FAST="${FAST:-0}"
SDL_REQUIRED_VERSION="${SDL_REQUIRED_VERSION:-3.4.0}"
BUILD_SHEET="${BUILD_SHEET:-0}"

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$ROOT_DIR/src"
OUT_PLATFORMER="$ROOT_DIR/platformer"
OUT_SHEET="$ROOT_DIR/sheet_config"
BUILD_INCLUDE_DIR="$ROOT_DIR/.build/include/sdl3"

if [ "$FAST" = "1" ]; then
  CXXFLAGS=("-std=c++17" "-O1" "-fno-plt" "-DSDL_ENABLE_OLD_NAMES=1")
  echo "[INFO] FAST build enabled"
else
  CXXFLAGS=("-std=c++17" "-O3" "-DNDEBUG" "-flto" "-fno-plt" "-DSDL_ENABLE_OLD_NAMES=1")
fi

find_header_dir() {
  local rel="$1"
  local base
  for base in /usr/include /usr/local/include; do
    if [ -f "$base/$rel" ]; then
      dirname "$base/$rel"
      return 0
    fi
  done
  return 1
}

append_split_flags() {
  local value="$1"
  local -n out_ref="$2"
  local token
  for token in $value; do
    out_ref+=("$token")
  done
}

# Use rebuilt local sdl3.pc only for SDL3 core.
LOCAL_SDL3_PC_PATH=""
for d in "$ROOT_DIR/deps/linux-sdl3-src/build" "$ROOT_DIR/deps/linux-sdl2-src/build"; do
  if [ -f "$d/sdl3.pc" ]; then
    LOCAL_SDL3_PC_PATH="$d"
    break
  fi
done
if [ -z "$LOCAL_SDL3_PC_PATH" ]; then
  echo "[ERROR] Missing rebuilt sdl3.pc (expected in deps/linux-sdl3-src/build or deps/linux-sdl2-src/build)."
  exit 1
fi
SDL3_PKG_CONFIG_PATH="$LOCAL_SDL3_PC_PATH"
echo "[INFO] Using rebuilt sdl3.pc from: $LOCAL_SDL3_PC_PATH"

PKG_SDL3=""
for p in sdl3 SDL3; do
  if PKG_CONFIG_PATH="$SDL3_PKG_CONFIG_PATH" pkg-config --exists "$p"; then
    PKG_SDL3="$p"
    break
  fi
done
PKG_IMAGE=""
for p in sdl3_image sdl3-image SDL3_image; do
  if pkg-config --exists "$p"; then
    PKG_IMAGE="$p"
    break
  fi
done
PKG_TTF=""
for p in sdl3_ttf sdl3-ttf SDL3_ttf; do
  if pkg-config --exists "$p"; then
    PKG_TTF="$p"
    break
  fi
done
PKG_MIXER=""
for p in sdl3_mixer sdl3-mixer; do
  if pkg-config --exists "$p"; then
    PKG_MIXER="$p"
    break
  fi
done

SDL_CFLAGS_ARR=()
SDL_LIBS_ARR=()
MIXER_LIBS_ARR=()
INCLUDE_DIRS_FOR_SHIM=()
# Always scan common SDL include roots for lowercase shim generation.
for d in /usr/include/SDL3 /usr/include/SDL3_image /usr/include/SDL3_ttf /usr/include/SDL3_mixer /usr/local/include/SDL3 /usr/local/include/SDL3_image /usr/local/include/SDL3_ttf /usr/local/include/SDL3_mixer; do
  if [ -d "$d" ]; then
    INCLUDE_DIRS_FOR_SHIM+=("$d")
  fi
done

# SDL3 core
if [ -n "$PKG_SDL3" ]; then
  append_split_flags "$(PKG_CONFIG_PATH="$SDL3_PKG_CONFIG_PATH" pkg-config --cflags "$PKG_SDL3" 2>/dev/null || true)" SDL_CFLAGS_ARR
  append_split_flags "$(PKG_CONFIG_PATH="$SDL3_PKG_CONFIG_PATH" pkg-config --libs "$PKG_SDL3" 2>/dev/null || true)" SDL_LIBS_ARR
  if [ -f /usr/include/SDL3/SDL.h ]; then
    INCLUDE_DIRS_FOR_SHIM+=(/usr/include/SDL3)
  fi
  if [ -f /usr/local/include/SDL3/SDL.h ]; then
    INCLUDE_DIRS_FOR_SHIM+=(/usr/local/include/SDL3)
  fi
elif SDL3_HDR_DIR="$(find_header_dir SDL3/SDL.h)"; then
  SDL_CFLAGS_ARR+=("-I$SDL3_HDR_DIR")
  SDL_LIBS_ARR+=("-lSDL3")
  INCLUDE_DIRS_FOR_SHIM+=("$SDL3_HDR_DIR")
else
  echo "[ERROR] Missing SDL3 development files (pkg-config or SDL3/SDL.h)."
  exit 1
fi

# SDL3_image
if [ -n "$PKG_IMAGE" ]; then
  append_split_flags "$(pkg-config --cflags "$PKG_IMAGE" 2>/dev/null || true)" SDL_CFLAGS_ARR
  append_split_flags "$(pkg-config --libs "$PKG_IMAGE" 2>/dev/null || true)" SDL_LIBS_ARR
  if [ -f /usr/include/SDL3_image/SDL_image.h ]; then
    INCLUDE_DIRS_FOR_SHIM+=(/usr/include/SDL3_image)
  fi
  if [ -f /usr/local/include/SDL3_image/SDL_image.h ]; then
    INCLUDE_DIRS_FOR_SHIM+=(/usr/local/include/SDL3_image)
  fi
elif SDL3_IMAGE_HDR_DIR="$(find_header_dir SDL3_image/SDL_image.h)"; then
  SDL_CFLAGS_ARR+=("-I$SDL3_IMAGE_HDR_DIR")
  SDL_LIBS_ARR+=("-lSDL3_image")
  INCLUDE_DIRS_FOR_SHIM+=("$SDL3_IMAGE_HDR_DIR")
else
  echo "[ERROR] Missing SDL3_image development files (pkg-config or SDL3_image/SDL_image.h)."
  exit 1
fi

# SDL3_ttf
if [ -n "$PKG_TTF" ]; then
  append_split_flags "$(pkg-config --cflags "$PKG_TTF" 2>/dev/null || true)" SDL_CFLAGS_ARR
  append_split_flags "$(pkg-config --libs "$PKG_TTF" 2>/dev/null || true)" SDL_LIBS_ARR
  if [ -f /usr/include/SDL3_ttf/SDL_ttf.h ]; then
    INCLUDE_DIRS_FOR_SHIM+=(/usr/include/SDL3_ttf)
  fi
  if [ -f /usr/local/include/SDL3_ttf/SDL_ttf.h ]; then
    INCLUDE_DIRS_FOR_SHIM+=(/usr/local/include/SDL3_ttf)
  fi
elif SDL3_TTF_HDR_DIR="$(find_header_dir SDL3_ttf/SDL_ttf.h)"; then
  SDL_CFLAGS_ARR+=("-I$SDL3_TTF_HDR_DIR")
  SDL_LIBS_ARR+=("-lSDL3_ttf")
  INCLUDE_DIRS_FOR_SHIM+=("$SDL3_TTF_HDR_DIR")
else
  echo "[ERROR] Missing SDL3_ttf development files (pkg-config or SDL3_ttf/SDL_ttf.h)."
  exit 1
fi

# Optional mixer
if [ -n "$PKG_MIXER" ]; then
  append_split_flags "$(pkg-config --cflags "$PKG_MIXER" 2>/dev/null || true)" SDL_CFLAGS_ARR
  append_split_flags "$(pkg-config --libs "$PKG_MIXER" 2>/dev/null || true)" MIXER_LIBS_ARR
  if [ -f /usr/include/SDL3_mixer/SDL_mixer.h ]; then
    INCLUDE_DIRS_FOR_SHIM+=(/usr/include/SDL3_mixer)
  fi
  if [ -f /usr/local/include/SDL3_mixer/SDL_mixer.h ]; then
    INCLUDE_DIRS_FOR_SHIM+=(/usr/local/include/SDL3_mixer)
  fi
elif SDL3_MIXER_HDR_DIR="$(find_header_dir SDL3_mixer/SDL_mixer.h)"; then
  SDL_CFLAGS_ARR+=("-I$SDL3_MIXER_HDR_DIR")
  MIXER_LIBS_ARR+=("-lSDL3_mixer")
  INCLUDE_DIRS_FOR_SHIM+=("$SDL3_MIXER_HDR_DIR")
else
  echo "[WARN] SDL3_mixer not found; building without mixer support."
fi

# Build compatibility include shim for lowercase includes like <sdl3/SDL.h>
mkdir -p "$BUILD_INCLUDE_DIR"
for inc in "${INCLUDE_DIRS_FOR_SHIM[@]}"; do
  [ -d "$inc" ] || continue
  for h in "$inc"/*.h; do
    [ -f "$h" ] || continue
    ln -sf "$h" "$BUILD_INCLUDE_DIR/$(basename "$h")"
  done
done
SDL_CFLAGS_ARR+=("-I$ROOT_DIR/.build/include" "-I$ROOT_DIR/.build/include/sdl3")

echo "[INFO] SDL include shim: $BUILD_INCLUDE_DIR"

SDL_VERSION_LOG="unknown"
if [ -n "$PKG_SDL3" ]; then
  SDL_VERSION_LOG="$(PKG_CONFIG_PATH="$SDL3_PKG_CONFIG_PATH" pkg-config --modversion "$PKG_SDL3" 2>/dev/null || echo unknown)"
elif [ -f /usr/include/SDL3/SDL_version.h ]; then
  SDL_MAJ="$(awk '/#define[[:space:]]+SDL_MAJOR_VERSION[[:space:]]+[0-9]+/{print $3; exit}' /usr/include/SDL3/SDL_version.h)"
  SDL_MIN="$(awk '/#define[[:space:]]+SDL_MINOR_VERSION[[:space:]]+[0-9]+/{print $3; exit}' /usr/include/SDL3/SDL_version.h)"
  SDL_MIC="$(awk '/#define[[:space:]]+SDL_MICRO_VERSION[[:space:]]+[0-9]+/{print $3; exit}' /usr/include/SDL3/SDL_version.h)"
  SDL_VERSION_LOG="${SDL_MAJ:-0}.${SDL_MIN:-0}.${SDL_MIC:-0}"
fi
echo "[INFO] SDL3 version in use: $SDL_VERSION_LOG (required: $SDL_REQUIRED_VERSION)"

if [ -n "$PKG_SDL3" ] && ! PKG_CONFIG_PATH="$SDL3_PKG_CONFIG_PATH" pkg-config --atleast-version="$SDL_REQUIRED_VERSION" "$PKG_SDL3"; then
  echo "[WARN] sdl3 ${SDL_REQUIRED_VERSION}+ not found. Continuing with installed version: $SDL_VERSION_LOG"
fi

if [ "$MODE" = "clean" ]; then
  rm -f "$OUT_PLATFORMER" "$OUT_SHEET"
  echo "[OK] Cleaned $OUT_PLATFORMER and $OUT_SHEET"
  exit 0
fi

if [ "$MODE" = "check" ]; then
  echo "[OK] Dependency check passed"
  exit 0
fi

JSON_FLAGS=()
if [ -f "$ROOT_DIR/third_party/nlohmann/json.hpp" ]; then
  JSON_FLAGS+=("-I$ROOT_DIR/third_party")
fi

PLATFORMER_SOURCES=(
  "$SRC_DIR/main.cpp"
  "$SRC_DIR/TileMap.cpp"
  "$SRC_DIR/AssetPath.cpp"
  "$SRC_DIR/LevelLoader.cpp"
  "$SRC_DIR/TextRenderer.cpp"
  "$SRC_DIR/LevelSelect.cpp"
  "$SRC_DIR/PlayerController.cpp"
  "$SRC_DIR/LevelManager.cpp"
  "$SRC_DIR/GameSupport.cpp"
  "$SRC_DIR/CrashReporter.cpp"
  "$SRC_DIR/FrontendMenu.cpp"
)

SHEET_SOURCES=(
  "$SRC_DIR/SheetConfigTool.cpp"
)

"$CXX" "${CXXFLAGS[@]}" \
  "${SDL_CFLAGS_ARR[@]}" \
  "${JSON_FLAGS[@]}" \
  "${PLATFORMER_SOURCES[@]}" \
  "${SDL_LIBS_ARR[@]}" \
  "${MIXER_LIBS_ARR[@]}" \
  -o "$OUT_PLATFORMER"

if [ "$BUILD_SHEET" = "1" ]; then
  "$CXX" "${CXXFLAGS[@]}" \
    "${SDL_CFLAGS_ARR[@]}" \
    "${SHEET_SOURCES[@]}" \
    "${SDL_LIBS_ARR[@]}" \
    -o "$OUT_SHEET"
else
  echo "[INFO] Skipping sheet tool build (set BUILD_SHEET=1 to enable)."
fi

echo "[OK] Build complete -> $OUT_PLATFORMER and $OUT_SHEET"

if [ "$MODE" = "run" ]; then
  echo "[INFO] Launching $OUT_PLATFORMER"
  exec "$OUT_PLATFORMER"
fi
