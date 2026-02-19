#!/usr/bin/env bash
set -euo pipefail

CONFIG="${CONFIG:-Release}"
BUILD_DIR="${BUILD_DIR:-.build}"
RUN="${RUN:-0}"

for arg in "$@"; do
  case "$arg" in
    --run)
      RUN=1
      ;;
    --debug)
      CONFIG=Debug
      ;;
    --release)
      CONFIG=Release
      ;;
    --relwithdebinfo)
      CONFIG=RelWithDebInfo
      ;;
    --minsizerel)
      CONFIG=MinSizeRel
      ;;
    *)
      echo "Unknown argument: $arg"
      echo "Usage: ./build-local.sh [--run] [--debug|--release|--relwithdebinfo|--minsizerel]"
      exit 1
      ;;
  esac
done

echo "[1/2] Configure project"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG"

echo "[2/2] Build targets"
cmake --build "$BUILD_DIR" --config "$CONFIG"

if [ "$RUN" = "1" ]; then
  if [ -x "$BUILD_DIR/platformer" ]; then
    echo "Launching $BUILD_DIR/platformer"
    exec "$BUILD_DIR/platformer"
  elif [ -x "$BUILD_DIR/$CONFIG/platformer" ]; then
    echo "Launching $BUILD_DIR/$CONFIG/platformer"
    exec "$BUILD_DIR/$CONFIG/platformer"
  else
    echo "Build succeeded but platformer binary was not found in $BUILD_DIR"
    exit 1
  fi
fi
