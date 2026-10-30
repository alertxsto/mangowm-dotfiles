#!/usr/bin/env python3
"""Waybar power profile indicator driven by native PPD D-Bus changes."""

import json

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

SERVICE = "net.hadess.PowerProfiles"
PATH = "/net/hadess/PowerProfiles"
last_state = None


def emit(proxy=None, *_args):
    global last_state
    profile = None
    degraded = None
    if proxy is not None and proxy.get_name_owner():
        profile = proxy.get_cached_property("ActiveProfile")
        degraded = proxy.get_cached_property("PerformanceDegraded")
    name = profile.unpack() if profile is not None else ""
    reason = degraded.unpack() if degraded is not None else ""
    tooltip = f"Power profile: {name}" if name else "Power profiles unavailable"
    if name == "performance" and reason:
        tooltip += f"\nPerformance limited: {reason}"
    state = {
        "text": "<span size='11500'>󰓅</span>",
        "tooltip": tooltip,
        "class": name or "unavailable",
    }
    if state != last_state:
        print(json.dumps(state, ensure_ascii=False), flush=True)
        last_state = state


def main():
    try:
        proxy = Gio.DBusProxy.new_for_bus_sync(
            Gio.BusType.SYSTEM,
            Gio.DBusProxyFlags.DO_NOT_AUTO_START,
            None,
            SERVICE,
            PATH,
            SERVICE,
            None,
        )
    except GLib.Error:
        emit()
        return
    proxy.connect("g-properties-changed", emit)
    proxy.connect("notify::g-name-owner", emit)
    emit(proxy)
    GLib.MainLoop().run()


if __name__ == "__main__":
    main()
