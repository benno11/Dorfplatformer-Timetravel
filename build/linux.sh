#!/usr/bin/env bash
set -euo pipefail

# Linux build helper for platformer + sheet_config.
# Modes:
#   ./build/linux.sh           -> build
#   ./build/linux.sh build     -> build
#   ./build/linux.sh check     -> dependency check only
#   ./build/linux.sh run       -> build then run platformer
#   ./build/linux.sh clean     -> remove built binaries
#
# Env toggles:
#   FAST=1 (default unless RELEASE=1)  -> very fast dev build flags
#   RELEASE=1                          -> optimized release flags
#   LTO=1                              -> enable link-time optimization (slow)

MODE="${1:-build}"
CXX="${CXX:-g++}"
FAST="${FAST:-0}"
RELEASE="${RELEASE:-0}"
LTO="${LTO:-0}"
SDL_REQUIRED_VERSION="${SDL_REQUIRED_VERSION:-3.4.0}"
BUILD_SHEET="${BUILD_SHEET:-0}"
AUTO_BUILD_UPDATED_SCRIPTS="${AUTO_BUILD_UPDATED_SCRIPTS:-1}"

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$ROOT_DIR/src"
OUT_PLATFORMER="$ROOT_DIR/platformer"
OUT_SHEET="$ROOT_DIR/sheet_config"
BUILD_INCLUDE_DIR="$ROOT_DIR/.build/include/sdl3"
LINUX_BUILD_DIR="$ROOT_DIR/.build/linux"

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

COMPILER=("$CXX")
if command -v ccache >/dev/null 2>&1; then
  COMPILER=("ccache" "$CXX")
  echo "[INFO] Using ccache compiler launcher"
fi

# Default to fast dev builds unless RELEASE=1 is set.
if [ "$FAST" = "0" ] && [ "$RELEASE" != "1" ]; then
  FAST="1"
fi

if [ "$FAST" = "1" ]; then
  CXXFLAGS=("-std=c++17" "-O1" "-g0" "-fno-plt" "-pipe" "-DSDL_ENABLE_OLD_NAMES=1")
  echo "[INFO] FAST build enabled (default)"
else
  CXXFLAGS=("-std=c++17" "-O3" "-DNDEBUG" "-fno-plt" "-pipe" "-DSDL_ENABLE_OLD_NAMES=1")
fi

if [ "$LTO" = "1" ]; then
  CXXFLAGS+=("-flto")
  echo "[INFO] LTO enabled"
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

# Prefer rebuilt/local SDL3_mixer pkg-config metadata first.
MIXER_PKG_CONFIG_PATH="$SDL3_PKG_CONFIG_PATH${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
LOCAL_MIXER_PC_PATH=""
for d in \
  "$ROOT_DIR/deps/linux-sdl3-mixer-src/build" \
  "/usr/local/lib/pkgconfig" \
  "/usr/lib/pkgconfig"; do
  if [ -f "$d/sdl3-mixer.pc" ] || [ -f "$d/sdl3_mixer.pc" ]; then
    LOCAL_MIXER_PC_PATH="$d"
    break
  fi
done
if [ -n "$LOCAL_MIXER_PC_PATH" ]; then
  MIXER_PKG_CONFIG_PATH="$LOCAL_MIXER_PC_PATH:$SDL3_PKG_CONFIG_PATH${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
  echo "[INFO] Using SDL3_mixer pkg-config path: $LOCAL_MIXER_PC_PATH"
fi

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
  if PKG_CONFIG_PATH="$MIXER_PKG_CONFIG_PATH" pkg-config --exists "$p"; then
    PKG_MIXER="$p"
    break
  fi
done
PKG_CURL=""
for p in libcurl curl; do
  if pkg-config --exists "$p"; then
    PKG_CURL="$p"
    break
  fi
done

