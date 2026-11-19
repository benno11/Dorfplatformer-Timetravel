#!/usr/bin/env bash
set -euo pipefail

cmake --preset production
cmake --build --preset production --parallel

if [[ "${*:-}" == *"--run"* ]]; then
  if [[ -x "./build/production/platformer" ]]; then
    exec ./build/production/platformer
  fi
fi
