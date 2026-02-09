#!/usr/bin/env bash
set -euo pipefail

# Avoid host include/library contamination during cross-compile.
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH INCLUDE LIBRARY_PATH LD_LIBRARY_PATH

# Android NDK cross-build script
#
# Required env vars:
#   ANDROID_NDK_HOME   -> path to Android NDK
#   SDL2_ANDROID_ROOT  -> root folder containing Android builds of SDL2 libs/includes
#
# Optional env vars:
#   API                -> Android API level (default: 24)
#   ABI                -> arm64-v8a | armeabi-v7a | x86_64 | x86 (default: arm64-v8a)
#   FAST               -> 1 for fast iteration build flags (default: 0)
#   SDL2_IMAGE_ROOT    -> root folder for SDL2_image Android build
#   SDL2_TTF_ROOT      -> root folder for SDL2_ttf Android build
#   SDL2_MIXER_ROOT    -> root folder for SDL2_mixer Android build
#
# Expected folder layout per *_ROOT:
#   include/
#   lib/<abi>/
#
# Output:
#   build/android/<abi>/libplatformer.so

if [ -f "build/android.env" ]; then
  # shellcheck disable=SC1091
  . "build/android.env"
fi

FORCE_STAGED_SDL_ROOT="${FORCE_STAGED_SDL_ROOT:-1}"
if [ "$FORCE_STAGED_SDL_ROOT" = "1" ]; then
  SDL2_ANDROID_ROOT="$PWD/deps/android"
  SDL2_IMAGE_ROOT="$SDL2_ANDROID_ROOT"
  SDL2_TTF_ROOT="$SDL2_ANDROID_ROOT"
  SDL2_MIXER_ROOT="$SDL2_ANDROID_ROOT"
  export SDL2_ANDROID_ROOT SDL2_IMAGE_ROOT SDL2_TTF_ROOT SDL2_MIXER_ROOT
  echo "[INFO] FORCE_STAGED_SDL_ROOT=1 -> using staged SDL root: $SDL2_ANDROID_ROOT"
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

if [ -z "${SDL2_ANDROID_ROOT:-}" ]; then
  for c in \
    "$PWD/deps/android" \
    "$PWD/deps/android-sdl" \
    "$PWD/deps/sdl-android" \
    "$PWD/deps/SDL-android" \
    "$HOME/Android/SDL2-android" \
    "$HOME/Android/SDL2" \
    "$HOME/SDL2-android" \
    "$HOME/Android/sdl2"; do
    if [ -d "$c/include" ] && [ -d "$c/lib" ] && \
       { [ -d "$c/lib/arm64-v8a" ] || [ -d "$c/lib/armeabi-v7a" ] || [ -d "$c/lib/x86_64" ] || [ -d "$c/lib/x86" ]; }; then
      SDL2_ANDROID_ROOT="$c"
      export SDL2_ANDROID_ROOT
      break
    fi
  done
fi

: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME}"
if [ -z "${SDL2_ANDROID_ROOT:-}" ]; then
  echo "[ERROR] SDL2_ANDROID_ROOT not found."
  echo "[HINT] Expected layout:"
  echo "       <root>/include"
  echo "       <root>/lib/<abi>"
  echo "[HINT] Set it explicitly, for example:"
  echo "       SDL2_ANDROID_ROOT=/path/to/sdl-android ./build/android.sh"
  echo "[INFO] Checked candidates:"
  printf '  - %s\n' \
    "$PWD/deps/android" \
    "$PWD/deps/android-sdl" \
    "$PWD/deps/sdl-android" \
    "$PWD/deps/SDL-android" \
    "$HOME/Android/SDL2-android" \
    "$HOME/Android/SDL2" \
    "$HOME/SDL2-android" \
    "$HOME/Android/sdl2"
  exit 1
fi

if [ ! -f "$SDL2_ANDROID_ROOT/include/SDL2/SDL.h" ]; then
  echo "[ERROR] Missing staged SDL headers: $SDL2_ANDROID_ROOT/include/SDL2/SDL.h"
  echo "[HINT] Run: ./build/setup-android-sdl.sh"
  exit 1
fi
SDL2_IMAGE_ROOT="${SDL2_IMAGE_ROOT:-$SDL2_ANDROID_ROOT}"
SDL2_TTF_ROOT="${SDL2_TTF_ROOT:-$SDL2_ANDROID_ROOT}"
SDL2_MIXER_ROOT="${SDL2_MIXER_ROOT:-$SDL2_ANDROID_ROOT}"

if [ ! -f "$SDL2_IMAGE_ROOT/include/SDL2/SDL_image.h" ]; then
  echo "[ERROR] Missing staged SDL_image headers: $SDL2_IMAGE_ROOT/include/SDL2/SDL_image.h"
  echo "[HINT] Run: ./build/setup-android-sdl.sh"
  exit 1
fi
if [ ! -f "$SDL2_TTF_ROOT/include/SDL2/SDL_ttf.h" ]; then
  echo "[ERROR] Missing staged SDL_ttf headers: $SDL2_TTF_ROOT/include/SDL2/SDL_ttf.h"
  echo "[HINT] Run: ./build/setup-android-sdl.sh"
  exit 1
fi
if [ ! -f "$SDL2_MIXER_ROOT/include/SDL2/SDL_mixer.h" ]; then
  echo "[ERROR] Missing staged SDL_mixer headers: $SDL2_MIXER_ROOT/include/SDL2/SDL_mixer.h"
  echo "[HINT] Run: ./build/setup-android-sdl.sh"
  exit 1
