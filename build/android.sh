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

CXXFLAGS=(
  -std=c++17
  -O3
  -DNDEBUG
  -flto
  -fPIC
)

SRC=(
  src/main.cpp
  src/TileMap.cpp
  src/AssetPath.cpp
  src/LevelLoader.cpp
  src/TextRenderer.cpp
  src/LevelSelect.cpp
  src/PlayerController.cpp
  src/LevelManager.cpp
)

OUT_LIB="$OUT_DIR/libplatformer.so"

echo "[INFO] Building Android ABI=$ABI API=$API"
"$CXX" "${CXXFLAGS[@]}" "${CPPFLAGS[@]}" "${SRC[@]}" -shared "${LDFLAGS[@]}" -o "$OUT_LIB"

echo "[OK] Built: $OUT_LIB"
echo "[NEXT] Sync into Android app: ABI=$ABI ./build/update-android-app.sh"
