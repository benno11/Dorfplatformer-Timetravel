#!/usr/bin/env bash
set -euo pipefail

# Avoid host include/library contamination during cross-compile.
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH INCLUDE LIBRARY_PATH LD_LIBRARY_PATH

# Android NDK cross-build script
#
# Required env vars:
#   ANDROID_NDK_HOME   -> path to Android NDK
#   SDL3_ANDROID_ROOT  -> root folder containing Android builds of SDL3 libs/includes
#
# Optional env vars:
#   API                -> Android API level (default: 24)
#   ABI                -> arm64-v8a | armeabi-v7a | arm32 | x86_64 | x86-64 | x86 | riscv64 | all (default: all)
#   ABIS               -> space/comma-separated ABI list (overrides ABI)
#   FAST               -> 1 for fast iteration build flags (default: 0)
#   STRIP_ANDROID_SO   -> 1 to strip libplatformer.so after link (default: 1)
#   SDL3_IMAGE_ROOT    -> root folder for SDL3_image Android build
#   SDL3_TTF_ROOT      -> root folder for SDL3_ttf Android build
#   SDL3_MIXER_ROOT    -> root folder for SDL3_mixer Android build
#   CURL_ANDROID_ROOT  -> root folder for Android libcurl build (optional)
#   ANDROID_ALLOW_NO_CURL -> 1 to allow building Android without curl (default: 0)
#
# Expected folder layout per *_ROOT:
#   include/
#   lib/<abi>/
#
# Output:
#   build/android/<abi>/libplatformer.so

SAVED_ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-}"

if [ -f "build/android.env" ]; then
  # shellcheck disable=SC1091
  . "build/android.env"
fi

if [ -n "$SAVED_ANDROID_NDK_HOME" ]; then
  ANDROID_NDK_HOME="$SAVED_ANDROID_NDK_HOME"
  export ANDROID_NDK_HOME
fi