fi

API="${API:-24}"
ABI="${ABI:-arm64-v8a}"
FAST="${FAST:-0}"
SDL_REQUIRED_VERSION="${SDL_REQUIRED_VERSION:-2.32.11}"
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
  "$OUT_DIR/libSDL2.so" \
  "$OUT_DIR/libSDL2_image.so" \
  "$OUT_DIR/libSDL2_ttf.so" \
  "$OUT_DIR/libSDL2_mixer.so"

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
  -I"$SDL2_ANDROID_ROOT/include"
  -I"$SDL2_IMAGE_ROOT/include"
  -I"$SDL2_TTF_ROOT/include"
  -I"$SDL2_MIXER_ROOT/include"
)

SDL_VERSION_PROBE='
#include <SDL2/SDL.h>
SDL_MAJOR_VERSION SDL_MINOR_VERSION SDL_PATCHLEVEL
'
SDL_VERSION_LINE="$("$CXX" -E -P -x c++ - "${CPPFLAGS[@]}" <<<"$SDL_VERSION_PROBE" 2>/dev/null | tail -n1 || true)"
SDL_MAJOR="$(awk '{print $1}' <<<"$SDL_VERSION_LINE")"
SDL_MINOR="$(awk '{print $2}' <<<"$SDL_VERSION_LINE")"
SDL_PATCH="$(awk '{print $3}' <<<"$SDL_VERSION_LINE")"
if [ -z "$SDL_MAJOR" ] || [ -z "$SDL_MINOR" ] || [ -z "$SDL_PATCH" ]; then
  echo "[ERROR] Could not detect SDL version from active compile include path."
  echo "[INFO] SDL2_ANDROID_ROOT=$SDL2_ANDROID_ROOT"
  echo "[INFO] SDL2_IMAGE_ROOT=$SDL2_IMAGE_ROOT"
  echo "[INFO] SDL2_TTF_ROOT=$SDL2_TTF_ROOT"
  echo "[INFO] SDL2_MIXER_ROOT=$SDL2_MIXER_ROOT"
  exit 1
fi
SDL_STAGED_VERSION="${SDL_MAJOR}.${SDL_MINOR}.${SDL_PATCH}"
if ! ver_ge "$SDL_STAGED_VERSION" "$SDL_REQUIRED_VERSION"; then
  echo "[ERROR] SDL version too old in compile path: required >= $SDL_REQUIRED_VERSION, found $SDL_STAGED_VERSION"
  echo "[HINT] Rebuild Android SDL with:"
  echo "       SDL_REF=release-2.32.x FORCE_REBUILD_SDL=1 ./build/setup-android-sdl.sh"
  exit 1
fi
echo "[INFO] Using SDL in compile path: $SDL_STAGED_VERSION (required >= $SDL_REQUIRED_VERSION)"

LDFLAGS=(
  -L"$SDL2_ANDROID_ROOT/lib/$ABI"
  -L"$SDL2_IMAGE_ROOT/lib/$ABI"
  -L"$SDL2_TTF_ROOT/lib/$ABI"
  -lSDL2
  -lSDL2_image
  -lSDL2_ttf
  -lSDL2_mixer
  -landroid
  -llog
  -lGLESv2
  -lEGL
  -Wl,--no-undefined
)
if [ ! -d "$SDL2_MIXER_ROOT/lib/$ABI" ]; then
  echo "[ERROR] Missing staged SDL_mixer libs: $SDL2_MIXER_ROOT/lib/$ABI"
  echo "[HINT] Run: ./build/setup-android-sdl.sh"
  exit 1
fi
LDFLAGS+=( -L"$SDL2_MIXER_ROOT/lib/$ABI" )
echo "[INFO] SDL2_mixer linked"

if [ "$FAST" = "1" ]; then
  CXXFLAGS=(
    -std=c++17
    -O1
    -fPIC
  )
  echo "[INFO] FAST build enabled (reduced optimization, no LTO)"
else
  CXXFLAGS=(
    -std=c++17
    -O3
    -DNDEBUG
    -flto
    -fPIC
  )
fi

SRC=(
  src/main.cpp
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
)

OUT_LIB="$OUT_DIR/libplatformer.so"

echo "[INFO] Building Android ABI=$ABI API=$API"
"$CXX" "${CXXFLAGS[@]}" "${CPPFLAGS[@]}" "${SRC[@]}" -shared "${LDFLAGS[@]}" -o "$OUT_LIB"

echo "[OK] Built: $OUT_LIB"
copy_if_exists() {
  local src="$1"
  local dst="$2"
  if [ -f "$src" ]; then
    cp -f "$src" "$dst"
    echo "[OK] Synced next to game: $(basename "$src")"
  fi
}
copy_if_exists "$SDL2_ANDROID_ROOT/lib/$ABI/libSDL2.so" "$OUT_DIR/libSDL2.so"
copy_if_exists "$SDL2_IMAGE_ROOT/lib/$ABI/libSDL2_image.so" "$OUT_DIR/libSDL2_image.so"
copy_if_exists "$SDL2_TTF_ROOT/lib/$ABI/libSDL2_ttf.so" "$OUT_DIR/libSDL2_ttf.so"
copy_if_exists "$SDL2_MIXER_ROOT/lib/$ABI/libSDL2_mixer.so" "$OUT_DIR/libSDL2_mixer.so"
echo "[NEXT] Sync into Android app: ABI=$ABI ./build/update-android-app.sh"
