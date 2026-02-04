#!/usr/bin/env bash
set -e

echo "[INFO] Updating package lists..."
sudo apt update

echo "[INFO] Installing build tools..."
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config

echo "[INFO] Installing SDL2..."
sudo apt install -y \
  libsdl2-dev

echo "[INFO] Installing JSON library..."
sudo apt install -y \
  nlohmann-json3-dev

echo "[OK] All dependencies installed."