compute_build_code_id() {
  local id=""
  if command -v git >/dev/null 2>&1 && git -C "$PWD" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    local head dirty=""
    head="$(git -C "$PWD" rev-parse --short=12 HEAD 2>/dev/null || true)"
    if [ -n "$head" ]; then
      if ! git -C "$PWD" diff --quiet -- src 2>/dev/null || ! git -C "$PWD" diff --cached --quiet -- src 2>/dev/null; then
        dirty="-dirty"
      fi
      id="git-${head}${dirty}"
    fi
  fi
  if [ -z "$id" ] && command -v sha256sum >/dev/null 2>&1; then
    local sum=""
    sum="$(find "$PWD/src" -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.h' \) -print0 | sort -z | xargs -0 cat | sha256sum | awk '{print substr($1,1,12)}' || true)"
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
BUILD_CODE_HEADER="$PWD/.build/generated/BuildCodeId.h"
mkdir -p "$(dirname "$BUILD_CODE_HEADER")"
build_code_header_text="#pragma once
#define DF_BUILD_CODE_ID \"$BUILD_CODE_ID\"
"
if [ ! -f "$BUILD_CODE_HEADER" ] || ! printf '%s' "$build_code_header_text" | cmp -s - "$BUILD_CODE_HEADER"; then
  printf '%s' "$build_code_header_text" > "$BUILD_CODE_HEADER"
fi

FORCE_STAGED_SDL_ROOT="${FORCE_STAGED_SDL_ROOT:-1}"
if [ "$FORCE_STAGED_SDL_ROOT" = "1" ]; then
  SDL3_ANDROID_ROOT="$PWD/deps/android"
  SDL3_IMAGE_ROOT="$SDL3_ANDROID_ROOT"
  SDL3_TTF_ROOT="$SDL3_ANDROID_ROOT"
  SDL3_MIXER_ROOT="$SDL3_ANDROID_ROOT"
  export SDL3_ANDROID_ROOT SDL3_IMAGE_ROOT SDL3_TTF_ROOT SDL3_MIXER_ROOT
  echo "[INFO] FORCE_STAGED_SDL_ROOT=1 -> using staged SDL root: $SDL3_ANDROID_ROOT"
fi

if [ -z "${ANDROID_NDK_HOME:-}" ]; then
  for c in \
    "$HOME/Android/Sdk/ndk-bundle" \
    "$HOME/Android/Sdk/ndk" \
    "$HOME/android-sdk/ndk-bundle" \
    "$HOME/android-sdk/ndk" \
    "/opt/android-ndk"; do
    if [ -x "$c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++" ]; then
      ANDROID_NDK_HOME="$c"
      export ANDROID_NDK_HOME
      break
    fi
  done
fi

if [ -z "${ANDROID_NDK_HOME:-}" ] && [ -d "$HOME/Android/Sdk/ndk" ]; then
  latest_ndk="$(find "$HOME/Android/Sdk/ndk" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -n1 || true)"
  if [ -n "$latest_ndk" ] && [ -x "$latest_ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++" ]; then
    ANDROID_NDK_HOME="$latest_ndk"
    export ANDROID_NDK_HOME
  fi
fi

if [ -z "${SDL3_ANDROID_ROOT:-}" ]; then
  for c in \
    "$PWD/deps/android" \
    "$PWD/deps/android-sdl" \
    "$PWD/deps/sdl-android" \
    "$PWD/deps/SDL-android" \
    "$HOME/Android/sdl3-android" \
    "$HOME/Android/sdl3" \
    "$HOME/sdl3-android" \
    "$HOME/Android/sdl3"; do
    if [ -d "$c/include" ] && [ -d "$c/lib" ] && \
       { [ -d "$c/lib/arm64-v8a" ] || [ -d "$c/lib/armeabi-v7a" ] || [ -d "$c/lib/x86_64" ] || [ -d "$c/lib/x86" ] || [ -d "$c/lib/riscv64" ]; }; then
      SDL3_ANDROID_ROOT="$c"
      export SDL3_ANDROID_ROOT
      break
    fi
  done
fi

: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME}"
if [ -z "${SDL3_ANDROID_ROOT:-}" ]; then
  echo "[ERROR] SDL3_ANDROID_ROOT not found."
  echo "[HINT] Expected layout:"
  echo "       <root>/include"
  echo "       <root>/lib/<abi>"
  echo "[HINT] Set it explicitly, for example:"
  echo "       SDL3_ANDROID_ROOT=/path/to/sdl-android ./build/android.sh"
  echo "[INFO] Checked candidates:"
  printf '  - %s\n' \
    "$PWD/deps/android" \
    "$PWD/deps/android-sdl" \
    "$PWD/deps/sdl-android" \
    "$PWD/deps/SDL-android" \
    "$HOME/Android/sdl3-android" \
    "$HOME/Android/sdl3" \
    "$HOME/sdl3-android" \
    "$HOME/Android/sdl3"
  exit 1
fi

if [ ! -f "$SDL3_ANDROID_ROOT/include/SDL3/SDL.h" ]; then
  echo "[ERROR] Missing staged SDL headers: $SDL3_ANDROID_ROOT/include/SDL3/SDL.h"
  echo "[HINT] Run: ./build/setup-android-sdl.sh"
  exit 1
fi
SDL3_IMAGE_ROOT="${SDL3_IMAGE_ROOT:-$SDL3_ANDROID_ROOT}"
SDL3_TTF_ROOT="${SDL3_TTF_ROOT:-$SDL3_ANDROID_ROOT}"
SDL3_MIXER_ROOT="${SDL3_MIXER_ROOT:-$SDL3_ANDROID_ROOT}"
CURL_ANDROID_ROOT="${CURL_ANDROID_ROOT:-$PWD/deps/android-curl}"
ANDROID_ALLOW_NO_CURL="${ANDROID_ALLOW_NO_CURL:-0}"

if [ ! -f "$SDL3_IMAGE_ROOT/include/SDL3/SDL_image.h" ]; then
  echo "[ERROR] Missing staged SDL_image headers: $SDL3_IMAGE_ROOT/include/SDL3/SDL_image.h"
  echo "[HINT] Run: ./build/setup-android-sdl.sh"
  exit 1
fi
if [ ! -f "$SDL3_TTF_ROOT/include/SDL3/SDL_ttf.h" ]; then
  echo "[ERROR] Missing staged SDL_ttf headers: $SDL3_TTF_ROOT/include/SDL3/SDL_ttf.h"
  echo "[HINT] Run: ./build/setup-android-sdl.sh"
  exit 1
fi
if [ ! -f "$SDL3_MIXER_ROOT/include/SDL3/SDL_mixer.h" ]; then
  echo "[ERROR] Missing staged SDL_mixer headers: $SDL3_MIXER_ROOT/include/SDL3/SDL_mixer.h"
  echo "[HINT] Run: ./build/setup-android-sdl.sh"
  exit 1
