#!/usr/bin/env bash
set -euo pipefail

# CMake/Xcode iOS build helper. Defaults to simulator so it can be used for
# quick smoke builds; set IOS_SDK=iphoneos for device/archive builds.
MODE="${1:-build}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
IOS_SDK="${IOS_SDK:-iphonesimulator}"
CONFIG="${CONFIG:-Release}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/.build/ios-$IOS_SDK}"
DEPLOYMENT_TARGET="${PLATFORMER_IOS_DEPLOYMENT_TARGET:-15.0}"

case "$IOS_SDK" in
  iphoneos)
    IOS_ARCHS="${IOS_ARCHS:-arm64}"
    VCPKG_TRIPLET="${VCPKG_TARGET_TRIPLET:-arm64-ios}"
    ;;
  iphonesimulator)
    IOS_ARCHS="${IOS_ARCHS:-arm64}"
    VCPKG_TRIPLET="${VCPKG_TARGET_TRIPLET:-arm64-ios-simulator}"
    ;;
  *)
    echo "[ERROR] IOS_SDK must be iphoneos or iphonesimulator (got: $IOS_SDK)" >&2
    exit 1
    ;;
esac

if ! command -v cmake >/dev/null 2>&1; then
  echo "[ERROR] cmake is required for iOS builds." >&2
  exit 1
fi
if ! command -v xcodebuild >/dev/null 2>&1; then
  echo "[ERROR] Xcode command line tools are required for iOS builds." >&2
  exit 1
fi

configure() {
  cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="$IOS_SDK" \
    -DCMAKE_OSX_ARCHITECTURES="$IOS_ARCHS" \
    -DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO \
    -DPLATFORMER_IOS_DEPLOYMENT_TARGET="$DEPLOYMENT_TARGET" \
    -DPLATFORMER_REQUIRE_SDL3_MIXER=OFF \
    -DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET"
}

case "$MODE" in
  configure)
    configure
    ;;
  build)
    configure
    cmake --build "$BUILD_DIR" --config "$CONFIG" --target ios-package
    ;;
  clean)
    rm -rf "$BUILD_DIR"
    echo "[OK] Removed $BUILD_DIR"
    ;;
  *)
    echo "Usage: $0 [configure|build|clean]" >&2
    exit 1
    ;;
esac
