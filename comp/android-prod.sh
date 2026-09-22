#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ABI="${ABI:-arm64-v8a}"
ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_HOME:-}}"

if [[ -z "${ANDROID_NDK_HOME}" ]]; then
  echo "ANDROID_NDK_HOME is required for Android native builds." >&2
  exit 1
fi

if [[ -d "${REPO_ROOT}/deps/android-install/${ABI}/SDL" ]]; then
  export SDL3_DIR="${REPO_ROOT}/deps/android-install/${ABI}/SDL/lib/cmake/SDL3"
  export SDL3_image_DIR="${REPO_ROOT}/deps/android-install/${ABI}/SDL_image/lib/cmake/SDL3_image"
  export SDL3_ttf_DIR="${REPO_ROOT}/deps/android-install/${ABI}/SDL_ttf/lib/cmake/SDL3_ttf"
  export SDL3_mixer_DIR="${REPO_ROOT}/deps/android-install/${ABI}/SDL_mixer/lib/cmake/SDL3_mixer"
  export CMAKE_PREFIX_PATH="${REPO_ROOT}/deps/android-install/${ABI}/SDL:${REPO_ROOT}/deps/android-install/${ABI}/SDL_image:${REPO_ROOT}/deps/android-install/${ABI}/SDL_ttf:${REPO_ROOT}/deps/android-install/${ABI}/SDL_mixer:${CMAKE_PREFIX_PATH:-}"
fi

cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DANDROID_ABI="${ABI}" \
  -DANDROID_PLATFORM=android-24 \
  -DANDROID_NDK="${ANDROID_NDK_HOME}" \
  -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" \
  -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-}" \
  -DSDL3_DIR="${SDL3_DIR:-}" \
  -DSDL3_image_DIR="${SDL3_image_DIR:-}" \
  -DSDL3_ttf_DIR="${SDL3_ttf_DIR:-}" \
  -DSDL3_mixer_DIR="${SDL3_mixer_DIR:-}"

cmake --build "${REPO_ROOT}/build" --parallel
