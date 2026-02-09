#!/usr/bin/env bash
set -euo pipefail

# Prevent silent hangs on network operations.
export GIT_TERMINAL_PROMPT=0
NETWORK_TIMEOUT_SECONDS="${NETWORK_TIMEOUT_SECONDS:-900}"

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
FORCE_REBUILD_SDL="${FORCE_REBUILD_SDL:-0}"

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

if [ "$FORCE_REBUILD_SDL" = "1" ]; then
  echo "[INFO] FORCE_REBUILD_SDL=1 -> cleaning Android SDL build/install/stage for ABI=$ABI"
  rm -rf "$BUILD_ROOT/SDL" "$BUILD_ROOT/SDL_image" "$BUILD_ROOT/SDL_ttf" "$BUILD_ROOT/SDL_mixer"
  rm -rf "$INSTALL_ROOT/SDL" "$INSTALL_ROOT/SDL_image" "$INSTALL_ROOT/SDL_ttf" "$INSTALL_ROOT/SDL_mixer"
  rm -rf "$STAGE_ROOT/include" "$STAGE_ROOT/lib/$ABI"
  mkdir -p "$STAGE_ROOT/lib/$ABI"
fi

run_with_timeout() {
  if command -v timeout >/dev/null 2>&1; then
    timeout "$NETWORK_TIMEOUT_SECONDS" "$@"
  else
    "$@"
  fi
}

clone_or_update() {
  local name="$1"
  local url="$2"
  local ref="$3"
  shift 3
  local fallbacks=("$@")
  local dir="$SRC_ROOT/$name"
  local chosen_ref="$ref"
  local remote_ref=""

  resolve_remote_ref() {
    local candidate="$1"
    if run_with_timeout git ls-remote --exit-code --heads "$url" "$candidate" >/dev/null 2>&1; then
      echo "refs/heads/$candidate"
      return 0
    fi
    if run_with_timeout git ls-remote --exit-code --tags "$url" "$candidate" >/dev/null 2>&1; then
      echo "refs/tags/$candidate"
      return 0
    fi
    return 1
  }

  if remote_ref="$(resolve_remote_ref "$chosen_ref")"; then
    :
  else
    for alt in "${fallbacks[@]}"; do
      if remote_ref="$(resolve_remote_ref "$alt")"; then
        echo "[WARN] $name ref '$chosen_ref' not found, using '$alt'"
        chosen_ref="$alt"
        break
      fi
    done
  fi

  if [ -z "$remote_ref" ]; then
    echo "[ERROR] Could not find a valid ref for $name"
    echo "        Tried: $ref ${fallbacks[*]}"
    exit 1
  fi

  if [ ! -d "$dir/.git" ]; then
    echo "[INFO] Cloning $name ($chosen_ref)"
    run_with_timeout git clone --depth 1 --branch "$chosen_ref" "$url" "$dir"
  else
    echo "[INFO] Updating $name ($chosen_ref)"
    run_with_timeout git -C "$dir" fetch --depth 1 origin "$chosen_ref" || run_with_timeout git -C "$dir" fetch --depth 1 origin "$remote_ref"
    git -C "$dir" checkout -f "$chosen_ref" || git -C "$dir" checkout -f "origin/$chosen_ref" || git -C "$dir" checkout -f FETCH_HEAD
    git -C "$dir" reset --hard "origin/$chosen_ref" || git -C "$dir" reset --hard FETCH_HEAD || true
  fi
}

sync_submodules() {
  local dir="$1"
  if [ -d "$dir/.git" ]; then
    echo "[INFO] Syncing submodules in $(basename "$dir")"
    git -C "$dir" submodule sync --recursive
    run_with_timeout git -C "$dir" submodule update --init --recursive --depth 1 --jobs "$JOBS" --progress
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

  echo "[INFO] Building $(basename "$src")..."
  cmake --build "$bld" -j"$JOBS"
  echo "[INFO] Installing $(basename "$src")..."
  cmake --install "$bld"
}

SDL_REF="${SDL_REF:-release-2.32.x}"
SDL_IMAGE_REF="${SDL_IMAGE_REF:-release-2.8.x}"
SDL_TTF_REF="${SDL_TTF_REF:-release-2.24.x}"
SDL_MIXER_REF="${SDL_MIXER_REF:-release-2.8.x}"

clone_or_update "SDL" "https://github.com/libsdl-org/SDL.git" "$SDL_REF" "release-2.32.x" "release-2.32" "release-2.30.x" "main"
clone_or_update "SDL_image" "https://github.com/libsdl-org/SDL_image.git" "$SDL_IMAGE_REF" "release-2.8" "main"
clone_or_update "SDL_ttf" "https://github.com/libsdl-org/SDL_ttf.git" "$SDL_TTF_REF" "release-2.24" "main"
clone_or_update "SDL_mixer" "https://github.com/libsdl-org/SDL_mixer.git" "$SDL_MIXER_REF" "release-2.8" "main"
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
if [ -f "$STAGE_ROOT/include/SDL2/SDL_version.h" ]; then
  SDL_MAJOR="$(awk '/#define[[:space:]]+SDL_MAJOR_VERSION[[:space:]]+[0-9]+/{print $3; exit}' "$STAGE_ROOT/include/SDL2/SDL_version.h")"
  SDL_MINOR="$(awk '/#define[[:space:]]+SDL_MINOR_VERSION[[:space:]]+[0-9]+/{print $3; exit}' "$STAGE_ROOT/include/SDL2/SDL_version.h")"
  SDL_PATCH="$(awk '/#define[[:space:]]+SDL_MICRO_VERSION[[:space:]]+[0-9]+/{print $3; exit}' "$STAGE_ROOT/include/SDL2/SDL_version.h")"
  if [ -z "$SDL_PATCH" ]; then
    SDL_PATCH="$(awk '/#define[[:space:]]+SDL_PATCHLEVEL[[:space:]]+[0-9]+/{print $3; exit}' "$STAGE_ROOT/include/SDL2/SDL_version.h")"
  fi
  echo "[INFO] Staged SDL header version: ${SDL_MAJOR:-0}.${SDL_MINOR:-0}.${SDL_PATCH:-0}"
fi
cat > "$ROOT_DIR/build/android.env" <<ENV
export ANDROID_NDK_HOME="$ANDROID_NDK_HOME"
export SDL2_ANDROID_ROOT="$STAGE_ROOT"
export SDL2_IMAGE_ROOT="$STAGE_ROOT"
export SDL2_TTF_ROOT="$STAGE_ROOT"
export SDL2_MIXER_ROOT="$STAGE_ROOT"
ENV

echo "[OK] Wrote: $ROOT_DIR/build/android.env"
echo "[NEXT] source build/android.env && ./build/android.sh"
