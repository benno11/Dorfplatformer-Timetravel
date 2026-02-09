#!/usr/bin/env bash
set -e

if command -v sudo >/dev/null 2>&1; then
  SUDO="sudo"
else
  SUDO=""
fi

SDL2_REQUIRED_VERSION="${SDL2_REQUIRED_VERSION:-2.32.11}"
SDL2_SOURCE_REF="${SDL2_SOURCE_REF:-release-2.32.x}"
AUTO_BUILD_SDL2_FROM_SOURCE="${AUTO_BUILD_SDL2_FROM_SOURCE:-1}"

need_install() {
  ! dpkg -s "$1" >/dev/null 2>&1
}

current_sdl_version() {
  pkg-config --modversion sdl2 2>/dev/null || echo unknown
}

needs_sdl_upgrade() {
  if ! command -v pkg-config >/dev/null 2>&1; then
    echo "[WARN] pkg-config not found; cannot verify SDL2 version."
    return 1
  fi
  if ! pkg-config --exists sdl2; then
    echo "[WARN] SDL2 pkg-config entry not found; cannot verify SDL2 version."
    return 1
  fi
  pkg-config --atleast-version="$SDL2_REQUIRED_VERSION" sdl2
}

check_sdl_version() {
  if needs_sdl_upgrade; then
    echo "[OK] SDL2 version check passed: $(current_sdl_version)"
  else
    echo "[WARN] SDL2 ${SDL2_REQUIRED_VERSION}+ not found. Installed version: $(current_sdl_version)"
  fi
}

build_sdl2_from_source() {
  if ! command -v git >/dev/null 2>&1 || ! command -v cmake >/dev/null 2>&1; then
    echo "[ERROR] git/cmake required to build SDL2 from source."
    return 1
  fi
  local workdir="deps/linux-sdl2-src"
  local builddir="$workdir/build"
  local chosen_ref="$SDL2_SOURCE_REF"
  local remote_ref=""
  local fallback_refs=("release-2.32.x" "release-2.32" "main")

  resolve_remote_ref() {
    local candidate="$1"
    if git ls-remote --exit-code --heads https://github.com/libsdl-org/SDL.git "$candidate" >/dev/null 2>&1; then
      echo "refs/heads/$candidate"
      return 0
    fi
    if git ls-remote --exit-code --tags https://github.com/libsdl-org/SDL.git "$candidate" >/dev/null 2>&1; then
      echo "refs/tags/$candidate"
      return 0
    fi
    return 1
  }

  if remote_ref="$(resolve_remote_ref "$chosen_ref")"; then
    :
  else
    for alt in "${fallback_refs[@]}"; do
      if remote_ref="$(resolve_remote_ref "$alt")"; then
        echo "[WARN] SDL ref '$chosen_ref' not found, using '$alt'"
        chosen_ref="$alt"
        break
      fi
    done
  fi
  if [ -z "$remote_ref" ]; then
    echo "[ERROR] Could not resolve SDL ref '$SDL2_SOURCE_REF' (or fallbacks)."
    return 1
  fi

  echo "[INFO] Building SDL2 from source ($chosen_ref)..."
  mkdir -p deps
  if [ ! -d "$workdir/.git" ]; then
    git clone --depth 1 --branch "$chosen_ref" https://github.com/libsdl-org/SDL.git "$workdir"
  else
    git -C "$workdir" fetch --depth 1 origin "$chosen_ref" || git -C "$workdir" fetch --depth 1 origin "$remote_ref"
    git -C "$workdir" checkout -f "$chosen_ref" || git -C "$workdir" checkout -f "origin/$chosen_ref" || git -C "$workdir" checkout -f FETCH_HEAD
    git -C "$workdir" reset --hard "origin/$chosen_ref" || git -C "$workdir" reset --hard FETCH_HEAD || true
  fi
  cmake -S "$workdir" -B "$builddir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDL_SHARED=ON \
    -DSDL_STATIC=OFF \
    -DSDL_TESTS=OFF \
    -DCMAKE_INSTALL_PREFIX=/usr/local
  cmake --build "$builddir" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
  $SUDO cmake --install "$builddir"
  $SUDO ldconfig || true
}

PACKAGES=(
  build-essential
  cmake
  pkg-config
  libsdl2-dev
  libsdl2-ttf-dev
  libsdl2-image-dev
  libsdl2-mixer-dev
  nlohmann-json3-dev
)

MISSING=()
for pkg in "${PACKAGES[@]}"; do
  if need_install "$pkg"; then
    MISSING+=("$pkg")
  else
    echo "[SKIP] Already installed: $pkg"
  fi
done

if [ ${#MISSING[@]} -eq 0 ]; then
  echo "[OK] All dependencies are already installed."
  if ! needs_sdl_upgrade; then
    echo "[INFO] Attempting to upgrade SDL2 dev packages..."
    $SUDO apt update
    $SUDO apt install -y --only-upgrade \
      libsdl2-dev \
      libsdl2-ttf-dev \
      libsdl2-image-dev \
      libsdl2-mixer-dev || true
    if ! needs_sdl_upgrade && [ "$AUTO_BUILD_SDL2_FROM_SOURCE" = "1" ]; then
      build_sdl2_from_source || true
    fi
  fi
  check_sdl_version
  exit 0
fi

echo "[INFO] Updating package lists..."
$SUDO apt update

echo "[INFO] Installing missing dependencies..."
$SUDO apt install -y "${MISSING[@]}"

echo "[OK] All dependencies installed."
if ! needs_sdl_upgrade; then
  echo "[INFO] Attempting to upgrade SDL2 dev packages..."
  $SUDO apt install -y --only-upgrade \
    libsdl2-dev \
    libsdl2-ttf-dev \
    libsdl2-image-dev \
    libsdl2-mixer-dev || true
  if ! needs_sdl_upgrade && [ "$AUTO_BUILD_SDL2_FROM_SOURCE" = "1" ]; then
    build_sdl2_from_source || true
  fi
fi
check_sdl_version
