#!/usr/bin/env bash
set -euo pipefail

# Mobile-oriented build script (Termux / Android-like Linux env)
# Usage:
#   ./build/mobile.sh
#   CXX=clang++ ./build/mobile.sh

CXX="${CXX:-clang++}"
CXXFLAGS="${CXXFLAGS:--std=c++17 -O2 -fPIC}"
LDFLAGS="${LDFLAGS:-}"
SDL_REQUIRED_VERSION="${SDL_REQUIRED_VERSION:-3.4.0}"
SRC_DIR="src"
OUT_PLATFORMER="platformer_mobile"

# SDL package names differ across environments.
pkg_sdl3=""
for p in sdl3 sdl3; do
  if pkg-config --exists "$p"; then pkg_sdl3="$p"; break; fi
done
if [ -z "$pkg_sdl3" ]; then
  echo "[ERROR] sdl3 pkg-config package not found (expected 'sdl3' or 'sdl3')."
  exit 1
fi
if ! pkg-config --atleast-version="$SDL_REQUIRED_VERSION" "$pkg_sdl3"; then
  echo "[WARN] sdl3 ${SDL_REQUIRED_VERSION}+ not found. Continuing with installed version: $(pkg-config --modversion "$pkg_sdl3" 2>/dev/null || echo unknown)"
fi

pkg_image=""
for p in sdl3_image sdl3-image; do
  if pkg-config --exists "$p"; then pkg_image="$p"; break; fi
done
if [ -z "$pkg_image" ]; then
  echo "[ERROR] sdl3_image pkg-config package not found."
  exit 1
fi

pkg_ttf=""
for p in sdl3_ttf sdl3-ttf; do
  if pkg-config --exists "$p"; then pkg_ttf="$p"; break; fi
done
if [ -z "$pkg_ttf" ]; then
  echo "[ERROR] sdl3_ttf pkg-config package not found."
  exit 1
fi

MIXER_PKG=""
for p in sdl3_mixer sdl3-mixer; do
  if pkg-config --exists "$p"; then MIXER_PKG="$p"; break; fi
done

PKGS=("$pkg_sdl3" "$pkg_image" "$pkg_ttf")
if [ -n "$MIXER_PKG" ]; then
  PKGS+=("$MIXER_PKG")
  echo "[INFO] sdl3_mixer found: enabling music support"
else
  echo "[INFO] sdl3_mixer not found: building without mixer linkage"
fi

CPPFLAGS_PKG="$(pkg-config --cflags "${PKGS[@]}")"
LIBS_PKG="$(pkg-config --libs "${PKGS[@]}")"

echo "[INFO] Building mobile target with: $CXX"
$CXX $CXXFLAGS $CPPFLAGS_PKG \
  "$SRC_DIR/main.cpp" \
  "$SRC_DIR/TileMap.cpp" \
  "$SRC_DIR/AssetPath.cpp" \
  "$SRC_DIR/LevelLoader.cpp" \
  "$SRC_DIR/TextRenderer.cpp" \
  "$SRC_DIR/LevelSelect.cpp" \
  "$SRC_DIR/PlayerController.cpp" \
  "$SRC_DIR/LevelManager.cpp" \
  "$SRC_DIR/GameSupport.cpp" \
  "$SRC_DIR/CrashReporter.cpp" \
  "$SRC_DIR/FrontendMenu.cpp" \
  $LDFLAGS $LIBS_PKG \
  -o "$OUT_PLATFORMER"

echo "[OK] Build complete -> ./$OUT_PLATFORMER"
