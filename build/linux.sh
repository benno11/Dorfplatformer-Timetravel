#!/usr/bin/env bash
set -e
#include <SDL2/SDL.h>
# ---- CONFIG ----
CXX=g++
CXXFLAGS="-std=c++17 -O2"
SRC_DIR="src"
OUT_PLATFORMER="platformer"
OUT_SHEET="sheet_config"
MIXER_LIBS=""
if pkg-config --exists SDL2_mixer; then
  MIXER_LIBS="-lSDL2_mixer"
fi

# ---- BUILD ----
$CXX $CXXFLAGS \
  $SRC_DIR/main.cpp \
  $SRC_DIR/TileMap.cpp \
  $SRC_DIR/LevelLoader.cpp \
  $SRC_DIR/TextRenderer.cpp \
  $SRC_DIR/LevelSelect.cpp \
  -lSDL2 \
  -lSDL2_image \
  -lSDL2_ttf \
  $MIXER_LIBS \
  -o $OUT_PLATFORMER

$CXX $CXXFLAGS \
  $SRC_DIR/SheetConfigTool.cpp \
  -lSDL2 \
  -lSDL2_image \
  -o $OUT_SHEET

echo "Build OK -> ./$OUT_PLATFORMER and ./$OUT_SHEET"
