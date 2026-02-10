#!/usr/bin/env bash
set -euo pipefail

# Build and stage libcurl for Android.
#
# Output layout:
#   deps/android-curl/include/curl/curl.h
#   deps/android-curl/lib/<abi>/libcurl.so
#
# Usage:
#   ./build/setup-android-curl.sh
#   ABI=arm64-v8a ./build/setup-android-curl.sh
#   ABI=arm64-v8a API=24 ./build/setup-android-curl.sh

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ABI="${ABI:-arm64-v8a}"
API="${API:-24}"
SRC_DIR="$ROOT_DIR/deps/android-curl-src"
BUILD_DIR="$ROOT_DIR/build/android-curl/$ABI"
STAGE_DIR="$ROOT_DIR/deps/android-curl"

if [ -f "$ROOT_DIR/build/android.env" ]; then
  # shellcheck disable=SC1091
  . "$ROOT_DIR/build/android.env"
fi

if [ -z "${ANDROID_NDK_HOME:-}" ]; then
  for c in \
    "$HOME/Android/Sdk/ndk-bundle" \
    "$HOME/Android/Sdk/ndk" \
    "$HOME/android-sdk/ndk-bundle" \
    "$HOME/android-sdk/ndk" \
    "/opt/android-ndk"; do
    if [ -x "$c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++" ]; then
      ANDROID_NDK_HOME="$c"
      export ANDROID_NDK_HOME
      break
    fi
  done
fi

if [ -z "${ANDROID_NDK_HOME:-}" ] && [ -d "$HOME/Android/Sdk/ndk" ]; then
  latest_ndk="$(find "$HOME/Android/Sdk/ndk" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -n1 || true)"
  if [ -n "$latest_ndk" ] && [ -x "$latest_ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++" ]; then
    ANDROID_NDK_HOME="$latest_ndk"
    export ANDROID_NDK_HOME
  fi
fi

: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME}"

if ! command -v cmake >/dev/null 2>&1; then
  echo "[error] cmake not found"
  exit 1
fi

if ! command -v git >/dev/null 2>&1; then
  echo "[error] git not found"
  exit 1
fi

if [ ! -d "$SRC_DIR/.git" ]; then
  echo "[info] cloning curl source..."
  rm -rf "$SRC_DIR"
  git clone --depth 1 --branch curl-8_12_1 https://github.com/curl/curl.git "$SRC_DIR"
else
  echo "[info] using existing curl source at $SRC_DIR"
fi

mkdir -p "$BUILD_DIR"

case "$ABI" in
  arm64-v8a|armeabi-v7a|x86_64|x86) ;;
  *)
    echo "[error] unsupported ABI: $ABI"
    exit 1
    ;;
esac

echo "[info] configuring curl for Android ABI=$ABI API=$API"
cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_STATIC_LIBS=OFF \
  -DCURL_USE_OPENSSL=OFF \
  -DCURL_USE_LIBPSL=OFF \
  -DHTTP_ONLY=ON \
  -DBUILD_CURL_EXE=OFF \
  -DBUILD_TESTING=OFF \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" \
  -DANDROID_PLATFORM="android-$API"

echo "[info] building curl..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

LIB_CANDIDATES=(
  "$BUILD_DIR/lib/libcurl.so"
  "$BUILD_DIR/libcurl/libcurl.so"
)
LIB_PATH=""
for c in "${LIB_CANDIDATES[@]}"; do
  if [ -f "$c" ]; then
    LIB_PATH="$c"
    break
  fi
done

if [ -z "$LIB_PATH" ]; then
  LIB_PATH="$(find "$BUILD_DIR" -type f -name 'libcurl.so' | head -n1 || true)"
fi

if [ -z "$LIB_PATH" ] || [ ! -f "$LIB_PATH" ]; then
  echo "[error] could not find built libcurl.so under $BUILD_DIR"
  exit 1
fi

mkdir -p "$STAGE_DIR/include/curl" "$STAGE_DIR/lib/$ABI"
cp -f "$SRC_DIR/include/curl/"*.h "$STAGE_DIR/include/curl/"
cp -f "$LIB_PATH" "$STAGE_DIR/lib/$ABI/libcurl.so"

echo "[ok] staged Android curl:"
echo "     header: $STAGE_DIR/include/curl/curl.h"
echo "     lib:    $STAGE_DIR/lib/$ABI/libcurl.so"
echo "[next] ABI=$ABI ./build/fix-update-android-app.sh"
