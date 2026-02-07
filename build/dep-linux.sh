#!/usr/bin/env bash
set -e

if command -v sudo >/dev/null 2>&1; then
  SUDO="sudo"
else
  SUDO=""
fi

need_install() {
  ! dpkg -s "$1" >/dev/null 2>&1
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
  exit 0
fi

echo "[INFO] Updating package lists..."
$SUDO apt update

echo "[INFO] Installing missing dependencies..."
$SUDO apt install -y "${MISSING[@]}"

echo "[OK] All dependencies installed."
