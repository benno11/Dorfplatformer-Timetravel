#!/usr/bin/env bash
set -euo pipefail

# Sync native libs + assets into Android app module.
#
# Usage:
#   ./build/update-android-app.sh
#   ABI=arm64-v8a ./build/update-android-app.sh

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ANDROID_DIR="$ROOT_DIR/Android"
APP_MAIN_DIR="$ANDROID_DIR/app/src/main"
ASSETS_SRC="$ROOT_DIR/assets"
ASSETS_DST="$APP_MAIN_DIR/assets"

ABI="${ABI:-arm64-v8a}"

if [ ! -d "$ANDROID_DIR" ]; then
  echo "[ERROR] Android project not found at: $ANDROID_DIR"
  exit 1
fi

if [ ! -d "$ASSETS_SRC" ]; then
  echo "[ERROR] Assets folder not found at: $ASSETS_SRC"
  exit 1
fi

if [ -f "$ROOT_DIR/build/android.env" ]; then
  # shellcheck disable=SC1091
  . "$ROOT_DIR/build/android.env"
fi

SDL2_ANDROID_ROOT="${SDL2_ANDROID_ROOT:-$ROOT_DIR/deps/android}"
NATIVE_BUILD_LIB="$ROOT_DIR/build/android/$ABI/libplatformer.so"
NATIVE_LIBS_DST="$APP_MAIN_DIR/jniLibs/$ABI"

mkdir -p "$NATIVE_LIBS_DST"

if [ ! -f "$NATIVE_BUILD_LIB" ]; then
  echo "[ERROR] Missing native game library: $NATIVE_BUILD_LIB"
  echo "[HINT] Build it first with: ABI=$ABI ./build/android.sh"
  exit 1
fi

cp -f "$NATIVE_BUILD_LIB" "$NATIVE_LIBS_DST/libplatformer.so"
echo "[OK] Synced: $NATIVE_BUILD_LIB -> $NATIVE_LIBS_DST/libplatformer.so"

copy_if_exists() {
  local src="$1"
  local dst="$2"
  if [ -f "$src" ]; then
    cp -f "$src" "$dst"
    echo "[OK] Synced: $(basename "$src")"
  fi
}

copy_if_exists "$SDL2_ANDROID_ROOT/lib/$ABI/libSDL2.so" "$NATIVE_LIBS_DST/libSDL2.so"
copy_if_exists "${SDL2_IMAGE_ROOT:-$SDL2_ANDROID_ROOT}/lib/$ABI/libSDL2_image.so" "$NATIVE_LIBS_DST/libSDL2_image.so"
copy_if_exists "${SDL2_TTF_ROOT:-$SDL2_ANDROID_ROOT}/lib/$ABI/libSDL2_ttf.so" "$NATIVE_LIBS_DST/libSDL2_ttf.so"
copy_if_exists "${SDL2_MIXER_ROOT:-$SDL2_ANDROID_ROOT}/lib/$ABI/libSDL2_mixer.so" "$NATIVE_LIBS_DST/libSDL2_mixer.so"

# Package libc++_shared.so from NDK for runtime linking.
if [ -z "${ANDROID_NDK_HOME:-}" ]; then
  for c in \
    "$HOME/Android/Sdk/ndk-bundle" \
    "$HOME/Android/Sdk/ndk" \
    "$HOME/android-sdk/ndk-bundle" \
    "$HOME/android-sdk/ndk" \
    "/opt/android-ndk"; do
    if [ -x "$c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++" ]; then
      ANDROID_NDK_HOME="$c"
      break
    fi
  done
fi

if [ -z "${ANDROID_NDK_HOME:-}" ] && [ -d "$HOME/Android/Sdk/ndk" ]; then
  latest_ndk="$(find "$HOME/Android/Sdk/ndk" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -n1 || true)"
  if [ -n "$latest_ndk" ] && [ -x "$latest_ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++" ]; then
    ANDROID_NDK_HOME="$latest_ndk"
  fi
fi

ndk_arch=""
case "$ABI" in
  arm64-v8a) ndk_arch="aarch64-linux-android" ;;
  armeabi-v7a) ndk_arch="arm-linux-androideabi" ;;
  x86_64) ndk_arch="x86_64-linux-android" ;;
  x86) ndk_arch="i686-linux-android" ;;
esac

if [ -n "$ndk_arch" ] && [ -n "${ANDROID_NDK_HOME:-}" ]; then
  cxx_shared="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/$ndk_arch/libc++_shared.so"
  copy_if_exists "$cxx_shared" "$NATIVE_LIBS_DST/libc++_shared.so"
fi

rm -rf "$ASSETS_DST"
cp -a "$ASSETS_SRC" "$ASSETS_DST"
echo "[OK] Synced assets -> $ASSETS_DST"

echo "[DONE] Android app content updated."
echo "[NEXT] cd Android && ./gradlew assembleDebug"
