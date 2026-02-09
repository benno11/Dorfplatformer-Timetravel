#!/usr/bin/env bash
set -e

if command -v sudo >/dev/null 2>&1; then
  SUDO="sudo"
else
  SUDO=""
fi

SDL3_REQUIRED_VERSION="${SDL3_REQUIRED_VERSION:-3.4.0}"
SDL3_SOURCE_REF="${SDL3_SOURCE_REF:-release-3.4.x}"
SDL3_IMAGE_SOURCE_REF="${SDL3_IMAGE_SOURCE_REF:-main}"
SDL3_TTF_SOURCE_REF="${SDL3_TTF_SOURCE_REF:-main}"
SDL3_MIXER_SOURCE_REF="${SDL3_MIXER_SOURCE_REF:-main}"
AUTO_BUILD_SDL3_FROM_SOURCE="${AUTO_BUILD_SDL3_FROM_SOURCE:-1}"

need_install() {
  ! dpkg -s "$1" >/dev/null 2>&1
}

current_sdl_version() {
  pkg-config --modversion sdl3 2>/dev/null || echo unknown
}

needs_sdl_upgrade() {
  if ! command -v pkg-config >/dev/null 2>&1; then
    echo "[WARN] pkg-config not found; cannot verify sdl3 version."
    return 1
  fi
  if ! pkg-config --exists sdl3; then
    echo "[WARN] sdl3 pkg-config entry not found; cannot verify sdl3 version."
    return 1
  fi
  pkg-config --atleast-version="$SDL3_REQUIRED_VERSION" sdl3
}

check_sdl_version() {
  if needs_sdl_upgrade; then
    echo "[OK] sdl3 version check passed: $(current_sdl_version)"
  else
    echo "[WARN] sdl3 ${SDL3_REQUIRED_VERSION}+ not found. Installed version: $(current_sdl_version)"
  fi
}

