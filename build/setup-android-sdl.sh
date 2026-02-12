#!/usr/bin/env bash
set -euo pipefail

# Prevent silent hangs on network operations.
export GIT_TERMINAL_PROMPT=0
NETWORK_TIMEOUT_SECONDS="${NETWORK_TIMEOUT_SECONDS:-180}"

# Build and stage SDL3, SDL3_image, SDL3_ttf, SDL3_mixer for Android into deps/android
#
# Usage:
#   ./build/setup-android-sdl.sh
#   ABI=arm64-v8a API=24 ./build/setup-android-sdl.sh
#
# Required:
#   ANDROID_NDK_HOME
#
# Output layout (used by build/android.sh):
#   deps/android/include/SDL3/...
#   deps/android/lib/<abi>/libSDL3.a
#   deps/android/lib/<abi>/libSDL3_image.a
#   deps/android/lib/<abi>/libSDL3_ttf.a
#   deps/android/lib/<abi>/libSDL3_mixer.a

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

# Always reset per-component Android build/install dirs before configure to
# avoid stale CMake exports (e.g. SDL3sharedTargets.cmake from older shared builds).
reset_component_dirs() {
  local name="$1"
  rm -rf "$BUILD_ROOT/$name" "$INSTALL_ROOT/$name"
}

run_with_timeout() {
  if command -v timeout >/dev/null 2>&1; then
    timeout "$NETWORK_TIMEOUT_SECONDS" "$@"
  else
    # Portable fallback when coreutils timeout is unavailable.
    "$@" &
    local cmd_pid=$!
    local waited=0
    while kill -0 "$cmd_pid" 2>/dev/null; do
      if [ "$waited" -ge "$NETWORK_TIMEOUT_SECONDS" ]; then
        kill "$cmd_pid" 2>/dev/null || true
        sleep 1
        kill -9 "$cmd_pid" 2>/dev/null || true
        wait "$cmd_pid" 2>/dev/null || true
        echo "[ERROR] Command timed out after ${NETWORK_TIMEOUT_SECONDS}s: $*" >&2
        return 124
      fi
      sleep 1
      waited=$((waited + 1))
    done
    wait "$cmd_pid"
  fi
}