SDL_CFLAGS_ARR=()
SDL_LIBS_ARR=()
MIXER_LIBS_ARR=()
CURL_CFLAGS_ARR=()
CURL_LIBS_ARR=()
INCLUDE_DIRS_FOR_SHIM=()
# Always scan common SDL include roots for lowercase shim generation.
for d in /usr/include/SDL3 /usr/include/SDL3_image /usr/include/SDL3_ttf /usr/include/SDL3_mixer /usr/local/include/SDL3 /usr/local/include/SDL3_image /usr/local/include/SDL3_ttf /usr/local/include/SDL3_mixer; do
  if [ -d "$d" ]; then
    INCLUDE_DIRS_FOR_SHIM+=("$d")
  fi
done
if [ -d "$ROOT_DIR/deps/linux-sdl3-src/include/SDL3" ]; then
  INCLUDE_DIRS_FOR_SHIM+=("$ROOT_DIR/deps/linux-sdl3-src/include/SDL3")
fi
if [ -d "$ROOT_DIR/deps/linux-sdl3-image-src/include/SDL3_image" ]; then
  INCLUDE_DIRS_FOR_SHIM+=("$ROOT_DIR/deps/linux-sdl3-image-src/include/SDL3_image")
fi
if [ -d "$ROOT_DIR/deps/linux-sdl3-ttf-src/include/SDL3_ttf" ]; then
  INCLUDE_DIRS_FOR_SHIM+=("$ROOT_DIR/deps/linux-sdl3-ttf-src/include/SDL3_ttf")
fi
if [ -d "$ROOT_DIR/deps/linux-sdl3-mixer-src/include/SDL3_mixer" ]; then
  INCLUDE_DIRS_FOR_SHIM+=("$ROOT_DIR/deps/linux-sdl3-mixer-src/include/SDL3_mixer")
fi

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
  if [ -f "$ROOT_DIR/deps/linux-sdl3-src/include/SDL3/SDL.h" ]; then
    SDL_CFLAGS_ARR+=("-I$ROOT_DIR/deps/linux-sdl3-src/include")
    INCLUDE_DIRS_FOR_SHIM+=("$ROOT_DIR/deps/linux-sdl3-src/include/SDL3")
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
elif [ -f "$ROOT_DIR/deps/linux-sdl3-image-src/include/SDL3_image/SDL_image.h" ]; then
  SDL_CFLAGS_ARR+=("-I$ROOT_DIR/deps/linux-sdl3-image-src/include")
  SDL_LIBS_ARR+=("-lSDL3_image")
  INCLUDE_DIRS_FOR_SHIM+=("$ROOT_DIR/deps/linux-sdl3-image-src/include/SDL3_image")
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
elif [ -f "$ROOT_DIR/deps/linux-sdl3-ttf-src/include/SDL3_ttf/SDL_ttf.h" ]; then
  SDL_CFLAGS_ARR+=("-I$ROOT_DIR/deps/linux-sdl3-ttf-src/include")
  SDL_LIBS_ARR+=("-lSDL3_ttf")
  INCLUDE_DIRS_FOR_SHIM+=("$ROOT_DIR/deps/linux-sdl3-ttf-src/include/SDL3_ttf")
else
  echo "[ERROR] Missing SDL3_ttf development files (pkg-config or SDL3_ttf/SDL_ttf.h)."
  exit 1
fi

# Optional mixer
LOCAL_MIXER_SO="$ROOT_DIR/deps/linux-sdl3-mixer-src/build/libSDL3_mixer.so"
if [ -f "$LOCAL_MIXER_SO" ]; then
  MIXER_LIBS_ARR+=("$LOCAL_MIXER_SO" "-Wl,-rpath,$ROOT_DIR/deps/linux-sdl3-mixer-src/build")
  echo "[INFO] SDL3_mixer link target: $LOCAL_MIXER_SO"
  if [ -f "$ROOT_DIR/deps/linux-sdl3-mixer-src/include/SDL3_mixer/SDL_mixer.h" ]; then
    SDL_CFLAGS_ARR+=("-I$ROOT_DIR/deps/linux-sdl3-mixer-src/include")
    INCLUDE_DIRS_FOR_SHIM+=("$ROOT_DIR/deps/linux-sdl3-mixer-src/include/SDL3_mixer")
  fi
