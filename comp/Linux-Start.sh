#!/usr/bin/env bash
set -euo pipefail
echo "[info] Launch wrapper restored from comp."
if [[ -x "./build/platformer" ]]; then
  exec ./build/platformer
fi