fi

API="${API:-24}"
ABI="${ABI:-all}"
ABIS="${ABIS:-}"
FAST="${FAST:-0}"
STRIP_ANDROID_SO="${STRIP_ANDROID_SO:-1}"
ANDROID_EMBED_SDL=1
SDL_REQUIRED_VERSION="${SDL_REQUIRED_VERSION:-3.4.0}"
ver_ge() {
  local a="$1" b="$2"
  local a1 a2 a3 b1 b2 b3
  IFS='.' read -r a1 a2 a3 <<<"$a"
  IFS='.' read -r b1 b2 b3 <<<"$b"
  a1="${a1:-0}"; a2="${a2:-0}"; a3="${a3:-0}"
  b1="${b1:-0}"; b2="${b2:-0}"; b3="${b3:-0}"
  if [ "$a1" -gt "$b1" ]; then return 0; fi
  if [ "$a1" -lt "$b1" ]; then return 1; fi
  if [ "$a2" -gt "$b2" ]; then return 0; fi
  if [ "$a2" -lt "$b2" ]; then return 1; fi
  [ "$a3" -ge "$b3" ]
}

normalize_abi() {
  case "$1" in
    arm32|armeabi-v7a) echo "armeabi-v7a" ;;
    x86-64|x86_64) echo "x86_64" ;;
    *) echo "$1" ;;
  esac
}

abi_target_prefix() {
  case "$1" in
    arm64-v8a) echo "aarch64-linux-android" ;;
    armeabi-v7a) echo "armv7a-linux-androideabi" ;;
    x86_64) echo "x86_64-linux-android" ;;
    x86) echo "i686-linux-android" ;;
    riscv64) echo "riscv64-linux-android" ;;
    *) echo "" ;;
  esac
}

has_staged_sdl_for_abi() {
  local abi="$1"
  [ -f "$SDL3_ANDROID_ROOT/lib/$abi/libSDL3.a" ] &&
  [ -f "$SDL3_IMAGE_ROOT/lib/$abi/libSDL3_image.a" ] &&
  [ -f "$SDL3_TTF_ROOT/lib/$abi/libSDL3_ttf.a" ] &&
  [ -f "$SDL3_MIXER_ROOT/lib/$abi/libSDL3_mixer.a" ]
}

has_staged_curl_for_abi() {
  local abi="$1"
  [ -f "$CURL_ANDROID_ROOT/include/curl/curl.h" ] &&
  [ -f "$CURL_ANDROID_ROOT/lib/$abi/libcurl.so" ]
}

