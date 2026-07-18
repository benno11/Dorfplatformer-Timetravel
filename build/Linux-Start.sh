#!/usr/bin/env bash
set -euo pipefail

# Launch the Linux build from the repository root no matter where this script is
# invoked from. The previous file contained developer-local absolute paths,
# which made packaged/Linux startup fail on other machines.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
APP="$ROOT_DIR/platformer"
LOG_DIR="$ROOT_DIR/build"
LOG_FILE="$LOG_DIR/startup.log"

mkdir -p "$LOG_DIR"
cd "$ROOT_DIR"

if [ ! -x "$APP" ]; then
  echo "[INFO] platformer binary not found; building Linux version..." | tee -a "$LOG_FILE"
  "$SCRIPT_DIR/linux.sh" build 2>&1 | tee -a "$LOG_FILE"
fi

if [ ! -x "$APP" ]; then
  echo "[ERROR] Linux platformer binary is missing or not executable: $APP" | tee -a "$LOG_FILE" >&2
  exit 1
fi

echo "[INFO] Starting $APP" >> "$LOG_FILE"
exec "$APP" "$@"