build_sdl3_from_source() {
  if ! command -v git >/dev/null 2>&1 || ! command -v cmake >/dev/null 2>&1; then
    echo "[ERROR] git/cmake required to build sdl3 from source."
    return 1
  fi
  local workdir="deps/linux-sdl3-src"
  local builddir="$workdir/build"
  local chosen_ref="$SDL3_SOURCE_REF"
  local remote_ref=""
  local fallback_refs=("release-3.4.x" "release-3.4" "main")

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
    echo "[ERROR] Could not resolve SDL ref '$SDL3_SOURCE_REF' (or fallbacks)."
    return 1
  fi

  echo "[INFO] Building sdl3 from source ($chosen_ref)..."
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

build_sdl3_image_from_source() {
  if ! command -v git >/dev/null 2>&1 || ! command -v cmake >/dev/null 2>&1; then
    echo "[ERROR] git/cmake required to build sdl3_image from source."
    return 1
  fi
  local workdir="deps/linux-sdl3-image-src"
  local builddir="$workdir/build"
  local chosen_ref="$SDL3_IMAGE_SOURCE_REF"
  local remote_ref=""
  local fallback_refs=("main")

  resolve_remote_ref_image() {
    local candidate="$1"
    if git ls-remote --exit-code --heads https://github.com/libsdl-org/SDL_image.git "$candidate" >/dev/null 2>&1; then
      echo "refs/heads/$candidate"
      return 0
    fi
    if git ls-remote --exit-code --tags https://github.com/libsdl-org/SDL_image.git "$candidate" >/dev/null 2>&1; then
      echo "refs/tags/$candidate"
      return 0
    fi
    return 1
  }

  if remote_ref="$(resolve_remote_ref_image "$chosen_ref")"; then
    :
  else
    for alt in "${fallback_refs[@]}"; do
      if remote_ref="$(resolve_remote_ref_image "$alt")"; then
        echo "[WARN] SDL_image ref '$chosen_ref' not found, using '$alt'"
        chosen_ref="$alt"
        break
      fi
    done
  fi
  if [ -z "$remote_ref" ]; then
    echo "[ERROR] Could not resolve SDL_image ref '$SDL3_IMAGE_SOURCE_REF' (or fallbacks)."
    return 1
  fi

  echo "[INFO] Building sdl3_image from source ($chosen_ref)..."
  mkdir -p deps
  if [ ! -d "$workdir/.git" ]; then
    git clone --depth 1 --branch "$chosen_ref" https://github.com/libsdl-org/SDL_image.git "$workdir"
  else
    git -C "$workdir" fetch --depth 1 origin "$chosen_ref" || git -C "$workdir" fetch --depth 1 origin "$remote_ref"
    git -C "$workdir" checkout -f FETCH_HEAD
    git -C "$workdir" reset --hard FETCH_HEAD
  fi
  cmake -S "$workdir" -B "$builddir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDL3IMAGE_SAMPLES=OFF \
    -DSDL3IMAGE_VENDORED=ON \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DSDL3_DIR=/usr/local/lib/cmake/SDL3
  cmake --build "$builddir" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
  $SUDO cmake --install "$builddir"
  $SUDO ldconfig || true
}

build_sdl3_ttf_from_source() {
  if ! command -v git >/dev/null 2>&1 || ! command -v cmake >/dev/null 2>&1; then
    echo "[ERROR] git/cmake required to build sdl3_ttf from source."
    return 1
  fi
  local workdir="deps/linux-sdl3-ttf-src"
  local builddir="$workdir/build"
  local chosen_ref="$SDL3_TTF_SOURCE_REF"
  local remote_ref=""
  local fallback_refs=("main")

  resolve_remote_ref_ttf() {
    local candidate="$1"
    if git ls-remote --exit-code --heads https://github.com/libsdl-org/SDL_ttf.git "$candidate" >/dev/null 2>&1; then
      echo "refs/heads/$candidate"
      return 0
    fi
    if git ls-remote --exit-code --tags https://github.com/libsdl-org/SDL_ttf.git "$candidate" >/dev/null 2>&1; then
      echo "refs/tags/$candidate"
      return 0
    fi
    return 1
  }

  if remote_ref="$(resolve_remote_ref_ttf "$chosen_ref")"; then
    :
  else
    for alt in "${fallback_refs[@]}"; do
      if remote_ref="$(resolve_remote_ref_ttf "$alt")"; then
        echo "[WARN] SDL_ttf ref '$chosen_ref' not found, using '$alt'"
        chosen_ref="$alt"
        break
      fi
    done
  fi
  if [ -z "$remote_ref" ]; then
    echo "[ERROR] Could not resolve SDL_ttf ref '$SDL3_TTF_SOURCE_REF' (or fallbacks)."
    return 1
  fi

  echo "[INFO] Building sdl3_ttf from source ($chosen_ref)..."
  mkdir -p deps
  if [ ! -d "$workdir/.git" ]; then
    git clone --depth 1 --branch "$chosen_ref" https://github.com/libsdl-org/SDL_ttf.git "$workdir"
  else
    git -C "$workdir" fetch --depth 1 origin "$chosen_ref" || git -C "$workdir" fetch --depth 1 origin "$remote_ref"
    git -C "$workdir" checkout -f FETCH_HEAD
    git -C "$workdir" reset --hard FETCH_HEAD
  fi
  cmake -S "$workdir" -B "$builddir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDL3TTF_SAMPLES=OFF \
    -DSDL3TTF_VENDORED=ON \
    -DSDL3TTF_HARFBUZZ=OFF \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DSDL3_DIR=/usr/local/lib/cmake/SDL3
  cmake --build "$builddir" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
  $SUDO cmake --install "$builddir"
  $SUDO ldconfig || true
}

build_sdl3_mixer_from_source() {
  if ! command -v git >/dev/null 2>&1 || ! command -v cmake >/dev/null 2>&1; then
    echo "[ERROR] git/cmake required to build sdl3_mixer from source."
    return 1
  fi
  local workdir="deps/linux-sdl3-mixer-src"
  local builddir="$workdir/build"
  local chosen_ref="$SDL3_MIXER_SOURCE_REF"
  local remote_ref=""
  local fallback_refs=("main")

  resolve_remote_ref_mixer() {
    local candidate="$1"
    if git ls-remote --exit-code --heads https://github.com/libsdl-org/SDL_mixer.git "$candidate" >/dev/null 2>&1; then
      echo "refs/heads/$candidate"
      return 0
    fi
    if git ls-remote --exit-code --tags https://github.com/libsdl-org/SDL_mixer.git "$candidate" >/dev/null 2>&1; then
      echo "refs/tags/$candidate"
      return 0
    fi
    return 1
  }

  if remote_ref="$(resolve_remote_ref_mixer "$chosen_ref")"; then
    :
  else
    for alt in "${fallback_refs[@]}"; do
      if remote_ref="$(resolve_remote_ref_mixer "$alt")"; then
        echo "[WARN] SDL_mixer ref '$chosen_ref' not found, using '$alt'"
        chosen_ref="$alt"
        break
      fi
    done
  fi
  if [ -z "$remote_ref" ]; then
    echo "[ERROR] Could not resolve SDL_mixer ref '$SDL3_MIXER_SOURCE_REF' (or fallbacks)."
    return 1
  fi

  echo "[INFO] Building sdl3_mixer from source ($chosen_ref)..."
  mkdir -p deps
  if [ ! -d "$workdir/.git" ]; then
    git clone --depth 1 --branch "$chosen_ref" https://github.com/libsdl-org/SDL_mixer.git "$workdir"
  else
    git -C "$workdir" fetch --depth 1 origin "$chosen_ref" || git -C "$workdir" fetch --depth 1 origin "$remote_ref"
    git -C "$workdir" checkout -f FETCH_HEAD
    git -C "$workdir" reset --hard FETCH_HEAD
  fi
  git -C "$workdir" submodule sync --recursive
  git -C "$workdir" submodule update --init --recursive --depth 1 --jobs "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
  cmake -S "$workdir" -B "$builddir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDLMIXER_VENDORED=ON \
    -DSDLMIXER_SAMPLES=OFF \
    -DSDLMIXER_EXAMPLES=OFF \
    -DSDLMIXER_TESTS=OFF \
    -DSDLMIXER_MOD=OFF \
    -DSDLMIXER_MIDI=OFF \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DSDL3_DIR=/usr/local/lib/cmake/SDL3
  cmake --build "$builddir" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
  $SUDO cmake --install "$builddir"
  $SUDO ldconfig || true
}

build_all_sdl_from_source() {
  build_sdl3_from_source || return 1
  build_sdl3_image_from_source || return 1
  build_sdl3_ttf_from_source || return 1
  build_sdl3_mixer_from_source || return 1
}

PACKAGES=(
  build-essential
  cmake
  pkg-config
  libsdl3-dev
  libsdl3-ttf-dev
  libsdl3-image-dev
  libsdl3-mixer-dev
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
    echo "[INFO] Attempting to upgrade sdl3 dev packages..."
    $SUDO apt update || true
    $SUDO apt install -y --only-upgrade \
      libsdl3-dev \
      libsdl3-ttf-dev \
      libsdl3-image-dev \
      libsdl3-mixer-dev || true
    if ! needs_sdl_upgrade && [ "$AUTO_BUILD_SDL3_FROM_SOURCE" = "1" ]; then
      build_all_sdl_from_source || true
    fi
  fi
  check_sdl_version
  exit 0
fi

echo "[INFO] Updating package lists..."
$SUDO apt update

echo "[INFO] Installing missing dependencies..."
if ! $SUDO apt install -y "${MISSING[@]}"; then
  echo "[WARN] apt could not install one or more packages."
  if [ "$AUTO_BUILD_SDL3_FROM_SOURCE" = "1" ]; then
    echo "[INFO] Continuing with SDL3 source build fallback."
    build_all_sdl_from_source || true
    check_sdl_version
    exit 0
  fi
  exit 1
fi

echo "[OK] All dependencies installed."
if ! needs_sdl_upgrade; then
  echo "[INFO] Attempting to upgrade sdl3 dev packages..."
  $SUDO apt install -y --only-upgrade \
    libsdl3-dev \
    libsdl3-ttf-dev \
    libsdl3-image-dev \
    libsdl3-mixer-dev || true
  if ! needs_sdl_upgrade && [ "$AUTO_BUILD_SDL3_FROM_SOURCE" = "1" ]; then
    build_all_sdl_from_source || true
  fi
fi
check_sdl_version
