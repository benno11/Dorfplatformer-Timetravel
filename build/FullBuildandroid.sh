#!/usr/bin/env bash
set -euo pipefail
clear

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# Pin SDL source used by setup script unless caller overrides.
export SDL_REF="${SDL_REF:-release-2.32.x}"
export SDL_REQUIRED_VERSION="${SDL_REQUIRED_VERSION:-2.32.11}"
export FORCE_STAGED_SDL_ROOT="${FORCE_STAGED_SDL_ROOT:-1}"
export FORCE_REBUILD_SDL="${FORCE_REBUILD_SDL:-1}"

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

# Force subsequent steps to use the freshly staged SDL copy from setup step.
if [ -f "build/android.env" ]; then
  # shellcheck disable=SC1091
  . "build/android.env"
fi
export SDL2_ANDROID_ROOT="${SDL2_ANDROID_ROOT:-$ROOT_DIR/deps/android}"
export SDL2_IMAGE_ROOT="${SDL2_IMAGE_ROOT:-$SDL2_ANDROID_ROOT}"
export SDL2_TTF_ROOT="${SDL2_TTF_ROOT:-$SDL2_ANDROID_ROOT}"
export SDL2_MIXER_ROOT="${SDL2_MIXER_ROOT:-$SDL2_ANDROID_ROOT}"
echo "[INFO] Using staged SDL roots:"
echo "       SDL2_ANDROID_ROOT=$SDL2_ANDROID_ROOT"
echo "       SDL2_IMAGE_ROOT=$SDL2_IMAGE_ROOT"
echo "       SDL2_TTF_ROOT=$SDL2_TTF_ROOT"
echo "       SDL2_MIXER_ROOT=$SDL2_MIXER_ROOT"

run_step "build/dep-android.sh"
run_step "build/android.sh"
run_step "build/update-android-app.sh"

echo "[OK] Full Android build pipeline completed."
