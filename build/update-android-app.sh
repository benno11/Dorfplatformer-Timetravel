#!/usr/bin/env bash
set -euo pipefail

# Sync native libs + assets into Android app module.
#
# Usage:
#   ./build/update-android-app.sh
#   ABI=arm64-v8a ./build/update-android-app.sh
#   ABI=all ./build/update-android-app.sh
#   ABIS="arm64-v8a,armeabi-v7a,x86_64,x86" ./build/update-android-app.sh

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ANDROID_DIR="$ROOT_DIR/Android"
APP_MAIN_DIR="$ANDROID_DIR/app/src/main"
ASSETS_SRC="$ROOT_DIR/assets"
ASSETS_DST="$APP_MAIN_DIR/assets"

# Track caller intent before defaults are applied.
ABI_WAS_SET=0
ABIS_WAS_SET=0
if [ -n "${ABI+x}" ]; then ABI_WAS_SET=1; fi
if [ -n "${ABIS+x}" ]; then ABIS_WAS_SET=1; fi

ABI="${ABI:-arm64-v8a}"
ABIS="${ABIS:-}"
SKIP_ASSETS="${SKIP_ASSETS:-0}"
ANDROID_ALLOW_NO_CURL="${ANDROID_ALLOW_NO_CURL:-0}"

normalize_abi() {
  case "$1" in
    arm32|armeabi-v7a) echo "armeabi-v7a" ;;
    x86-64|x86_64) echo "x86_64" ;;
    *) echo "$1" ;;
  esac
}

if [ ! -d "$ANDROID_DIR" ]; then
  echo "[error] android project not found at: $ANDROID_DIR"
  exit 1
fi

if [ ! -d "$ASSETS_SRC" ]; then
  echo "[error] assets folder not found at: $ASSETS_SRC"
  exit 1
fi

if [ -f "$ROOT_DIR/build/android.env" ]; then
  # shellcheck disable=SC1091
  . "$ROOT_DIR/build/android.env"
fi

# Default to syncing every built ABI unless the caller explicitly set ABI/ABIS.
if [ -z "${ANDROID_MULTI_ABI_CHILD:-}" ] && [ "$ABI_WAS_SET" -eq 0 ] && [ "$ABIS_WAS_SET" -eq 0 ]; then
  ABI="all"
fi

SDL3_ANDROID_ROOT="${SDL3_ANDROID_ROOT:-$ROOT_DIR/deps/android}"
SDL3_IMAGE_ROOT="${SDL3_IMAGE_ROOT:-$SDL3_ANDROID_ROOT}"
SDL3_TTF_ROOT="${SDL3_TTF_ROOT:-$SDL3_ANDROID_ROOT}"
SDL3_MIXER_ROOT="${SDL3_MIXER_ROOT:-$SDL3_ANDROID_ROOT}"
CURL_ANDROID_ROOT="${CURL_ANDROID_ROOT:-$ROOT_DIR/deps/android-curl}"

