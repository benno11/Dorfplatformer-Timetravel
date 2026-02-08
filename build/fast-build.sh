#!/usr/bin/env bash
set -euo pipefail

# Fast desktop/Linux iteration build:
# 1) native build with FAST=1 flags
# 2) optionally run immediately
#
# Usage:
#   ./build/fast-build.sh
#   RUN=1 ./build/fast-build.sh

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
RUN="${RUN:-0}"

echo "[STEP] FAST Linux build"
FAST=1 "$ROOT_DIR/build/linux.sh"

if [ "$RUN" = "1" ]; then
  echo "[STEP] Run platformer"
  "$ROOT_DIR/platformer"
fi

echo "[DONE] Fast build complete."
