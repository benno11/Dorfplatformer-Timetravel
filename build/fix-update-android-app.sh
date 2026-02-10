#!/usr/bin/env bash
set -euo pipefail

# Fix helper for:
#   [error] missing native game library: .../build/android/<abi>/libplatformer.so
#
# This script ensures the native Android library exists, then syncs
# libs/assets into Android/app via update-android-app.sh.
#
# Usage:
#   ./build/fix-update-android-app.sh
#   ABI=arm64-v8a ./build/fix-update-android-app.sh
#   ABI=arm64-v8a ANDROID_ALLOW_NO_CURL=1 ./build/fix-update-android-app.sh

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ABI="${ABI:-arm64-v8a}"
LIB_PATH="$ROOT_DIR/build/android/$ABI/libplatformer.so"

if [ ! -f "$LIB_PATH" ]; then
  echo "[info] missing $LIB_PATH"
  echo "[info] building native Android library first..."
  ABI="$ABI" "${ROOT_DIR}/build/android.sh"
fi

if [ ! -f "$LIB_PATH" ]; then
  echo "[error] build finished but library still missing: $LIB_PATH"
  exit 1
fi

echo "[info] syncing Android app contents..."
ABI="$ABI" "${ROOT_DIR}/build/update-android-app.sh"

echo "[done] Android app is ready to assemble."
echo "[next] cd Android && ./gradlew assembleDebug"
