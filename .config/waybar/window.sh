#!/usr/bin/env python3
"""Two-line focused-window label for the V4.y bar, fed by Mango IPC."""

from __future__ import annotations

import json
import os
import re
import subprocess
import time
from pathlib import Path

COLORS = Path.home() / ".config/waybar/colors.css"
CACHE = Path(os.environ.get("XDG_RUNTIME_DIR", "/tmp")) / "mango-workspaces.json"
TOKEN = re.compile(r"@define-color\s+([A-Za-z0-9_]+)\s+(#[0-9a-fA-F]{6})")
MAX_TITLE = 22


def palette() -> tuple[str, str]:
    colors = dict(TOKEN.findall(COLORS.read_text(encoding="utf-8")))
    return colors.get("on_surface_variant", "#665c54"), colors.get("on_surface", "#3c3836")


def focused() -> dict:
    try:
        raw = subprocess.check_output(
            ["mmsg", "get", "focusing-client"], text=True, timeout=1, stderr=subprocess.DEVNULL
        )
        data = json.loads(raw)
    except (OSError, subprocess.SubprocessError, json.JSONDecodeError):
        return {}
    return data if isinstance(data, dict) else {}


def active_workspace() -> str:
    try:
        data = json.loads(CACHE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return "?"
    for monitor in data.get("all_tags", []):
        for tag in monitor.get("tags", []):
            if tag.get("is_active"):
                return str(tag.get("index", "?"))
    return "?"


def esc(value: str) -> str:
    return value.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def emit(top: str, bottom: str, tooltip: str) -> None:
    muted, foreground = palette()
    text = (
        f"<span size='7500' foreground='{muted}' rise='-2000'>{esc(top)}</span>\n"
        f"<span size='9000' weight='bold' foreground='{foreground}'>{esc(bottom)}</span>"
    )
    print(
        json.dumps({"text": text, "tooltip": tooltip, "class": "custom-window"}, ensure_ascii=False),
        flush=True,
    )


def snapshot() -> tuple[str, str, str]:
    client = focused()
    title = str(client.get("title") or "")
    if client.get("is_focused") and title:
        app = str(client.get("appid") or "Window")
        short = title if len(title) <= MAX_TITLE else title[: MAX_TITLE - 3] + "..."
        return app, short, f"{app}: {title}"
    workspace = active_workspace()
    return "Desktop", f"Workspace {workspace}", f"Workspace {workspace}"


def main() -> None:
    last = None
    while True:
        current = snapshot()
        if current != last:
            emit(*current)
            last = current
        time.sleep(0.4)


if __name__ == "__main__":
    main()
