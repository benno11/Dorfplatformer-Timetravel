#!/usr/bin/env bash
set -euo pipefail

# Build and stage SDL2, SDL2_image, SDL2_ttf, SDL2_mixer for Android into deps/android
#
# Usage:
#   ./build/setup-android-sdl.sh
#   ABI=arm64-v8a API=24 ./build/setup-android-sdl.sh
#
# Required:
#   ANDROID_NDK_HOME
#
# Output layout (used by build/android.sh):
#   deps/android/include/SDL2/...
#   deps/android/lib/<abi>/libSDL2.so
#   deps/android/lib/<abi>/libSDL2_image.so
#   deps/android/lib/<abi>/libSDL2_ttf.so

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# Preserve ANDROID_NDK_HOME if already set
SAVED_ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-}"

if [ -f "$ROOT_DIR/build/android.env" ]; then
  # shellcheck disable=SC1091
  . "$ROOT_DIR/build/android.env"
fi

# Restore ANDROID_NDK_HOME if it was set before sourcing android.env
if [ -n "$SAVED_ANDROID_NDK_HOME" ]; then
  ANDROID_NDK_HOME="$SAVED_ANDROID_NDK_HOME"
  export ANDROID_NDK_HOME
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

ABI="${ABI:-arm64-v8a}"
API="${API:-24}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

case "$ABI" in
  arm64-v8a|armeabi-v7a|x86_64|x86) ;;
  *)
    echo "[ERROR] Unsupported ABI: $ABI"
    exit 1
    ;;
esac

if [ -z "${ANDROID_NDK_HOME:-}" ]; then
  echo "[ERROR] ANDROID_NDK_HOME is not set. Please set it to the path of your Android NDK installation."
  exit 1
fi

TOOLCHAIN="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
if [ ! -f "$TOOLCHAIN" ]; then
  echo "[ERROR] Missing NDK toolchain file: $TOOLCHAIN"
  echo "[ERROR] ANDROID_NDK_HOME is set to: $ANDROID_NDK_HOME"
  echo "[ERROR] Please ensure ANDROID_NDK_HOME points to a valid Android NDK installation."
  exit 1
fi

SRC_ROOT="$ROOT_DIR/deps/android-src"
BUILD_ROOT="$ROOT_DIR/deps/android-build/$ABI"
INSTALL_ROOT="$ROOT_DIR/deps/android-install/$ABI"
STAGE_ROOT="$ROOT_DIR/deps/android"

mkdir -p "$SRC_ROOT" "$BUILD_ROOT" "$INSTALL_ROOT" "$STAGE_ROOT/lib/$ABI"

clone_or_update() {
  local name="$1"
  local url="$2"
  local ref="$3"
  local dir="$SRC_ROOT/$name"
  if [ ! -d "$dir/.git" ]; then
    echo "[INFO] Cloning $name ($ref)"
    git clone --depth 1 --branch "$ref" "$url" "$dir"
  else
    echo "[INFO] Updating $name ($ref)"
    git -C "$dir" fetch --depth 1 origin "$ref"
    git -C "$dir" checkout -f "$ref" || git -C "$dir" checkout -f "origin/$ref"
    git -C "$dir" reset --hard "origin/$ref" || true
  fi
}

sync_submodules() {
  local dir="$1"
  if [ -d "$dir/.git" ]; then
    echo "[INFO] Syncing submodules in $(basename "$dir")"
    git -C "$dir" submodule sync --recursive
    git -C "$dir" submodule update --init --recursive --depth 1
  fi
}

cmake_android() {
  local src="$1"
  local bld="$2"
  local prefix="$3"
  shift 3

  cmake -S "$src" -B "$bld" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM="android-$API" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    "$@"

  cmake --build "$bld" -j"$JOBS"
  cmake --install "$bld"
}

clone_or_update "SDL" "https://github.com/libsdl-org/SDL.git" "release-2.32.x"
clone_or_update "SDL_image" "https://github.com/libsdl-org/SDL_image.git" "release-2.8.x"
clone_or_update "SDL_ttf" "https://github.com/libsdl-org/SDL_ttf.git" "release-2.24.x"
clone_or_update "SDL_mixer" "https://github.com/libsdl-org/SDL_mixer.git" "release-2.8.x"
sync_submodules "$SRC_ROOT/SDL_image"
sync_submodules "$SRC_ROOT/SDL_ttf"
sync_submodules "$SRC_ROOT/SDL_mixer"

