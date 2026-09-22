#!/usr/bin/env bash
# Switches the V4.y bar between the full-width bar and the floating dock.
set -euo pipefail

dir="${XDG_CONFIG_HOME:-$HOME/.config}/waybar"
state="$dir/.mode"
mkdir -p "$dir/configs" "$dir/styles"
[[ -f "$state" ]] || printf 'bar\n' > "$state"
mode="$(<"$state")"

if [[ "${1:-}" == toggle ]]; then
  if [[ "$mode" == dock ]]; then
    next=bar
  else
    next=dock
  fi
  cp "$dir/configs/$next.jsonc" "$dir/config.jsonc"
  cp "$dir/styles/$next.css" "$dir/style.css"
  printf '%s\n' "$next" > "$state"
  # Height and margins live in the config, so a style-only reload is not enough.
  pkill -x waybar 2>/dev/null || true
  setsid waybar >/dev/null 2>&1 < /dev/null &
  exit 0
fi

if [[ "$mode" == dock ]]; then
  printf '%s\n' '{"text":"Dock","tooltip":"Switch to bar style"}'
else
  printf '%s\n' '{"text":"Bar","tooltip":"Switch to dock style"}'
fi
