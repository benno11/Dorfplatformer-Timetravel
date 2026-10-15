#!/usr/bin/env bash
set -euo pipefail
echo "[info] Android binary layout optimization restored from comp wrapper."

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
IFS=',' read -r -a ABI_LIST <<< "${ABIS:-${ABI:-arm64-v8a}}"

strip_tool=""
if [[ -n "${NDK_HOME}" ]]; then
  for candidate in "${NDK_HOME}"/toolchains/llvm/prebuilt/*/bin/llvm-strip \
      "${NDK_HOME}"/toolchains/llvm/prebuilt/*/bin/llvm-strip.exe; do
    if [[ -x "${candidate}" ]]; then
      strip_tool="${candidate}"
      break
    fi
  done
fi
if [[ -z "${strip_tool}" ]]; then
  echo "[error] Cannot find NDK llvm-strip; set ANDROID_NDK_HOME or ANDROID_NDK_ROOT." >&2
  exit 1
fi

stripped=0
for abi in "${ABI_LIST[@]}"; do
  abi="${abi//[[:space:]]/}"
  [[ -n "${abi}" ]] || continue

  library=""
  # Match the source selection used by update-android-app.sh so the stripped
  # library is the one subsequently copied into the APK.
  for candidate in \
      "${REPO_ROOT}/build/android/${abi}/libplatformer.so" \
      "${REPO_ROOT}/build/android/${abi}/platformer" \
      "${REPO_ROOT}/build/libplatformer.so" \
      "${REPO_ROOT}/build/platformer"; do
    if [[ -f "${candidate}" ]]; then
      library="${candidate}"
      break
    fi
  done
  if [[ -z "${library}" ]]; then
    echo "[error] Missing Android native library for ${abi}." >&2
    exit 1
  fi

  # Preserve JNI exports and runtime symbols; only remove debug information.
  "${strip_tool}" --strip-debug "${library}"
  echo "[info] Stripped Android debug symbols for ${abi}: ${library}"
  stripped=$((stripped + 1))
done

if [[ "${stripped}" -eq 0 ]]; then
  echo "[error] No Android ABIs were requested for symbol stripping." >&2
  exit 1
fi
