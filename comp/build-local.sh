#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel

if [[ "${*:-}" == *"--run"* ]]; then
  if [[ -x "./build/platformer" ]]; then
    exec ./build/platformer
  fi
fi
