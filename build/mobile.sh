#!/usr/bin/env bash
set -euo pipefail

# Mobile-oriented build script (Termux / Android-like Linux env)
# Usage:
#   ./build/mobile.sh
#   CXX=clang++ ./build/mobile.sh

CXX="${CXX:-clang++}"
CXXFLAGS="${CXXFLAGS:--std=c++17 -O2 -fPIC}"
LDFLAGS="${LDFLAGS:-}"
SRC_DIR="src"
OUT_PLATFORMER="platformer_mobile"

# SDL package names differ across environments.
pkg_sdl2=""
for p in sdl2 SDL2; do
  if pkg-config --exists "$p"; then pkg_sdl2="$p"; break; fi
done
if [ -z "$pkg_sdl2" ]; then
  echo "[ERROR] SDL2 pkg-config package not found (expected 'sdl2' or 'SDL2')."
  exit 1
fi

pkg_image=""
for p in SDL2_image sdl2-image; do
  if pkg-config --exists "$p"; then pkg_image="$p"; break; fi
done
if [ -z "$pkg_image" ]; then
  echo "[ERROR] SDL2_image pkg-config package not found."
  exit 1
fi

pkg_ttf=""
for p in SDL2_ttf sdl2-ttf; do
  if pkg-config --exists "$p"; then pkg_ttf="$p"; break; fi
done
if [ -z "$pkg_ttf" ]; then
  echo "[ERROR] SDL2_ttf pkg-config package not found."
  exit 1
fi

MIXER_PKG=""
for p in SDL2_mixer sdl2-mixer; do
  if pkg-config --exists "$p"; then MIXER_PKG="$p"; break; fi
done

PKGS=("$pkg_sdl2" "$pkg_image" "$pkg_ttf")
if [ -n "$MIXER_PKG" ]; then
  PKGS+=("$MIXER_PKG")
  echo "[INFO] SDL2_mixer found: enabling music support"
else
  echo "[INFO] SDL2_mixer not found: building without mixer linkage"
fi

CPPFLAGS_PKG="$(pkg-config --cflags "${PKGS[@]}")"
LIBS_PKG="$(pkg-config --libs "${PKGS[@]}")"

echo "[INFO] Building mobile target with: $CXX"
$CXX $CXXFLAGS $CPPFLAGS_PKG \
  "$SRC_DIR/main.cpp" \
  "$SRC_DIR/TileMap.cpp" \
  "$SRC_DIR/LevelLoader.cpp" \
  "$SRC_DIR/TextRenderer.cpp" \
  "$SRC_DIR/LevelSelect.cpp" \
  "$SRC_DIR/PlayerController.cpp" \
  $LDFLAGS $LIBS_PKG \
  -o "$OUT_PLATFORMER"

echo "[OK] Build complete -> ./$OUT_PLATFORMER"
