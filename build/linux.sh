#!/usr/bin/env bash
set -e
#include <SDL2/SDL.h>
# ---- CONFIG ----
CXX=g++
FAST="${FAST:-0}"
if [ "$FAST" = "1" ]; then
  CXXFLAGS="-std=c++17 -O1 -fno-plt"
  echo "[INFO] FAST build enabled"
else
  CXXFLAGS="-std=c++17 -O3 -DNDEBUG -flto -fno-plt"
fi
SRC_DIR="src"
OUT_PLATFORMER="platformer"
OUT_SHEET="sheet_config"
MIXER_LIBS=""
SDL_CFLAGS="$(pkg-config --cflags sdl2 SDL2_image SDL2_ttf 2>/dev/null || true)"
SDL_LIBS="$(pkg-config --libs sdl2 SDL2_image SDL2_ttf 2>/dev/null || true)"
if [ -z "$SDL_CFLAGS" ] || [ -z "$SDL_LIBS" ]; then
  echo "[ERROR] Missing pkg-config entries for sdl2/SDL2_image/SDL2_ttf"
  echo "[HINT] Install dev packages (e.g. libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev)"
  exit 1
fi
if pkg-config --exists SDL2_mixer; then
  MIXER_LIBS="$(pkg-config --libs SDL2_mixer)"
fi
JSON_CFLAGS=""
if [ -f "third_party/nlohmann/json.hpp" ]; then
  JSON_CFLAGS="-Ithird_party"
fi

# ---- BUILD ----
$CXX $CXXFLAGS \
  $SDL_CFLAGS \
  $JSON_CFLAGS \
  $SRC_DIR/main.cpp \
  $SRC_DIR/TileMap.cpp \
  $SRC_DIR/AssetPath.cpp \
  $SRC_DIR/LevelLoader.cpp \
  $SRC_DIR/TextRenderer.cpp \
  $SRC_DIR/LevelSelect.cpp \
  $SRC_DIR/PlayerController.cpp \
  $SRC_DIR/LevelManager.cpp \
  $SDL_LIBS \
  $MIXER_LIBS \
  -o $OUT_PLATFORMER

$CXX $CXXFLAGS \
  $SDL_CFLAGS \
  $SRC_DIR/SheetConfigTool.cpp \
  $SDL_LIBS \
  -o $OUT_SHEET

echo "Build OK -> ./$OUT_PLATFORMER and ./$OUT_SHEET"
