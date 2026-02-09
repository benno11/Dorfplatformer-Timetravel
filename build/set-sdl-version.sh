#!/usr/bin/env bash
set -euo pipefail

# Updates SDL version defaults across build scripts/config.
# Usage:
#   ./build/set-sdl-version.sh 2.32.11
#   ./build/set-sdl-version.sh 2.32.11 release-2.32.x

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
  echo "Usage: $0 <sdl_version> [sdl_ref]"
  exit 1
fi

SDL_VERSION="$1"
if [[ ! "$SDL_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "[ERROR] SDL version must be like: 2.32.11"
  exit 1
fi

if [ $# -eq 2 ]; then
  SDL_REF="$2"
else
  IFS='.' read -r major minor patch <<<"$SDL_VERSION"
  SDL_REF="release-${major}.${minor}.x"
fi

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

replace_line() {
  local file="$1"
  local pattern="$2"
  local replacement="$3"
  if ! grep -qE "$pattern" "$file"; then
    echo "[WARN] Pattern not found in $file: $pattern"
    return
  fi
  sed -E -i "s|$pattern|$replacement|g" "$file"
}

replace_line "CMakeLists.txt" \
  'set\(PROJECT_SDL_MIN_VERSION "[0-9]+\.[0-9]+\.[0-9]+"\)' \
  "set(PROJECT_SDL_MIN_VERSION \"$SDL_VERSION\")"

replace_line "README.md" \
  '- SDL2 version required: [0-9]+\.[0-9]+\.[0-9]+ or newer' \
  "- SDL2 version required: $SDL_VERSION or newer"

replace_line "build/linux.sh" \
  'SDL_REQUIRED_VERSION="\$\{SDL_REQUIRED_VERSION:-[0-9]+\.[0-9]+\.[0-9]+\}"' \
  "SDL_REQUIRED_VERSION=\"\${SDL_REQUIRED_VERSION:-$SDL_VERSION}\""

replace_line "build/mobile.sh" \
  'SDL_REQUIRED_VERSION="\$\{SDL_REQUIRED_VERSION:-[0-9]+\.[0-9]+\.[0-9]+\}"' \
  "SDL_REQUIRED_VERSION=\"\${SDL_REQUIRED_VERSION:-$SDL_VERSION}\""

replace_line "build/dep-linux.sh" \
  'SDL2_REQUIRED_VERSION="\$\{SDL2_REQUIRED_VERSION:-[0-9]+\.[0-9]+\.[0-9]+\}"' \
  "SDL2_REQUIRED_VERSION=\"\${SDL2_REQUIRED_VERSION:-$SDL_VERSION}\""

replace_line "build/dep-linux.sh" \
  'SDL2_SOURCE_REF="\$\{SDL2_SOURCE_REF:-[^}]+\}"' \
  "SDL2_SOURCE_REF=\"\${SDL2_SOURCE_REF:-$SDL_REF}\""

replace_line "build/setup-android-sdl.sh" \
  'SDL_REF="\$\{SDL_REF:-[^}]+\}"' \
  "SDL_REF=\"\${SDL_REF:-$SDL_REF}\""

replace_line "build/dep-android.sh" \
  'SDL_REF="\$\{SDL_REF:-[^}]+\}"' \
  "SDL_REF=\"\${SDL_REF:-$SDL_REF}\""

replace_line "build/FullBuildandroid.sh" \
  'export SDL_REF="\$\{SDL_REF:-[^}]+\}"' \
  "export SDL_REF=\"\${SDL_REF:-$SDL_REF}\""

replace_line "build/FullBuildandroid.sh" \
  'export SDL_REQUIRED_VERSION="\$\{SDL_REQUIRED_VERSION:-[0-9]+\.[0-9]+\.[0-9]+\}"' \
  "export SDL_REQUIRED_VERSION=\"\${SDL_REQUIRED_VERSION:-$SDL_VERSION}\""

replace_line "build/android.sh" \
  'SDL_REQUIRED_VERSION="\$\{SDL_REQUIRED_VERSION:-[0-9]+\.[0-9]+\.[0-9]+\}"' \
  "SDL_REQUIRED_VERSION=\"\${SDL_REQUIRED_VERSION:-$SDL_VERSION}\""

replace_line ".github/workflows/android-build.yml" \
  'export SDL_REF=.*' \
  "        export SDL_REF=$SDL_REF"

replace_line ".github/workflows/android-build.yml" \
  'export SDL_REQUIRED_VERSION=[0-9]+\.[0-9]+\.[0-9]+' \
  "        export SDL_REQUIRED_VERSION=$SDL_VERSION"

echo "[OK] SDL version updated:"
echo "     version = $SDL_VERSION"
echo "     ref     = $SDL_REF"
echo "[NEXT] Re-run Android setup/build:"
echo "       FORCE_REBUILD_SDL=1 ./build/FullBuildandroid.sh"