clone_or_update() {
  local name="$1"
  local url="$2"
  local ref="$3"
  shift 3
  local fallbacks=("$@")
  local dir="$SRC_ROOT/$name"
  local candidates=("$ref" "${fallbacks[@]}")
  local chosen_ref=""
  local candidate=""
  local ok=0
  local did_fetch=0

  echo "[INFO] Resolving ref for $name (requested: $ref)"

  if [ ! -d "$dir/.git" ]; then
    for candidate in "${candidates[@]}"; do
      [ -n "$candidate" ] || continue
      echo "[INFO] Cloning $name ($candidate)"
      rm -rf "$dir"
      if run_with_timeout git clone --depth 1 --branch "$candidate" "$url" "$dir"; then
        chosen_ref="$candidate"
        ok=1
        break
      fi
      echo "[WARN] Failed to clone $name with ref '$candidate'"
    done
  else
    for candidate in "${candidates[@]}"; do
      [ -n "$candidate" ] || continue
      echo "[INFO] Updating $name ($candidate)"
      if run_with_timeout git -C "$dir" fetch --depth 1 origin "$candidate"; then
        chosen_ref="$candidate"
        ok=1
        did_fetch=1
        break
      fi
      echo "[WARN] Failed to update $name with ref '$candidate'"
    done
  fi

  if [ "$ok" -ne 1 ]; then
    echo "[ERROR] Could not find a valid ref for $name"
    echo "        Tried: $ref ${fallbacks[*]}"
    exit 1
  fi

  if [ -d "$dir/.git" ]; then
    if [ "$did_fetch" -eq 1 ] && git -C "$dir" rev-parse --verify -q FETCH_HEAD >/dev/null; then
      git -C "$dir" checkout -f FETCH_HEAD
      git -C "$dir" reset --hard FETCH_HEAD
    elif git -C "$dir" rev-parse --verify -q "origin/$chosen_ref" >/dev/null; then
      git -C "$dir" checkout -f "$chosen_ref" || git -C "$dir" checkout -f "origin/$chosen_ref"
      git -C "$dir" reset --hard "origin/$chosen_ref"
    else
      # Fresh clone path: --branch already checked out; keep repository in clean state.
      git -C "$dir" reset --hard HEAD
    fi
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

SDL_REF="${SDL_REF:-release-3.4.x}"
SDL_IMAGE_REF="${SDL_IMAGE_REF:-main}"
SDL_TTF_REF="${SDL_TTF_REF:-main}"
SDL_MIXER_REF="${SDL_MIXER_REF:-main}"

clone_or_update "SDL" "https://github.com/libsdl-org/SDL.git" "$SDL_REF" "release-3.4.x" "release-3.4" "main"
clone_or_update "SDL_image" "https://github.com/libsdl-org/SDL_image.git" "$SDL_IMAGE_REF" "main"
clone_or_update "SDL_ttf" "https://github.com/libsdl-org/SDL_ttf.git" "$SDL_TTF_REF" "main"
clone_or_update "SDL_mixer" "https://github.com/libsdl-org/SDL_mixer.git" "$SDL_MIXER_REF" "main"
sync_submodules "$SRC_ROOT/SDL_image"
sync_submodules "$SRC_ROOT/SDL_ttf"
sync_submodules "$SRC_ROOT/SDL_mixer"

if [ ! -f "$SRC_ROOT/SDL/include/SDL3/SDL.h" ]; then
  echo "[ERROR] SDL source checkout is not SDL3. Ref '$SDL_REF' resolved to an incompatible layout."
  echo "[HINT] Use an SDL3 ref such as: release-3.4.x"
  exit 1
fi

echo "[INFO] Building SDL3"
reset_component_dirs "SDL"
cmake_android \
  "$SRC_ROOT/SDL" \
  "$BUILD_ROOT/SDL" \
  "$INSTALL_ROOT/SDL" \
  -DSDL_SHARED=OFF \
  -DSDL_STATIC=ON \
  -DSDL_TESTS=OFF

SDL3_CMAKE_DIR="$INSTALL_ROOT/SDL/lib/cmake/SDL3"

echo "[INFO] Building SDL3_image"
reset_component_dirs "SDL_image"
cmake_android \
  "$SRC_ROOT/SDL_image" \
  "$BUILD_ROOT/SDL_image" \
  "$INSTALL_ROOT/SDL_image" \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF \
  -DSDLIMAGE_SAMPLES=OFF \
  -DSDLIMAGE_DEPS_SHARED=OFF \
  -DSDLIMAGE_WEBP=OFF \
  -DSDLIMAGE_VENDORED=ON \
  -DSDL3_DIR="$SDL3_CMAKE_DIR"

echo "[INFO] Building SDL3_ttf"
reset_component_dirs "SDL_ttf"
cmake_android \
  "$SRC_ROOT/SDL_ttf" \
  "$BUILD_ROOT/SDL_ttf" \
  "$INSTALL_ROOT/SDL_ttf" \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF \
  -DSDLTTF_SAMPLES=OFF \
  -DSDLTTF_VENDORED=ON \
  -DSDLTTF_HARFBUZZ=OFF \
  -DSDL3_DIR="$SDL3_CMAKE_DIR"

echo "[INFO] Building SDL3_mixer"
reset_component_dirs "SDL_mixer"
cmake_android \
  "$SRC_ROOT/SDL_mixer" \
  "$BUILD_ROOT/SDL_mixer" \
  "$INSTALL_ROOT/SDL_mixer" \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF \
  -DSDLMIXER_TESTS=OFF \
  -DSDLMIXER_EXAMPLES=OFF \
  -DSDLMIXER_DEPS_SHARED=OFF \
  -DSDLMIXER_VENDORED=ON \
  -DSDLMIXER_OPUS=OFF \
  -DSDLMIXER_MP3=ON \
  -DSDLMIXER_MP3_DRMP3=ON \
  -DSDLMIXER_MP3_MPG123=OFF \
  -DSDLMIXER_FLAC=OFF \
  -DSDLMIXER_VORBIS=OFF \
  -DSDLMIXER_WAVPACK=OFF \
  -DSDLMIXER_GME=OFF \
  -DSDLMIXER_MOD=OFF \
  -DSDLMIXER_MIDI=OFF \
  -DSDL3_DIR="$SDL3_CMAKE_DIR"

# Stage include and libs in a unified root.
rm -rf "$STAGE_ROOT/include"
mkdir -p "$STAGE_ROOT/include/SDL3" "$STAGE_ROOT/lib/$ABI"

copy_headers() {
  local from="$1"
  if [ -d "$from/include/SDL3" ]; then
    cp -a "$from/include/SDL3/." "$STAGE_ROOT/include/SDL3/"
  fi
  # Keep a flattened SDL3 include layout expected by existing build scripts:
  # include/SDL3/SDL_image.h, include/SDL3/SDL_ttf.h, include/SDL3/SDL_mixer.h.
  if [ -d "$from/include" ]; then
    find "$from/include" -mindepth 1 -maxdepth 2 -type f -name '*.h' -exec cp -a {} "$STAGE_ROOT/include/SDL3/" \;
  fi
}

copy_headers "$INSTALL_ROOT/SDL"
copy_headers "$INSTALL_ROOT/SDL_image"
copy_headers "$INSTALL_ROOT/SDL_ttf"
copy_headers "$INSTALL_ROOT/SDL_mixer"

copy_libs() {
  local from="$1"
  if [ -d "$from" ]; then
    find "$from" -maxdepth 1 -type f \( -name 'lib*.a' -o -name 'libSDL*.so' \) -exec cp -a {} "$STAGE_ROOT/lib/$ABI/" \;
  fi
}

copy_libs "$INSTALL_ROOT/SDL/lib"
copy_libs "$INSTALL_ROOT/SDL_image/lib"
copy_libs "$INSTALL_ROOT/SDL_ttf/lib"
copy_libs "$INSTALL_ROOT/SDL_mixer/lib"

if [ ! -f "$STAGE_ROOT/lib/$ABI/libSDL3.a" ]; then
  echo "[WARN] Could not find libSDL3.a in staged output."
fi

echo "[OK] Staged Android SDL deps at: $STAGE_ROOT"
if [ -f "$STAGE_ROOT/include/SDL3/SDL_version.h" ]; then
  SDL_MAJOR="$(awk '/#define[[:space:]]+SDL_MAJOR_VERSION[[:space:]]+[0-9]+/{print $3; exit}' "$STAGE_ROOT/include/SDL3/SDL_version.h")"
  SDL_MINOR="$(awk '/#define[[:space:]]+SDL_MINOR_VERSION[[:space:]]+[0-9]+/{print $3; exit}' "$STAGE_ROOT/include/SDL3/SDL_version.h")"
  SDL_PATCH="$(awk '/#define[[:space:]]+SDL_MICRO_VERSION[[:space:]]+[0-9]+/{print $3; exit}' "$STAGE_ROOT/include/SDL3/SDL_version.h")"
  if [ -z "$SDL_PATCH" ]; then
    SDL_PATCH="$(awk '/#define[[:space:]]+SDL_PATCHLEVEL[[:space:]]+[0-9]+/{print $3; exit}' "$STAGE_ROOT/include/SDL3/SDL_version.h")"
  fi
  echo "[INFO] Staged SDL header version: ${SDL_MAJOR:-0}.${SDL_MINOR:-0}.${SDL_PATCH:-0}"
fi
cat > "$ROOT_DIR/build/android.env" <<ENV
export ANDROID_NDK_HOME="$ANDROID_NDK_HOME"
export SDL3_ANDROID_ROOT="$STAGE_ROOT"
export SDL3_IMAGE_ROOT="$STAGE_ROOT"
export SDL3_TTF_ROOT="$STAGE_ROOT"
export SDL3_MIXER_ROOT="$STAGE_ROOT"
ENV

echo "[OK] Wrote: $ROOT_DIR/build/android.env"
echo "[NEXT] source build/android.env && ./build/android.sh"
