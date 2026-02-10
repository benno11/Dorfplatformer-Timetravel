#!/usr/bin/env bash
set -euo pipefail

# Install libcurl development headers for this project.
# Supports: apt, dnf, yum, pacman, zypper, apk, brew

if command -v sudo >/dev/null 2>&1; then
  SUDO="sudo"
else
  SUDO=""
fi

log() { echo "[INFO] $*"; }
err() { echo "[ERROR] $*" >&2; }

install_with_apt() {
  $SUDO apt-get update
  $SUDO apt-get install -y libcurl4-openssl-dev pkg-config
}

install_with_dnf() {
  $SUDO dnf install -y libcurl-devel pkgconf-pkg-config
}

install_with_yum() {
  $SUDO yum install -y libcurl-devel pkgconfig
}

install_with_pacman() {
  $SUDO pacman -Sy --noconfirm curl pkgconf
}

install_with_zypper() {
  $SUDO zypper --non-interactive install libcurl-devel pkg-config
}

install_with_apk() {
  $SUDO apk add --no-cache curl-dev pkgconf
}

install_with_brew() {
  brew install curl pkg-config
}

verify() {
  if ! command -v pkg-config >/dev/null 2>&1; then
    err "pkg-config not found after install."
    exit 1
  fi
  if pkg-config --exists libcurl; then
    log "libcurl detected: $(pkg-config --modversion libcurl)"
    log "Install complete."
  else
    err "libcurl pkg-config entry not found after install."
    exit 1
  fi
}

main() {
  log "Installing libcurl development package..."

  if command -v apt-get >/dev/null 2>&1; then
    install_with_apt
  elif command -v dnf >/dev/null 2>&1; then
    install_with_dnf
  elif command -v yum >/dev/null 2>&1; then
    install_with_yum
  elif command -v pacman >/dev/null 2>&1; then
    install_with_pacman
  elif command -v zypper >/dev/null 2>&1; then
    install_with_zypper
  elif command -v apk >/dev/null 2>&1; then
    install_with_apk
  elif command -v brew >/dev/null 2>&1; then
    install_with_brew
  else
    err "Unsupported package manager. Install libcurl dev headers manually."
    err "Need pkg-config package name 'libcurl' to resolve."
    exit 1
  fi

  verify
}

main "$@"
