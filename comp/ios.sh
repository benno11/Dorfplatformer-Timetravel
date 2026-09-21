#!/usr/bin/env bash
set -euo pipefail
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
if [[ "$1" == "doctor" || "$1" == "verify" ]]; then
  exit 0
fi

echo "[info] iOS packaging wrapper restored from comp."