if [ -z "${ANDROID_MULTI_ABI_CHILD:-}" ]; then
  default_abis=(arm64-v8a armeabi-v7a x86_64 x86)
  AUTO_SETUP_ANDROID_DEPS="${AUTO_SETUP_ANDROID_DEPS:-1}"

  multi_abis=()
  if [ -n "$ABIS" ]; then
    abis_normalized="$(printf '%s' "$ABIS" | tr ',' ' ')"
    # shellcheck disable=SC2206
    multi_abis=($abis_normalized)
    for i in "${!multi_abis[@]}"; do
      multi_abis[$i]="$(normalize_abi "${multi_abis[$i]}")"
    done
  elif [ "$ABI" = "all" ]; then
    multi_abis=("${default_abis[@]}")
  fi

  if [ "${#multi_abis[@]}" -gt 0 ]; then
    echo "[INFO] Multi-ABI build requested: ${multi_abis[*]}"
    self_script="$0"
    case "$self_script" in
      /*) ;;
      *) self_script="$PWD/$self_script" ;;
    esac
    built_abis=()
    failed_abis=()
    for one_abi in "${multi_abis[@]}"; do
      echo "[INFO] ---- building ABI=$one_abi ----"
      tool_prefix="$(abi_target_prefix "$one_abi")"
      if [ -z "$tool_prefix" ]; then
        echo "[ERROR] Unsupported ABI in multi build: $one_abi"
        failed_abis+=("$one_abi")
        continue
      fi
      one_cxx="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/${tool_prefix}${API}-clang++"
      if [ ! -x "$one_cxx" ]; then
        echo "[WARN] Skipping ABI=$one_abi (NDK compiler missing: $one_cxx)"
        failed_abis+=("$one_abi")
        continue
      fi

      if ! has_staged_sdl_for_abi "$one_abi"; then
        if [ "$AUTO_SETUP_ANDROID_DEPS" = "1" ]; then
          echo "[INFO] Missing staged SDL libs for ABI=$one_abi -> running setup-android-sdl.sh"
          if ! ABI="$one_abi" API="$API" "$PWD/build/setup-android-sdl.sh"; then
            echo "[ERROR] setup-android-sdl.sh failed for ABI=$one_abi"
            failed_abis+=("$one_abi")
            continue
          fi
        else
          echo "[ERROR] Missing staged SDL libs for ABI=$one_abi"
          failed_abis+=("$one_abi")
          continue
        fi
      fi

      if [ "${ANDROID_ALLOW_NO_CURL:-0}" != "1" ] && ! has_staged_curl_for_abi "$one_abi"; then
        if [ "$AUTO_SETUP_ANDROID_DEPS" = "1" ]; then
          echo "[INFO] Missing staged curl for ABI=$one_abi -> running setup-android-curl.sh"
          if ! ABI="$one_abi" API="$API" "$PWD/build/setup-android-curl.sh"; then
            echo "[ERROR] setup-android-curl.sh failed for ABI=$one_abi"
            failed_abis+=("$one_abi")
            continue
          fi
        else
          echo "[ERROR] Missing staged curl for ABI=$one_abi"
          failed_abis+=("$one_abi")
          continue
        fi
      fi

      if ANDROID_MULTI_ABI_CHILD=1 ABI="$one_abi" ABIS="" "$self_script"; then
        built_abis+=("$one_abi")
      else
        failed_abis+=("$one_abi")
      fi
    done
    if [ "${#failed_abis[@]}" -gt 0 ]; then
      echo "[ERROR] Multi-ABI build failed for: ${failed_abis[*]}"
      if [ "${#built_abis[@]}" -gt 0 ]; then
        echo "[INFO] Multi-ABI build succeeded for: ${built_abis[*]}"
      fi
      echo "[HINT] Missing ABI-specific staged deps are a common cause."
      echo "[HINT] Ensure SDL and curl libs exist under lib/<abi> for each requested ABI."
      exit 1
    fi
    echo "[OK] Multi-ABI build complete: ${multi_abis[*]}"
    exit 0
  fi
fi

ABI="$(normalize_abi "$ABI")"

case "$ABI" in
  arm64-v8a)
    TARGET="aarch64-linux-android${API}"
    ;;
  armeabi-v7a)
    TARGET="armv7a-linux-androideabi${API}"
    ;;
  x86_64)
    TARGET="x86_64-linux-android${API}"
    ;;
  x86)
    TARGET="i686-linux-android${API}"
    ;;
  riscv64)
    TARGET="riscv64-linux-android${API}"
    ;;
  *)
    echo "[ERROR] Unsupported ABI: $ABI"
    exit 1
    ;;
esac

HOST_TAG="linux-x86_64"
TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST_TAG"
CXX="$TOOLCHAIN/bin/${TARGET}-clang++"

if [ ! -x "$CXX" ]; then
  echo "[ERROR] NDK compiler not found: $CXX"
  echo "        Check ANDROID_NDK_HOME and host tag."
  exit 1
fi

OUT_DIR="build/android/$ABI"
mkdir -p "$OUT_DIR"
rm -f \
  "$OUT_DIR/libplatformer.so" \
  "$OUT_DIR/libSDL3.so" \
  "$OUT_DIR/libSDL3_image.so" \
  "$OUT_DIR/libSDL3_ttf.so" \
  "$OUT_DIR/libSDL3_mixer.so"

JSON_INCLUDE_ROOT="${JSON_INCLUDE_ROOT:-}"

if [ -z "$JSON_INCLUDE_ROOT" ]; then
  for c in \
    "$PWD/third_party" \
    "$HOME/.local/include"; do
    if [ -f "$c/nlohmann/json.hpp" ]; then
      JSON_INCLUDE_ROOT="$c"
      break
    fi
  done
fi
if [ ! -f "$JSON_INCLUDE_ROOT/nlohmann/json.hpp" ]; then
  echo "[ERROR] Missing nlohmann header: $JSON_INCLUDE_ROOT/nlohmann/json.hpp"
  echo "[HINT] Run: ./build/dep-android.sh"
  exit 1
fi

if [ -z "$JSON_INCLUDE_ROOT" ]; then
  echo "[ERROR] nlohmann/json.hpp not found."
  echo "[HINT] Put it at: $PWD/third_party/nlohmann/json.hpp"
  echo "[HINT] Then run: ./build/android.sh"
  exit 1
fi

CPPFLAGS=(
  -I"$JSON_INCLUDE_ROOT"
  -I"$SDL3_ANDROID_ROOT/include"
  -I"$SDL3_IMAGE_ROOT/include"
  -I"$SDL3_TTF_ROOT/include"
  -I"$SDL3_MIXER_ROOT/include"
)

# Compatibility shim for lowercase includes used by game code:
#   #include <sdl3/SDL.h>, <sdl3/SDL_image.h>, ...
BUILD_INCLUDE_DIR="$PWD/.build/include/sdl3"
mkdir -p "$BUILD_INCLUDE_DIR"
for inc in \
  "$SDL3_ANDROID_ROOT/include/SDL3" \
  "$SDL3_IMAGE_ROOT/include/SDL3" \
  "$SDL3_IMAGE_ROOT/include/SDL3_image" \
  "$SDL3_TTF_ROOT/include/SDL3" \
  "$SDL3_TTF_ROOT/include/SDL3_ttf" \
  "$SDL3_MIXER_ROOT/include/SDL3" \
  "$SDL3_MIXER_ROOT/include/SDL3_mixer"; do
  [ -d "$inc" ] || continue
  for h in "$inc"/*.h; do
    [ -f "$h" ] || continue
    ln -sf "$h" "$BUILD_INCLUDE_DIR/$(basename "$h")"
  done
done
CPPFLAGS+=(
  -I"$PWD/.build/include"
  -I"$PWD/.build/include/sdl3"
)

SDL_VERSION_HEADER=""
for vh in \
  "$SDL3_ANDROID_ROOT/include/SDL3/SDL_version.h" \
  "$SDL3_IMAGE_ROOT/include/SDL3/SDL_version.h" \
  "$SDL3_TTF_ROOT/include/SDL3/SDL_version.h" \
  "$SDL3_MIXER_ROOT/include/SDL3/SDL_version.h"; do
  if [ -f "$vh" ]; then
    SDL_VERSION_HEADER="$vh"
    break
  fi
done

SDL_MAJOR=""
SDL_MINOR=""
SDL_PATCH=""
if [ -n "$SDL_VERSION_HEADER" ]; then
  SDL_MAJOR="$(awk '/#define[[:space:]]+SDL_MAJOR_VERSION[[:space:]]+[0-9]+/{print $3; exit}' "$SDL_VERSION_HEADER")"
  SDL_MINOR="$(awk '/#define[[:space:]]+SDL_MINOR_VERSION[[:space:]]+[0-9]+/{print $3; exit}' "$SDL_VERSION_HEADER")"
  SDL_PATCH="$(awk '/#define[[:space:]]+SDL_MICRO_VERSION[[:space:]]+[0-9]+/{print $3; exit}' "$SDL_VERSION_HEADER")"
  if [ -z "$SDL_PATCH" ]; then
    SDL_PATCH="$(awk '/#define[[:space:]]+SDL_PATCHLEVEL[[:space:]]+[0-9]+/{print $3; exit}' "$SDL_VERSION_HEADER")"
  fi
fi

if [ -z "$SDL_MAJOR" ] || [ -z "$SDL_MINOR" ] || [ -z "$SDL_PATCH" ]; then
  echo "[ERROR] Could not detect SDL version from active compile include path."
  if [ -n "$SDL_VERSION_HEADER" ]; then
    echo "[INFO] SDL version header probed: $SDL_VERSION_HEADER"
  fi
  echo "[INFO] SDL3_ANDROID_ROOT=$SDL3_ANDROID_ROOT"
  echo "[INFO] SDL3_IMAGE_ROOT=$SDL3_IMAGE_ROOT"
  echo "[INFO] SDL3_TTF_ROOT=$SDL3_TTF_ROOT"
  echo "[INFO] SDL3_MIXER_ROOT=$SDL3_MIXER_ROOT"
  exit 1
fi
SDL_STAGED_VERSION="${SDL_MAJOR}.${SDL_MINOR}.${SDL_PATCH}"
if ! ver_ge "$SDL_STAGED_VERSION" "$SDL_REQUIRED_VERSION"; then
  echo "[ERROR] SDL version too old in compile path: required >= $SDL_REQUIRED_VERSION, found $SDL_STAGED_VERSION"
  echo "[HINT] Rebuild Android SDL with:"
  echo "       SDL_REF=release-3.4.x FORCE_REBUILD_SDL=1 ./build/setup-android-sdl.sh"
  exit 1
fi
echo "[INFO] Using SDL in compile path: $SDL_STAGED_VERSION (required >= $SDL_REQUIRED_VERSION)"

LDFLAGS=(
  -L"$SDL3_ANDROID_ROOT/lib/$ABI"
  -L"$SDL3_IMAGE_ROOT/lib/$ABI"
  -L"$SDL3_TTF_ROOT/lib/$ABI"
  -L"$SDL3_MIXER_ROOT/lib/$ABI"
  -landroid
  -lOpenSLES
  -llog
  -lGLESv2
  -lEGL
  -Wl,--no-undefined
  -Wl,--gc-sections
)
if [ ! -d "$SDL3_MIXER_ROOT/lib/$ABI" ]; then
  echo "[ERROR] Missing staged SDL_mixer libs: $SDL3_MIXER_ROOT/lib/$ABI"
  echo "[HINT] Run: ./build/setup-android-sdl.sh"
  exit 1
fi

SDL_LINK_INPUTS=()
pick_sdl_lib() {
  local root="$1"
  local base="$2"
  local static_path="$root/lib/$ABI/lib${base}.a"
  if [ -f "$static_path" ]; then
    SDL_LINK_INPUTS+=("$static_path")
    return 0
  fi
  echo "[ERROR] Missing static SDL library: lib${base}.a under $root/lib/$ABI"
  echo "[HINT] Android build embeds SDL in libplatformer.so and does not use SDL shared .so at startup."
  return 1
}

pick_sdl_lib "$SDL3_ANDROID_ROOT" "SDL3"
pick_sdl_lib "$SDL3_IMAGE_ROOT" "SDL3_image"
pick_sdl_lib "$SDL3_TTF_ROOT" "SDL3_ttf"
pick_sdl_lib "$SDL3_MIXER_ROOT" "SDL3_mixer"

echo "[INFO] SDL link mode: embed/static only"

EXTRA_STATIC_INPUTS=()
declare -A _extra_seen=()
collect_extra_static_libs() {
  local root="$1"
  local libdir="$root/lib/$ABI"
  [ -d "$libdir" ] || return 0
  local f base
  while IFS= read -r -d '' f; do
    base="$(basename "$f")"
    case "$base" in
      libSDL3.a|libSDL3_image.a|libSDL3_ttf.a|libSDL3_mixer.a|libSDL3_test.a)
        continue
        ;;
    esac
    if [ -z "${_extra_seen[$base]+x}" ]; then
      _extra_seen["$base"]=1
      EXTRA_STATIC_INPUTS+=("$f")
    fi
  done < <(find "$libdir" -maxdepth 1 -type f -name 'lib*.a' -print0 | sort -z)
}

collect_extra_static_libs "$SDL3_ANDROID_ROOT"
collect_extra_static_libs "$SDL3_IMAGE_ROOT"
collect_extra_static_libs "$SDL3_TTF_ROOT"
collect_extra_static_libs "$SDL3_MIXER_ROOT"
if [ "${#EXTRA_STATIC_INPUTS[@]}" -gt 0 ]; then
  echo "[INFO] Extra static codec libs linked: ${#EXTRA_STATIC_INPUTS[@]}"
fi

CURL_ENABLED=0
if [ -f "$CURL_ANDROID_ROOT/include/curl/curl.h" ] && [ -f "$CURL_ANDROID_ROOT/lib/$ABI/libcurl.so" ]; then
  CPPFLAGS+=( -I"$CURL_ANDROID_ROOT/include" -DHAVE_CURL=1 )
  LDFLAGS+=( -L"$CURL_ANDROID_ROOT/lib/$ABI" -lcurl )
  CURL_ENABLED=1
  echo "[INFO] libcurl enabled for Android from: $CURL_ANDROID_ROOT"
else
  if [ "$ANDROID_ALLOW_NO_CURL" = "1" ]; then
    CPPFLAGS+=( -DHAVE_CURL=0 )
    echo "[WARN] Android libcurl not found at $CURL_ANDROID_ROOT (network fetch disabled in native code)"
    echo "[HINT] Provide:"
    echo "       $CURL_ANDROID_ROOT/include/curl/curl.h"
    echo "       $CURL_ANDROID_ROOT/lib/$ABI/libcurl.so"
  else
    echo "[ERROR] Android libcurl is required but missing."
    echo "[HINT] Provide:"
    echo "       $CURL_ANDROID_ROOT/include/curl/curl.h"
    echo "       $CURL_ANDROID_ROOT/lib/$ABI/libcurl.so"
    echo "[HINT] To bypass (not recommended): ANDROID_ALLOW_NO_CURL=1 ./build/android.sh"
    exit 1
  fi
fi

if [ "$FAST" = "1" ]; then
  CXXFLAGS=(
    -std=c++17
    -O1
    -DSDL_ENABLE_OLD_NAMES=1
    -fPIC
    -ffunction-sections
    -fdata-sections
  )
  echo "[INFO] FAST build enabled (reduced optimization, no LTO)"
else
  CXXFLAGS=(
    -std=c++17
    -O3
    -DNDEBUG
    -DSDL_ENABLE_OLD_NAMES=1
    -flto
    -fPIC
    -ffunction-sections
    -fdata-sections
  )
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
  src/World3PatternBackground.cpp
)

OUT_LIB="$OUT_DIR/libplatformer.so"
OBJ_DIR="$OUT_DIR/obj"
DEP_DIR="$OUT_DIR/dep"
FLAGS_STAMP="$OUT_DIR/.compile-flags"
mkdir -p "$OBJ_DIR" "$DEP_DIR"

current_flags_text="$(printf '%s\n' "$CXX" "${CXXFLAGS[@]}" "${CPPFLAGS[@]}")"
if [ ! -f "$FLAGS_STAMP" ] || ! printf '%s\n' "$current_flags_text" | cmp -s - "$FLAGS_STAMP"; then
  echo "[INFO] Compile flags changed; clearing cached objects for ABI=$ABI"
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
for src in "${SRC[@]}"; do
  rel="${src#src/}"
  obj="$OBJ_DIR/${rel%.cpp}.o"
  dep="$DEP_DIR/${rel%.cpp}.d"
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
    "$CXX" "${CXXFLAGS[@]}" "${CPPFLAGS[@]}" -MMD -MP -MF "$dep" -c "$src" -o "$obj"
  fi
done

echo "[INFO] Building Android ABI=$ABI API=$API"
"$CXX" -shared "${OBJECTS[@]}" \
  -Wl,--start-group "${SDL_LINK_INPUTS[@]}" "${EXTRA_STATIC_INPUTS[@]}" -Wl,--end-group \
  "${LDFLAGS[@]}" -o "$OUT_LIB"

if [ "$STRIP_ANDROID_SO" = "1" ]; then
  STRIP_TOOL="$TOOLCHAIN/bin/llvm-strip"
  if [ -x "$STRIP_TOOL" ]; then
    before_size="$(wc -c < "$OUT_LIB" | tr -d '[:space:]')"
    "$STRIP_TOOL" --strip-unneeded "$OUT_LIB"
    after_size="$(wc -c < "$OUT_LIB" | tr -d '[:space:]')"
    echo "[INFO] Stripped $(basename "$OUT_LIB"): ${before_size} -> ${after_size} bytes"
  else
    echo "[WARN] llvm-strip not found at $STRIP_TOOL; skipping strip"
  fi
fi

echo "[OK] Built: $OUT_LIB"
copy_if_exists() {
  local src="$1"
  local dst="$2"
  if [ -f "$src" ]; then
    cp -f "$src" "$dst"
    echo "[OK] Synced next to game: $(basename "$src")"
  fi
}
echo "[INFO] SDL is embedded in libplatformer.so; skipping SDL .so sync"
if [ "$CURL_ENABLED" = "1" ]; then
  copy_if_exists "$CURL_ANDROID_ROOT/lib/$ABI/libcurl.so" "$OUT_DIR/libcurl.so"
fi
echo "[NEXT] Sync into Android app: ABI=$ABI ./build/update-android-app.sh"