elif [ -n "$PKG_MIXER" ]; then
  append_split_flags "$(PKG_CONFIG_PATH="$MIXER_PKG_CONFIG_PATH" pkg-config --cflags "$PKG_MIXER" 2>/dev/null || true)" SDL_CFLAGS_ARR
  append_split_flags "$(PKG_CONFIG_PATH="$MIXER_PKG_CONFIG_PATH" pkg-config --libs "$PKG_MIXER" 2>/dev/null || true)" MIXER_LIBS_ARR
  MIXER_LIBDIR="$(PKG_CONFIG_PATH="$MIXER_PKG_CONFIG_PATH" pkg-config --variable=libdir "$PKG_MIXER" 2>/dev/null || echo unknown)"
  echo "[INFO] SDL3_mixer link target: $MIXER_LIBDIR/libSDL3_mixer.so"
  if [ -f /usr/include/SDL3_mixer/SDL_mixer.h ]; then
    INCLUDE_DIRS_FOR_SHIM+=(/usr/include/SDL3_mixer)
  fi
  if [ -f /usr/local/include/SDL3_mixer/SDL_mixer.h ]; then
    INCLUDE_DIRS_FOR_SHIM+=(/usr/local/include/SDL3_mixer)
  fi
  if [ -f "$ROOT_DIR/deps/linux-sdl3-mixer-src/include/SDL3_mixer/SDL_mixer.h" ]; then
    SDL_CFLAGS_ARR+=("-I$ROOT_DIR/deps/linux-sdl3-mixer-src/include")
    INCLUDE_DIRS_FOR_SHIM+=("$ROOT_DIR/deps/linux-sdl3-mixer-src/include/SDL3_mixer")
  fi
elif SDL3_MIXER_HDR_DIR="$(find_header_dir SDL3_mixer/SDL_mixer.h)"; then
  SDL_CFLAGS_ARR+=("-I$SDL3_MIXER_HDR_DIR")
  MIXER_LIBS_ARR+=("-lSDL3_mixer")
  INCLUDE_DIRS_FOR_SHIM+=("$SDL3_MIXER_HDR_DIR")
else
  echo "[WARN] SDL3_mixer not found; building without mixer support."
fi

# Optional libcurl for Firebase Realtime Database REST reads.
if [ -n "$PKG_CURL" ]; then
  append_split_flags "$(pkg-config --cflags "$PKG_CURL" 2>/dev/null || true)" CURL_CFLAGS_ARR
  append_split_flags "$(pkg-config --libs "$PKG_CURL" 2>/dev/null || true)" CURL_LIBS_ARR
  CXXFLAGS+=("-DHAVE_CURL=1")
  echo "[INFO] libcurl enabled via pkg-config package: $PKG_CURL"
else
  CXXFLAGS+=("-DHAVE_CURL=0")
  echo "[WARN] libcurl not found; remote Firebase level fetch disabled."
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
  rm -rf "$LINUX_BUILD_DIR"
  echo "[OK] Cleaned $OUT_PLATFORMER and $OUT_SHEET"
  exit 0
fi

if [ "$MODE" = "check" ]; then
  echo "[OK] Dependency check passed"
  exit 0
fi

# Ensure clean runtime output file before each build.
if [ "$MODE" = "build" ] || [ "$MODE" = "run" ]; then
  rm -f "$ROOT_DIR/bin"
fi

JSON_FLAGS=()
if [ -f "$ROOT_DIR/third_party/nlohmann/json.hpp" ]; then
  JSON_FLAGS+=("-I$ROOT_DIR/third_party")
fi

PLATFORMER_SOURCES=(
  "$SRC_DIR/main.cpp"
  "$SRC_DIR/GameApp.cpp"
  "$SRC_DIR/AndroidEntrypoints.cpp"
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
  "$SRC_DIR/AudioSystem.cpp"
  "$SRC_DIR/ParallaxRenderer.cpp"
  "$SRC_DIR/World3PatternBackground.cpp"
)

