#!/usr/bin/env bash
set -euo pipefail

# Android dependency setup for build/android.sh
#
# What this script does:
# 1) Installs host-side build tools (Ubuntu/Debian)
# 2) Verifies Android SDK/NDK environment variables
# 3) Optionally creates a deps layout for SDL Android libs
#
# Usage:
#   ./build/dep-android.sh
#   DOWNLOAD_SDL=1 ./build/dep-android.sh   # prepare deps folder and clone SDL repos
#
# Required for compilation (build/android.sh):
#   ANDROID_NDK_HOME
#   SDL2_ANDROID_ROOT (must contain include/ and lib/<abi>/)

if command -v sudo >/dev/null 2>&1; then
  SUDO="sudo"
else
  SUDO=""
fi

need_install() {
  ! dpkg -s "$1" >/dev/null 2>&1
}

HOST_PACKAGES=(
  build-essential
  clang
  cmake
  ninja-build
  pkg-config
  git
  unzip
  zip
  openjdk-17-jdk
  nlohmann-json3-dev
)

MISSING=()
for pkg in "${HOST_PACKAGES[@]}"; do
  if need_install "$pkg"; then
    MISSING+=("$pkg")
  else
    echo "[SKIP] Already installed: $pkg"
  fi
done

if [ ${#MISSING[@]} -gt 0 ]; then
  echo "[INFO] Updating package lists..."
  $SUDO apt update
  echo "[INFO] Installing missing host packages..."
  $SUDO apt install -y "${MISSING[@]}"
else
  echo "[OK] Host packages already installed."
fi

# Stage full nlohmann include tree locally so Android cross-build does not inject host /usr/include.
mkdir -p third_party
if [ -d /usr/include/nlohmann ]; then
  rm -rf third_party/nlohmann
  cp -a /usr/include/nlohmann third_party/nlohmann
  echo "[OK] Staged third_party/nlohmann/*"
else
  echo "[WARN] /usr/include/nlohmann not found."
  echo "[HINT] Install nlohmann-json3-dev, then rerun this script."
fi

echo "[INFO] Checking Android env..."
if [ -z "${ANDROID_NDK_HOME:-}" ]; then
  echo "[INFO] ANDROID_NDK_HOME not set. Trying auto-detection..."
  NDK_CANDIDATES=(
    "$HOME/Android/Sdk/ndk-bundle"
    "$HOME/Android/Sdk/ndk"
    "$HOME/android-sdk/ndk-bundle"
    "$HOME/android-sdk/ndk"
    "/opt/android-ndk"
  )
  for c in "${NDK_CANDIDATES[@]}"; do
    if [ -d "$c" ]; then
      if [ -x "$c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++" ]; then
        ANDROID_NDK_HOME="$c"
        export ANDROID_NDK_HOME
        break
      fi
      # Handle $SDK/ndk/<version>
      if [ -d "$c" ]; then
        cand="$(find "$c" -maxdepth 2 -type f -path "*/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++" 2>/dev/null | head -n1 || true)"
        if [ -n "$cand" ]; then
          ANDROID_NDK_HOME="$(dirname "$(dirname "$(dirname "$(dirname "$(dirname "$cand")")")")")"
          export ANDROID_NDK_HOME
          break
        fi
      fi
    fi
  done
  if [ -z "${ANDROID_NDK_HOME:-}" ]; then
    echo "[WARN] ANDROID_NDK_HOME is not set."
  else
    echo "[OK] ANDROID_NDK_HOME=$ANDROID_NDK_HOME"
  fi
else
  echo "[OK] ANDROID_NDK_HOME=$ANDROID_NDK_HOME"
fi

if [ -z "${ANDROID_SDK_ROOT:-}" ] && [ -z "${ANDROID_HOME:-}" ]; then
  echo "[INFO] ANDROID_SDK_ROOT/ANDROID_HOME not set. Trying auto-detection..."
  SDK_CANDIDATES=(
    "$HOME/Android/Sdk"
    "$HOME/android-sdk"
    "/opt/android-sdk"
  )
  for c in "${SDK_CANDIDATES[@]}"; do
    if [ -d "$c/platform-tools" ] || [ -d "$c/cmdline-tools" ] || [ -d "$c/tools" ]; then
      ANDROID_SDK_ROOT="$c"
      ANDROID_HOME="$c"
      export ANDROID_SDK_ROOT
      export ANDROID_HOME
      break
    fi
  done
  if [ -z "${ANDROID_SDK_ROOT:-}" ] && [ -z "${ANDROID_HOME:-}" ]; then
    echo "[WARN] ANDROID_SDK_ROOT/ANDROID_HOME not set (needed for full Android app packaging)."
  else
    echo "[OK] ANDROID_SDK_ROOT=${ANDROID_SDK_ROOT:-$ANDROID_HOME}"
  fi
else
  echo "[OK] Android SDK variable is set."
fi

if [ -z "${SDL2_ANDROID_ROOT:-}" ]; then
  echo "[INFO] SDL2_ANDROID_ROOT not set. Trying auto-detection..."
  is_valid_sdl_root() {
    [ -d "$1/include" ] && [ -d "$1/lib" ] && \
      { [ -d "$1/lib/arm64-v8a" ] || [ -d "$1/lib/armeabi-v7a" ] || [ -d "$1/lib/x86_64" ] || [ -d "$1/lib/x86" ]; }
  }

  CANDIDATES=(
    "$PWD/deps/android"
    "$PWD/deps/sdl-android"
    "$PWD/deps/SDL-android"
    "$HOME/Android/SDL2"
    "$HOME/SDL2-android"
    "$HOME/Android/sdl2"
  )

  for c in "${CANDIDATES[@]}"; do
    if is_valid_sdl_root "$c"; then
      SDL2_ANDROID_ROOT="$c"
      export SDL2_ANDROID_ROOT
      echo "[OK] Auto-detected SDL2_ANDROID_ROOT=$SDL2_ANDROID_ROOT"
      break
    fi
  done

  if [ -z "${SDL2_ANDROID_ROOT:-}" ]; then
    echo "[WARN] Could not auto-detect SDL2_ANDROID_ROOT."
  fi
else
  echo "[OK] SDL2_ANDROID_ROOT=$SDL2_ANDROID_ROOT"
fi

if [ "${DOWNLOAD_SDL:-0}" = "1" ]; then
  echo "[INFO] Preparing deps/android-src ..."
  mkdir -p deps/android-src
  cd deps/android-src

  if [ ! -d SDL ]; then
    git clone --depth 1 https://github.com/libsdl-org/SDL.git
  fi
  if [ ! -d SDL_image ]; then
    git clone --depth 1 https://github.com/libsdl-org/SDL_image.git
  fi
  if [ ! -d SDL_ttf ]; then
    git clone --depth 1 https://github.com/libsdl-org/SDL_ttf.git
  fi
  if [ ! -d SDL_mixer ]; then
    git clone --depth 1 https://github.com/libsdl-org/SDL_mixer.git || true
  fi

  cat <<'MSG'
[INFO] SDL source repos cloned.
[NEXT] Build and stage Android libs to a layout like:
  <root>/include/
  <root>/lib/<abi>/
Then export:
  SDL2_ANDROID_ROOT=<root>
  SDL2_IMAGE_ROOT=<root or separate root>
  SDL2_TTF_ROOT=<root or separate root>
  SDL2_MIXER_ROOT=<optional root>
MSG
fi

if [ -n "${SDL2_ANDROID_ROOT:-}" ]; then
  cat > build/android.env <<EOF
export ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-}"
export ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
export ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
export SDL2_ANDROID_ROOT="$SDL2_ANDROID_ROOT"
export SDL2_IMAGE_ROOT="\${SDL2_IMAGE_ROOT:-$SDL2_ANDROID_ROOT}"
export SDL2_TTF_ROOT="\${SDL2_TTF_ROOT:-$SDL2_ANDROID_ROOT}"
export SDL2_MIXER_ROOT="\${SDL2_MIXER_ROOT:-$SDL2_ANDROID_ROOT}"
EOF
  echo "[OK] Wrote build/android.env"
  echo "[INFO] Load it with: source build/android.env"
fi

cat <<'DONE'
[OK] Android dependency setup finished.
You can now run:
  ./build/android.sh
DONE
