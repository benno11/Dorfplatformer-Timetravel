#!/usr/bin/env bash
set -euo pipefail

BOLT_PROFILE_DIR="${DESKTOP_BOLT_PROFILE_DIR:-$PWD/build/profiles/desktop-bolt}"
BOLT_BIN="${LLVM_BOLT_BIN:-$(command -v llvm-bolt || true)}"
TARGET_BIN="${DESKTOP_BOLT_TARGET:-$PWD/build-ci/platformer}"
STRIP_BIN="${DESKTOP_STRIP_BIN:-$(command -v llvm-strip || command -v strip || true)}"

find_profile_for_target() {
  local target_path="$1"
  local target_name target_stem target_parent target_parent_name candidate=""
  target_name="$(basename "$target_path")"
  target_stem="${target_name%.*}"
  target_parent="$(dirname "$target_path")"
  target_parent_name="$(basename "$target_parent")"

  for candidate in \
    "$BOLT_PROFILE_DIR/$target_name.fdata" \
    "$BOLT_PROFILE_DIR/$target_stem.fdata" \
    "$BOLT_PROFILE_DIR/$target_parent_name/$target_name.fdata" \
    "$BOLT_PROFILE_DIR/$target_parent_name/$target_stem.fdata"; do
    if [ -f "$candidate" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done

  return 1
}

strip_binary() {
  local bin_path="$1"
  if [ -z "$STRIP_BIN" ]; then
    echo "[WARN] No strip tool found; leaving $bin_path unstripped"
    return 0
  fi

  local before_size after_size
  before_size="$(wc -c < "$bin_path" | tr -d '[:space:]')"
  "$STRIP_BIN" --strip-unneeded "$bin_path" 2>/dev/null || "$STRIP_BIN" "$bin_path"
  after_size="$(wc -c < "$bin_path" | tr -d '[:space:]')"
  echo "[INFO] Stripped $(basename "$bin_path"): ${before_size} -> ${after_size} bytes"
}

if [ ! -f "$TARGET_BIN" ]; then
  echo "[WARN] Desktop binary not found at $TARGET_BIN; skipping layout optimization"
  exit 0
fi

profile_path="$(find_profile_for_target "$TARGET_BIN" || true)"
if [ -z "$profile_path" ]; then
  echo "[INFO] No desktop BOLT profile found for $(basename "$TARGET_BIN"); keeping linker layout"
  strip_binary "$TARGET_BIN"
  exit 0
fi

if [ -z "$BOLT_BIN" ]; then
  echo "[ERROR] Found desktop BOLT profile, but llvm-bolt is not available on PATH."
  echo "[HINT] Install llvm-bolt or set LLVM_BOLT_BIN before running this step."
  exit 1
fi

optimized_path="${TARGET_BIN}.bolt"
echo "[INFO] Optimizing desktop code layout for $(basename "$TARGET_BIN") with profile $(basename "$profile_path")"
"$BOLT_BIN" \
  "$TARGET_BIN" \
  -o "$optimized_path" \
  -data="$profile_path" \
  -reorder-blocks=ext-tsp \
  -reorder-functions=hfsort+ \
  -split-functions \
  -split-all-cold \
  -update-debug-sections \
  -dyno-stats

mv -f "$optimized_path" "$TARGET_BIN"
strip_binary "$TARGET_BIN"
