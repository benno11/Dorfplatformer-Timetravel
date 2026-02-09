#!/usr/bin/env bash
set -euo pipefail

# Rebuild SDL3_mixer for Linux from source.
#
# Usage:
#   ./build/recompile-sdl-mixer.sh
#   SDL_MIXER_REF=main ./build/recompile-sdl-mixer.sh
#   INSTALL_PREFIX=/usr/local ./build/recompile-sdl-mixer.sh

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

SDL_MIXER_REF="${SDL_MIXER_REF:-main}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"

if command -v sudo >/dev/null 2>&1; then
  SUDO="sudo"
else
  SUDO=""
fi

if ! command -v git >/dev/null 2>&1; then
  echo "[ERROR] git is required."
  exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
  echo "[ERROR] cmake is required."
  exit 1
fi

WORKDIR="$ROOT_DIR/deps/linux-sdl3-mixer-src"
BUILDDIR="$WORKDIR/build"

echo "[INFO] Recompiling SDL3_mixer"
echo "       SDL_MIXER_REF=$SDL_MIXER_REF"
echo "       JOBS=$JOBS"
echo "       INSTALL_PREFIX=$INSTALL_PREFIX"

if [ ! -d "$WORKDIR/.git" ]; then
  echo "[INFO] Cloning SDL_mixer ($SDL_MIXER_REF)"
  git clone --depth 1 --branch "$SDL_MIXER_REF" https://github.com/libsdl-org/SDL_mixer.git "$WORKDIR"
else
  echo "[INFO] Updating SDL_mixer ($SDL_MIXER_REF)"
  git -C "$WORKDIR" fetch --depth 1 origin "$SDL_MIXER_REF"
  git -C "$WORKDIR" checkout -f FETCH_HEAD
  git -C "$WORKDIR" reset --hard FETCH_HEAD
fi

echo "[INFO] Syncing SDL_mixer submodules"
git -C "$WORKDIR" submodule sync --recursive
git -C "$WORKDIR" submodule update --init --recursive --depth 1 --jobs "$JOBS"

SDL3_DIR=""
for d in \
  "$INSTALL_PREFIX/lib/cmake/SDL3" \
  "/usr/local/lib/cmake/SDL3" \
  "$ROOT_DIR/deps/linux-sdl3-src/build"; do
  if [ -f "$d/SDL3Config.cmake" ]; then
    SDL3_DIR="$d"
    break
  fi
done
if [ -z "$SDL3_DIR" ]; then
  echo "[ERROR] Could not find SDL3 CMake config (SDL3Config.cmake)."
  echo "[HINT] Rebuild SDL3 first: RECOMPILE_PC=1 ./build/recompile-sdl.sh"
  exit 1
fi
echo "[INFO] Using SDL3_DIR=$SDL3_DIR"

cmake -S "$WORKDIR" -B "$BUILDDIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
  -DSDL3_DIR="$SDL3_DIR" \
  -DSDLMIXER_VENDORED=ON \
  -DSDLMIXER_SAMPLES=OFF \
  -DSDLMIXER_EXAMPLES=OFF \
  -DSDLMIXER_TESTS=OFF \
  -DSDLMIXER_MOD=OFF \
  -DSDLMIXER_MIDI=OFF

echo "[INFO] Building SDL3_mixer..."
cmake --build "$BUILDDIR" -j"$JOBS"

echo "[INFO] Installing SDL3_mixer..."
$SUDO cmake --install "$BUILDDIR"
$SUDO ldconfig || true

if [ -f "$BUILDDIR/sdl3-mixer.pc" ] || [ -f "$INSTALL_PREFIX/lib/pkgconfig/sdl3-mixer.pc" ] || [ -f "/usr/local/lib/pkgconfig/sdl3-mixer.pc" ]; then
  echo "[OK] SDL3_mixer recompile complete."
else
  echo "[ERROR] sdl3-mixer.pc not found after install."
  exit 1
fi

