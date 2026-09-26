#!/bin/bash
# Waybar module: tuned profile indicator (icon only, like network/bluetooth)

# TuneD maintains this file when the active profile changes; avoid launching Python every 2s.
profile=""
if [[ -r /etc/tuned/active_profile ]]; then
    IFS= read -r profile < /etc/tuned/active_profile
fi

case "$profile" in
    *latency*) category="latency" ;;
    *performance*|*throughput*|hpc*) category="performance" ;;
    *powersave*|spindown*) category="power-saver" ;;
    balanced*|desktop|default) category="balanced" ;;
    "") category="unavailable" ;;
    *) category="balanced" ;;
esac

icon="󰓅"
tooltip="Power profile: ${profile:-none}"
printf '{"text": "<span size='"'"'11500'"'"'>%s</span>", "tooltip": "%s", "class": "%s"}\n' \
    "$icon" "$tooltip" "$category"