SHEET_SOURCES=(
  "$SRC_DIR/SheetConfigTool.cpp"
)

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

PLATFORMER_OBJ_DIR="$LINUX_BUILD_DIR/obj/platformer"
PLATFORMER_DEP_DIR="$LINUX_BUILD_DIR/dep/platformer"
PLATFORMER_FLAGS_STAMP="$LINUX_BUILD_DIR/.platformer-flags"
SCRIPT_HASH_STAMP="$LINUX_BUILD_DIR/.build-scripts-hash"
mkdir -p "$PLATFORMER_OBJ_DIR" "$PLATFORMER_DEP_DIR"

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
  echo "[INFO] Build scripts changed; clearing Linux object cache"
  rm -rf "$LINUX_BUILD_DIR/obj" "$LINUX_BUILD_DIR/dep"
  mkdir -p "$PLATFORMER_OBJ_DIR" "$PLATFORMER_DEP_DIR"
  printf '%s\n' "$current_script_hash" > "$SCRIPT_HASH_STAMP"
fi

platformer_flags_text="$(printf '%s\n' "${COMPILER[@]}" "${CXXFLAGS[@]}" "${SDL_CFLAGS_ARR[@]}" "${CURL_CFLAGS_ARR[@]}" "${JSON_FLAGS[@]}")"
if [ ! -f "$PLATFORMER_FLAGS_STAMP" ] || ! printf '%s\n' "$platformer_flags_text" | cmp -s - "$PLATFORMER_FLAGS_STAMP"; then
  echo "[INFO] Platformer compile flags changed; clearing Linux object cache"
  rm -rf "$PLATFORMER_OBJ_DIR" "$PLATFORMER_DEP_DIR"
  mkdir -p "$PLATFORMER_OBJ_DIR" "$PLATFORMER_DEP_DIR"
  printf '%s\n' "$platformer_flags_text" > "$PLATFORMER_FLAGS_STAMP"
fi

PLATFORMER_OBJECTS=()
platformer_compiled=0
for src in "${PLATFORMER_SOURCES[@]}"; do
  rel="${src#$ROOT_DIR/}"
  obj="$PLATFORMER_OBJ_DIR/${rel%.cpp}.o"
  dep="$PLATFORMER_DEP_DIR/${rel%.cpp}.d"
  mkdir -p "$(dirname "$obj")" "$(dirname "$dep")"
  PLATFORMER_OBJECTS+=("$obj")

  needs_build=0
  if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
    needs_build=1
  elif dep_needs_rebuild "$dep" "$obj"; then
    needs_build=1
  fi

  if [ "$needs_build" -eq 1 ]; then
    echo "[INFO] Compiling $rel"
    "${COMPILER[@]}" "${CXXFLAGS[@]}" \
      "${SDL_CFLAGS_ARR[@]}" \
      "${CURL_CFLAGS_ARR[@]}" \
      "${JSON_FLAGS[@]}" \
      -MMD -MP -MF "$dep" -c "$src" -o "$obj"
    platformer_compiled=1
  else
    echo "[INFO] Skipping unchanged $rel"
  fi
done

platformer_needs_link=0
if [ "$platformer_compiled" -eq 1 ] || [ ! -f "$OUT_PLATFORMER" ]; then
  platformer_needs_link=1
else
  for obj in "${PLATFORMER_OBJECTS[@]}"; do
    if [ "$obj" -nt "$OUT_PLATFORMER" ]; then
      platformer_needs_link=1
      break
    fi
  done
fi

if [ "$platformer_needs_link" -eq 1 ]; then
  "${COMPILER[@]}" "${CXXFLAGS[@]}" \
    "${PLATFORMER_OBJECTS[@]}" \
    "${CURL_LIBS_ARR[@]}" \
    "${MIXER_LIBS_ARR[@]}" \
    "${SDL_LIBS_ARR[@]}" \
    -o "$OUT_PLATFORMER"
