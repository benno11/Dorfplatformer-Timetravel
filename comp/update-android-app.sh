#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IFS=',' read -r -a ABI_LIST <<< "${ABIS:-${ABI:-arm64-v8a,armeabi-v7a}}"
JNI_LIBS_DIR="${REPO_ROOT}/Android/app/src/main/jniLibs"

mkdir -p "${JNI_LIBS_DIR}"

copied=0
for abi in "${ABI_LIST[@]}"; do
  abi="${abi//[[:space:]]/}"
  if [[ -z "${abi}" ]]; then
    continue
  fi

  src=""
  for candidate in \
      "${REPO_ROOT}/build/android/${abi}/libplatformer.so" \
      "${REPO_ROOT}/build/android/${abi}/platformer" \
      "${REPO_ROOT}/build/libplatformer.so" \
      "${REPO_ROOT}/build/platformer"; do
    if [[ -f "${candidate}" ]]; then
      src="${candidate}"
      break
    fi
  done

  if [[ -z "${src}" ]]; then
    echo "[error] Missing Android native library for ${abi}." >&2
    echo "[error] Expected build/android/${abi}/libplatformer.so or build/android/${abi}/platformer." >&2
    exit 1
  fi

  dst_dir="${JNI_LIBS_DIR}/${abi}"
  mkdir -p "${dst_dir}"
  cp "${src}" "${dst_dir}/libplatformer.so"
  echo "[info] Synced ${abi} native library from ${src}"
  copied=$((copied + 1))
done

if [[ "${copied}" -eq 0 ]]; then
  echo "[error] No Android ABIs were requested for native library sync." >&2
  exit 1
fi
