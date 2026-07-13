#!/bin/sh
# SpectraCtrl GUI launcher - logs to ~/.cache/spectractl-gui.log
DIR="$(dirname "$(readlink -f "$0")")"
LOG="$HOME/.cache/spectractl-gui.log"
{
  echo "=== launch $(date) ==="
  cd "$DIR" || exit 1
  if [ ! -x node_modules/electron/dist/electron ]; then
    echo "electron binary missing - running npm install"
    npm install
  fi
  exec ./node_modules/electron/dist/electron .
} >> "$LOG" 2>&1
