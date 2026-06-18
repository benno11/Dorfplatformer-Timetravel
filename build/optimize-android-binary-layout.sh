#!/usr/bin/env bash
set -euo pipefail

: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME}"

ABIS_RAW="${ABIS:-arm64-v8a,armeabi-v7a,x86_64,x86}"
BOLT_PROFILE_DIR="${ANDROID_BOLT_PROFILE_DIR:-$PWD/build/profiles/android-bolt}"
TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64"
STRIP_TOOL="$TOOLCHAIN/bin/llvm-strip"
BOLT_BIN="${LLVM_BOLT_BIN:-$(command -v llvm-bolt || true)}"

normalize_abi() {
  case "$1" in
    arm32|armeabi-v7a) printf '%s\n' "armeabi-v7a" ;;
    x86-64|x86_64) printf '%s\n' "x86_64" ;;
    *) printf '%s\n' "$1" ;;
  esac
}

find_profile_for_abi() {
  local abi="$1"
  local candidate=""

  for candidate in \
    "$BOLT_PROFILE_DIR/$abi.fdata" \
    "$BOLT_PROFILE_DIR/libplatformer-$abi.fdata" \
    "$BOLT_PROFILE_DIR/$abi/libplatformer.fdata"; do
    if [ -f "$candidate" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done

  return 1
}

strip_library() {
  local lib_path="$1"
  if [ ! -x "$STRIP_TOOL" ]; then
    echo "[WARN] llvm-strip not found at $STRIP_TOOL; leaving $lib_path unstripped"
    return 0
  fi

  local before_size after_size
  before_size="$(wc -c < "$lib_path" | tr -d '[:space:]')"
  "$STRIP_TOOL" --strip-unneeded "$lib_path"
  after_size="$(wc -c < "$lib_path" | tr -d '[:space:]')"
  echo "[INFO] Stripped $(basename "$lib_path"): ${before_size} -> ${after_size} bytes"
}

IFS=', ' read -r -a ABI_LIST <<< "$ABIS_RAW"

if [ "${#ABI_LIST[@]}" -eq 0 ]; then
  echo "[WARN] No Android ABIs requested; skipping binary layout optimization"
  exit 0
fi

if [ ! -d "$BOLT_PROFILE_DIR" ]; then
  echo "[INFO] No Android BOLT profile directory at $BOLT_PROFILE_DIR; stripping linked libraries only"
fi

for abi_entry in "${ABI_LIST[@]}"; do
  [ -n "$abi_entry" ] || continue
  abi="$(normalize_abi "$abi_entry")"
  lib_path="$PWD/build/android/$abi/libplatformer.so"

  if [ ! -f "$lib_path" ]; then
    echo "[WARN] Built library not found for ABI=$abi at $lib_path; skipping"
    continue
  fi

  profile_path="$(find_profile_for_abi "$abi" || true)"
  if [ -z "$profile_path" ]; then
    echo "[INFO] No BOLT profile found for ABI=$abi; keeping linker layout"
    strip_library "$lib_path"
    continue
  fi

  if [ -z "$BOLT_BIN" ]; then
    echo "[ERROR] Found BOLT profile for ABI=$abi, but llvm-bolt is not available on PATH."
    echo "[HINT] Install llvm-bolt or set LLVM_BOLT_BIN before running this step."
    exit 1
  fi

  optimized_path="${lib_path%.so}.bolt.so"
  echo "[INFO] Optimizing code layout for ABI=$abi with profile $(basename "$profile_path")"
  "$BOLT_BIN" \
    "$lib_path" \
    -o "$optimized_path" \
    -data="$profile_path" \
    -reorder-blocks=ext-tsp \
    -reorder-functions=hfsort+ \
    -split-functions \
    -split-all-cold \
    -update-debug-sections \
    -dyno-stats

  mv -f "$optimized_path" "$lib_path"
  strip_library "$lib_path"
done
