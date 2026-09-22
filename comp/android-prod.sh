#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_HOME:-}}"
IFS=',' read -r -a ABI_LIST <<< "${ABIS:-${ABI:-arm64-v8a}}"

if [[ -z "${ANDROID_NDK_HOME}" ]]; then
  echo "ANDROID_NDK_HOME is required for Android native builds." >&2
  exit 1
fi

for abi in "${ABI_LIST[@]}"; do
  abi="${abi//[[:space:]]/}"
  if [[ -z "${abi}" ]]; then
    continue
  fi

  echo "[info] Building Android native library for ${abi}"

  sdl3_dir=""
  sdl3_image_dir=""
  sdl3_ttf_dir=""
  sdl3_mixer_dir=""
  cmake_prefix_path="${CMAKE_PREFIX_PATH:-}"
  if [[ -d "${REPO_ROOT}/deps/android-install/${abi}/SDL" ]]; then
    sdl3_dir="${REPO_ROOT}/deps/android-install/${abi}/SDL/lib/cmake/SDL3"
    sdl3_image_dir="${REPO_ROOT}/deps/android-install/${abi}/SDL_image/lib/cmake/SDL3_image"
    sdl3_ttf_dir="${REPO_ROOT}/deps/android-install/${abi}/SDL_ttf/lib/cmake/SDL3_ttf"
    sdl3_mixer_dir="${REPO_ROOT}/deps/android-install/${abi}/SDL_mixer/lib/cmake/SDL3_mixer"
    cmake_prefix_path="${REPO_ROOT}/deps/android-install/${abi}/SDL:${REPO_ROOT}/deps/android-install/${abi}/SDL_image:${REPO_ROOT}/deps/android-install/${abi}/SDL_ttf:${REPO_ROOT}/deps/android-install/${abi}/SDL_mixer:${cmake_prefix_path}"
  fi

  build_dir="${REPO_ROOT}/build/android/${abi}"
  cmake -S "${REPO_ROOT}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DANDROID_ABI="${abi}" \
    -DANDROID_PLATFORM=android-24 \
    -DANDROID_NDK="${ANDROID_NDK_HOME}" \
    -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" \
    -DCMAKE_PREFIX_PATH="${cmake_prefix_path}" \
    -DSDL3_DIR="${sdl3_dir}" \
    -DSDL3_image_DIR="${sdl3_image_dir}" \
    -DSDL3_ttf_DIR="${sdl3_ttf_dir}" \
    -DSDL3_mixer_DIR="${sdl3_mixer_dir}"

  cmake --build "${build_dir}" --parallel
done