echo "[INFO] Building SDL2"
cmake_android \
  "$SRC_ROOT/SDL" \
  "$BUILD_ROOT/SDL" \
  "$INSTALL_ROOT/SDL" \
  -DSDL_SHARED=ON \
  -DSDL_STATIC=OFF \
  -DSDL_TESTS=OFF

SDL2_CMAKE_DIR="$INSTALL_ROOT/SDL/lib/cmake/SDL2"

echo "[INFO] Building SDL2_image"
cmake_android \
  "$SRC_ROOT/SDL_image" \
  "$BUILD_ROOT/SDL_image" \
  "$INSTALL_ROOT/SDL_image" \
  -DSDL2IMAGE_SAMPLES=OFF \
  -DSDL2IMAGE_VENDORED=ON \
  -DSDL2_DIR="$SDL2_CMAKE_DIR"

echo "[INFO] Building SDL2_ttf"
cmake_android \
  "$SRC_ROOT/SDL_ttf" \
  "$BUILD_ROOT/SDL_ttf" \
  "$INSTALL_ROOT/SDL_ttf" \
  -DSDL2TTF_SAMPLES=OFF \
  -DSDL2TTF_VENDORED=ON \
  -DSDL2TTF_HARFBUZZ=OFF \
  -DSDL2_DIR="$SDL2_CMAKE_DIR"

echo "[INFO] Building SDL2_mixer"
cmake_android \
  "$SRC_ROOT/SDL_mixer" \
  "$BUILD_ROOT/SDL_mixer" \
  "$INSTALL_ROOT/SDL_mixer" \
  -DSDL2MIXER_SAMPLES=OFF \
  -DSDL2MIXER_VENDORED=ON \
  -DSDL2MIXER_OPUS=ON \
  -DSDL2MIXER_MOD=OFF \
  -DSDL2MIXER_MIDI=OFF \
  -DSDL2_DIR="$SDL2_CMAKE_DIR"

# Stage include and libs in a unified root.
rm -rf "$STAGE_ROOT/include"
mkdir -p "$STAGE_ROOT/include/SDL2" "$STAGE_ROOT/lib/$ABI"

copy_headers() {
  local from="$1"
  if [ -d "$from/include/SDL2" ]; then
    cp -a "$from/include/SDL2/." "$STAGE_ROOT/include/SDL2/"
  elif [ -d "$from/include" ]; then
    # Some packages may stage headers directly in include/
    find "$from/include" -maxdepth 1 -type f -name '*.h' -exec cp -a {} "$STAGE_ROOT/include/SDL2/" \;
  fi
}

copy_headers "$INSTALL_ROOT/SDL"
copy_headers "$INSTALL_ROOT/SDL_image"
copy_headers "$INSTALL_ROOT/SDL_ttf"
copy_headers "$INSTALL_ROOT/SDL_mixer"

copy_libs() {
  local from="$1"
  if [ -d "$from" ]; then
    find "$from" -maxdepth 1 -type f \( -name 'libSDL*.so' -o -name 'libSDL*.a' \) -exec cp -a {} "$STAGE_ROOT/lib/$ABI/" \;
  fi
}

copy_libs "$INSTALL_ROOT/SDL/lib"
copy_libs "$INSTALL_ROOT/SDL_image/lib"
copy_libs "$INSTALL_ROOT/SDL_ttf/lib"
copy_libs "$INSTALL_ROOT/SDL_mixer/lib"

if [ ! -f "$STAGE_ROOT/lib/$ABI/libSDL2.so" ]; then
  echo "[WARN] Could not find libSDL2.so in staged output."
fi

echo "[OK] Staged Android SDL deps at: $STAGE_ROOT"
cat > "$ROOT_DIR/build/android.env" <<ENV
export ANDROID_NDK_HOME="$ANDROID_NDK_HOME"
export SDL2_ANDROID_ROOT="$STAGE_ROOT"
export SDL2_IMAGE_ROOT="$STAGE_ROOT"
export SDL2_TTF_ROOT="$STAGE_ROOT"
export SDL2_MIXER_ROOT="$STAGE_ROOT"
ENV

echo "[OK] Wrote: $ROOT_DIR/build/android.env"
echo "[NEXT] source build/android.env && ./build/android.sh"
