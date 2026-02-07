#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

run_step() {
  local script="$1"
  if [ ! -f "$script" ]; then
    echo "[ERROR] Missing script: $script"
    exit 1
  fi
  echo "[STEP] $script"
  bash "$script"
}

run_step "build/setup-android-sdl.sh"
run_step "build/dep-android.sh"
run_step "build/android.sh"
run_step "build/update-android-app.sh"

echo "[OK] Full Android build pipeline completed."
