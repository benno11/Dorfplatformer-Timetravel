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

compute_build_code_id() {
  local id=""
  if command -v git >/dev/null 2>&1 && git -C "$ROOT_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    local head dirty=""
    head="$(git -C "$ROOT_DIR" rev-parse --short=12 HEAD 2>/dev/null || true)"
    if [ -n "$head" ]; then
      if ! git -C "$ROOT_DIR" diff --quiet -- src 2>/dev/null || ! git -C "$ROOT_DIR" diff --cached --quiet -- src 2>/dev/null; then
        dirty="-dirty"
      fi
      id="git-${head}${dirty}"
    fi
  fi
  if [ -z "$id" ] && command -v sha256sum >/dev/null 2>&1; then
    local sum=""
    sum="$(find "$ROOT_DIR/src" -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.h' \) -print0 | sort -z | xargs -0 cat | sha256sum | awk '{print substr($1,1,12)}' || true)"
    if [ -n "$sum" ]; then
      id="src-$sum"
    fi
  fi
  if [ -z "$id" ]; then
    id="dev"
  fi
  local nonce="${BUILD_UUID_NONCE:-}"
  if [ -z "$nonce" ]; then
    nonce="$(date +%s%N 2>/dev/null || true)"
    if [ -z "$nonce" ]; then
      nonce="$(date +%s)-$$"
    fi
  fi
  id="${id}-b${nonce}"
  printf '%s' "$id"
}

BUILD_CODE_ID="$(compute_build_code_id)"
BUILD_CODE_HEADER="$ROOT_DIR/.build/generated/BuildCodeId.h"
mkdir -p "$(dirname "$BUILD_CODE_HEADER")"
build_code_header_text="#pragma once
#define DF_BUILD_CODE_ID \"$BUILD_CODE_ID\"
"
if [ ! -f "$BUILD_CODE_HEADER" ] || ! printf '%s' "$build_code_header_text" | cmp -s - "$BUILD_CODE_HEADER"; then
  printf '%s' "$build_code_header_text" > "$BUILD_CODE_HEADER"
fi

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

SRC=(
  src/main.cpp
  src/GameApp.cpp
  src/AndroidEntrypoints.cpp
  src/TileMap.cpp
  src/AssetPath.cpp
  src/LevelLoader.cpp
  src/TextRenderer.cpp
  src/LevelSelect.cpp
  src/PlayerController.cpp
  src/LevelManager.cpp
  src/GameSupport.cpp
  src/CrashReporter.cpp
  src/FrontendMenu.cpp
  src/AudioSystem.cpp
  src/ParallaxRenderer.cpp
)

OBJ_DIR="$ROOT_DIR/.build/linux-reuse/obj"
DEP_DIR="$ROOT_DIR/.build/linux-reuse/dep"
FLAGS_STAMP="$ROOT_DIR/.build/linux-reuse/.compile-flags"
SCRIPT_HASH_STAMP="$ROOT_DIR/.build/linux-reuse/.build-scripts-hash"
mkdir -p "$OBJ_DIR" "$DEP_DIR"

current_script_hash=""
if command -v sha256sum >/dev/null 2>&1; then
  current_script_hash="$(
    find "$ROOT_DIR/build" -maxdepth 1 -type f -name '*.sh' -print0 \
      | sort -z \
      | xargs -0 sha256sum \
      | sha256sum \
      | awk '{print $1}'
  )"
fi
if [ -z "$current_script_hash" ]; then
  current_script_hash="scripts-$(date +%s)"
fi
if [ ! -f "$SCRIPT_HASH_STAMP" ] || ! printf '%s\n' "$current_script_hash" | cmp -s - "$SCRIPT_HASH_STAMP"; then
  echo "[INFO] Build scripts changed; clearing Linux-reuse object cache"
  rm -rf "$OBJ_DIR" "$DEP_DIR"
  mkdir -p "$OBJ_DIR" "$DEP_DIR"
  printf '%s\n' "$current_script_hash" > "$SCRIPT_HASH_STAMP"
fi

current_flags_text="$(printf '%s\n' "$CXX" "$CXXFLAGS" "$SDL_CFLAGS" "$JSON_CFLAGS")"
if [ ! -f "$FLAGS_STAMP" ] || ! printf '%s\n' "$current_flags_text" | cmp -s - "$FLAGS_STAMP"; then
  echo "[INFO] Compile flags changed; clearing Linux-reuse object cache"
  rm -rf "$OBJ_DIR" "$DEP_DIR"
  mkdir -p "$OBJ_DIR" "$DEP_DIR"
  printf '%s\n' "$current_flags_text" > "$FLAGS_STAMP"
fi

dep_needs_rebuild() {
  local depfile="$1"
  local objfile="$2"
  [ -f "$depfile" ] || return 0
  local path
  while IFS= read -r path; do
    [ -n "$path" ] || continue
    if [ ! -e "$path" ] || [ "$path" -nt "$objfile" ]; then
      return 0
    fi
  done < <(
    tr '\\\n' '  ' < "$depfile" |
      sed -E 's/^[^:]*:[[:space:]]*//' |
      tr ' ' '\n' |
      sed '/^$/d'
  )
  return 1
}

OBJECTS=()
compiled_any=0
for src in "${SRC[@]}"; do
  obj="$OBJ_DIR/${src%.cpp}.o"
  dep="$DEP_DIR/${src%.cpp}.d"
  mkdir -p "$(dirname "$obj")" "$(dirname "$dep")"
  OBJECTS+=("$obj")

  needs_build=0
  if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
    needs_build=1
  elif dep_needs_rebuild "$dep" "$obj"; then
    needs_build=1
  fi

  if [ "$needs_build" -eq 1 ]; then
    echo "[INFO] Compiling $src"
    g++ $CXXFLAGS \
      $SDL_CFLAGS \
      $JSON_CFLAGS \
      -MMD -MP -MF "$dep" -c "$src" -o "$obj"
    compiled_any=1
  else
    echo "[INFO] Skipping unchanged $src"
  fi
done

needs_link=0
if [ "$compiled_any" -eq 1 ] || [ ! -f "$PLATFORMER_BIN" ]; then
  needs_link=1
else
  for obj in "${OBJECTS[@]}"; do
    if [ "$obj" -nt "$PLATFORMER_BIN" ]; then
      needs_link=1
      break
    fi
  done
fi

if [ "$needs_link" -eq 1 ]; then
  echo "[INFO] Re-linking main binary: $PLATFORMER_BIN"
  g++ $CXXFLAGS \
    "${OBJECTS[@]}" \
    $SDL_LIBS \
    $MIXER_LIBS \
    -o "$PLATFORMER_BIN"
else
  echo "[INFO] Skipping unchanged link: $PLATFORMER_BIN"
fi

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
