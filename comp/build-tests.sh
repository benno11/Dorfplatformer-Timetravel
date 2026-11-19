#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake --preset tests
cmake --build --preset tests --parallel
ctest --preset tests