else
  echo "[INFO] Skipping unchanged link: $OUT_PLATFORMER"
fi

should_build_sheet=0
if [ "$BUILD_SHEET" = "1" ]; then
  should_build_sheet=1
elif [ "$AUTO_BUILD_UPDATED_SCRIPTS" = "1" ]; then
  if [ ! -f "$OUT_SHEET" ]; then
    should_build_sheet=1
  else
    for src in "${SHEET_SOURCES[@]}"; do
      if [ "$src" -nt "$OUT_SHEET" ]; then
        should_build_sheet=1
        break
      fi
    done
  fi
fi

if [ "$should_build_sheet" = "1" ]; then
  SHEET_OBJ_DIR="$LINUX_BUILD_DIR/obj/sheet"
  SHEET_DEP_DIR="$LINUX_BUILD_DIR/dep/sheet"
  SHEET_FLAGS_STAMP="$LINUX_BUILD_DIR/.sheet-flags"
  mkdir -p "$SHEET_OBJ_DIR" "$SHEET_DEP_DIR"

  sheet_flags_text="$(printf '%s\n' "${COMPILER[@]}" "${CXXFLAGS[@]}" "${SDL_CFLAGS_ARR[@]}")"
  if [ ! -f "$SHEET_FLAGS_STAMP" ] || ! printf '%s\n' "$sheet_flags_text" | cmp -s - "$SHEET_FLAGS_STAMP"; then
    echo "[INFO] Sheet compile flags changed; clearing Linux object cache"
    rm -rf "$SHEET_OBJ_DIR" "$SHEET_DEP_DIR"
    mkdir -p "$SHEET_OBJ_DIR" "$SHEET_DEP_DIR"
    printf '%s\n' "$sheet_flags_text" > "$SHEET_FLAGS_STAMP"
  fi

  SHEET_OBJECTS=()
  sheet_compiled=0
  for src in "${SHEET_SOURCES[@]}"; do
    rel="${src#$ROOT_DIR/}"
    obj="$SHEET_OBJ_DIR/${rel%.cpp}.o"
    dep="$SHEET_DEP_DIR/${rel%.cpp}.d"
    mkdir -p "$(dirname "$obj")" "$(dirname "$dep")"
    SHEET_OBJECTS+=("$obj")

    needs_build=0
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
      needs_build=1
    elif dep_needs_rebuild "$dep" "$obj"; then
      needs_build=1
    fi

    if [ "$needs_build" -eq 1 ]; then
      echo "[INFO] Compiling $rel"
      "${COMPILER[@]}" "${CXXFLAGS[@]}" \
        "${SDL_CFLAGS_ARR[@]}" \
        -MMD -MP -MF "$dep" -c "$src" -o "$obj"
      sheet_compiled=1
    else
      echo "[INFO] Skipping unchanged $rel"
    fi
  done

  sheet_needs_link=0
  if [ "$sheet_compiled" -eq 1 ] || [ ! -f "$OUT_SHEET" ]; then
    sheet_needs_link=1
  else
    for obj in "${SHEET_OBJECTS[@]}"; do
      if [ "$obj" -nt "$OUT_SHEET" ]; then
        sheet_needs_link=1
        break
      fi
    done
  fi

  if [ "$sheet_needs_link" -eq 1 ]; then
    "${COMPILER[@]}" "${CXXFLAGS[@]}" \
      "${SHEET_OBJECTS[@]}" \
      "${SDL_LIBS_ARR[@]}" \
      -o "$OUT_SHEET"
  else
    echo "[INFO] Skipping unchanged link: $OUT_SHEET"
  fi
else
  echo "[INFO] Skipping unchanged script tool build (set BUILD_SHEET=1 to force)."
fi

echo "[OK] Build complete -> $OUT_PLATFORMER and $OUT_SHEET"

if [ "$MODE" = "run" ]; then
  echo "[INFO] Launching $OUT_PLATFORMER"
  exec "$OUT_PLATFORMER"
fi