if [ -z "${ANDROID_MULTI_ABI_CHILD:-}" ]; then
  multi_abis=()
  if [ -n "$ABIS" ]; then
    abis_normalized="$(printf '%s' "$ABIS" | tr ',' ' ')"
    # shellcheck disable=SC2206
    multi_abis=($abis_normalized)
    for i in "${!multi_abis[@]}"; do
      multi_abis[$i]="$(normalize_abi "${multi_abis[$i]}")"
    done
  elif [ "$ABI" = "all" ]; then
    for candidate in arm64-v8a armeabi-v7a x86_64 x86 riscv64; do
      if [ -f "$ROOT_DIR/build/android/$candidate/libplatformer.so" ]; then
        multi_abis+=("$candidate")
      fi
    done
    if [ "${#multi_abis[@]}" -eq 0 ]; then
      echo "[error] ABI=all requested, but no built native libs found under build/android/<abi>/libplatformer.so"
      echo "[hint] run ./build/android-prod.sh first"
      exit 1
    fi
  fi

  if [ "${#multi_abis[@]}" -gt 0 ]; then
    self_script="$0"
    case "$self_script" in
      /*) ;;
      *) self_script="$PWD/$self_script" ;;
    esac
    echo "[info] multi-ABI sync requested: ${multi_abis[*]}"
    for one_abi in "${multi_abis[@]}"; do
      echo "[info] ---- syncing ABI=$one_abi ----"
      ANDROID_MULTI_ABI_CHILD=1 SKIP_ASSETS=1 ABI="$one_abi" ABIS="" "$self_script"
    done
    rm -rf "$ASSETS_DST"
    cp -a "$ASSETS_SRC" "$ASSETS_DST"
    echo "[ok] synced assets -> $ASSETS_DST"
    echo "[done] android app content updated for ABIs: ${multi_abis[*]}"
    echo "[next] cd Android && ./gradlew assembleDebug"
    exit 0
  fi
fi

ABI="$(normalize_abi "$ABI")"
NATIVE_BUILD_LIB="$ROOT_DIR/build/android/$ABI/libplatformer.so"
NATIVE_BUILD_DIR="$ROOT_DIR/build/android/$ABI"
NATIVE_LIBS_DST="$APP_MAIN_DIR/jniLibs/$ABI"

mkdir -p "$NATIVE_LIBS_DST"
rm -f \
  "$NATIVE_LIBS_DST/libplatformer.so" \
  "$NATIVE_LIBS_DST/libSDL3.so" \
  "$NATIVE_LIBS_DST/libSDL3_image.so" \
  "$NATIVE_LIBS_DST/libSDL3_ttf.so" \
  "$NATIVE_LIBS_DST/libSDL3_mixer.so" \
  "$NATIVE_LIBS_DST/libcurl.so" \
  "$NATIVE_LIBS_DST/libsdl3.so" \
  "$NATIVE_LIBS_DST/libsdl3_image.so" \
  "$NATIVE_LIBS_DST/libsdl3_ttf.so" \
  "$NATIVE_LIBS_DST/libsdl3_mixer.so" \
  "$NATIVE_LIBS_DST/libc++_shared.so"

if [ ! -f "$NATIVE_BUILD_LIB" ]; then
  echo "[error] missing native game library: $NATIVE_BUILD_LIB"
  echo "[hint] build it first with: ABI=$ABI ./build/android.sh"
  exit 1
fi

cp -f "$NATIVE_BUILD_LIB" "$NATIVE_LIBS_DST/libplatformer.so"
echo "[ok] synced: $NATIVE_BUILD_LIB -> $NATIVE_LIBS_DST/libplatformer.so"

copy_if_exists() {
  local src="$1"
  local dst="$2"
  if [ -f "$src" ]; then
    cp -f "$src" "$dst"
    echo "[ok] synced: $(basename "$src")"
    return 0
  fi
  return 1
}

# Prefer freshly built outputs from build/android/<abi>, then fallback to staged deps.
copy_lib_prefer_build() {
  local src_libname="$1"
  local staged_root="$2"
  local dst="$NATIVE_LIBS_DST/$src_libname"
  if copy_if_exists "$NATIVE_BUILD_DIR/$src_libname" "$dst"; then
    return 0
  fi
  if copy_if_exists "$staged_root/lib/$ABI/$src_libname" "$dst"; then
    return 0
  fi
  echo "[warn] missing $src_libname in build output and staged deps for ABI=$ABI"
  echo "       checked:"
  echo "       $NATIVE_BUILD_DIR/$src_libname"
  echo "       $staged_root/lib/$ABI/$src_libname"
  return 1
}

echo "[info] SDL is embedded in libplatformer.so -> skipping SDL shared library packaging"

# Optional networking stack for Android (curl + common TLS deps).
curl_enabled=0
if copy_lib_prefer_build "libcurl.so" "$CURL_ANDROID_ROOT"; then
  curl_enabled=1
  copy_lib_prefer_build "libssl.so" "$CURL_ANDROID_ROOT" || true
  copy_lib_prefer_build "libcrypto.so" "$CURL_ANDROID_ROOT" || true
else
  if [ "$ANDROID_ALLOW_NO_CURL" = "1" ]; then
    echo "[warn] libcurl.so not packaged; native HTTP fetch will be disabled (HAVE_CURL=0 build)."
  else
    echo "[error] libcurl.so missing for ABI=$ABI."
    echo "[hint] Provide $CURL_ANDROID_ROOT/lib/$ABI/libcurl.so or build with curl enabled."
    echo "[hint] To bypass (not recommended): ANDROID_ALLOW_NO_CURL=1 ./build/update-android-app.sh"
    exit 1
  fi
fi

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

if [ "$SKIP_ASSETS" != "1" ]; then
  rm -rf "$ASSETS_DST"
  cp -a "$ASSETS_SRC" "$ASSETS_DST"
  echo "[ok] synced assets -> $ASSETS_DST"
fi

echo "[done] android app content updated."
if [ "$curl_enabled" = "1" ]; then
  echo "[info] networking libs packaged: libcurl.so (+ optional libssl/libcrypto)."
else
  echo "[info] networking libs not packaged."
fi
echo "[next] cd Android && ./gradlew assembleDebug"
