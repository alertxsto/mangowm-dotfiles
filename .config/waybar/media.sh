#!/usr/bin/env bash
title="$(playerctl metadata title 2>/dev/null || true)"
if [[ -n "$title" ]]; then
  printf '  %.22s\n' "$title"
else
  printf '  No media\n'
fi
