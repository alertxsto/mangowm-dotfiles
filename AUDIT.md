# Current system audit

## Session path

- Distribution: Artix Linux with OpenRC.
- Login manager: SDDM.
- SDDM launches `/usr/share/wayland-sessions/mango.desktop`, whose command is `mango`.
- Mango loads `~/.config/mango/config.conf` and starts the session services directly.
- User OpenRC services provide D-Bus, PipeWire, PipeWire Pulse, and WirePlumber.
- XDG Desktop Portal and its GTK backend are D-Bus activated.

Mango session startup order:

1. Export the KDE Qt platform theme into the D-Bus activation environment.
2. Generate the wallpaper palette, then start Waybar. These are deliberately ordered because `theme-wallpaper` sends Waybar `SIGUSR2`; running both concurrently can terminate Waybar before its signal handler is ready.
3. Start the Mango workspace watcher, SwayNC, the custom OSD server, and the KDE polkit agent.

## Desktop stack

| Area | Implementation |
|---|---|
| Compositor | MangoWM Git build |
| Bar | Waybar with nine custom Mango tag modules |
| Notifications | SwayNC |
| Launcher | Rofi |
| Terminal and shell | Kitty, Fish, Starship, Fastfetch |
| File manager | Dolphin |
| Wallpaper | swaybg plus a custom full-screen Qt gallery |
| Palette | Matugen SchemeSmart |
| Audio | PipeWire, WirePlumber, wpctl |
| Network | NetworkManager, nmcli, custom Qt Wi-Fi popup |
| OSD | Custom Qt local-socket server |
| Toolkit integration | KDE color schemes, GTK3/GTK4 CSS, xsettingsd, dynamic Breeze folder icons |

## Dynamic theme pipeline

`theme-wallpaper` selects a wallpaper and runs Matugen. Matugen writes palettes for Mango, Waybar, SwayNC, Kitty, and Rofi. `sync-desktop-colors` then writes:

- KDE's `MangoDynamic.colors` and `kdeglobals` state;
- GTK3 and GTK4 named colors;
- a recolored Breeze folder icon overlay;
- Fish colors, Starship, and Fastfetch output.

Generated palette files are tracked as a working fallback. Runtime generation happens in the installed home, not in the Git checkout.

## Native utilities

The repository contains source and qmake projects for:

- `network-popup`: Qt Quick NetworkManager frontend;
- `mango-osd`: Qt Quick volume and brightness OSD with a per-user local socket;
- `wallpaper-overview`: Qt Quick gallery with cached thumbnails.

`install.sh` rebuilds all three. Prebuilt ELF files are intentionally not tracked.

## Portability findings

- The live system uses Wi-Fi interface `wlan0`. The installer detects the first NetworkManager Wi-Fi interface and rewrites the installed Waybar and popup source before compilation.
- The live system uses backlight device `amdgpu_bl2`. The installer detects `/sys/class/backlight` and rewrites the installed brightness helper.
- Matugen and SwayNC had absolute `/home/alertxist` paths. Repository templates use `__HOME__`; the installer renders the selected target home.
- The original theme helper invoked a private `~/.local/bin/matugen` binary. The repository invokes packaged `matugen` from `PATH`.
- No wallpaper is committed. If `~/Pictures/blinders.jpg`, the remembered wallpaper, and all Pictures images are absent, the bundled fallback palette remains usable.
- The package/service automation intentionally targets Artix Linux and OpenRC. `--skip-packages --skip-services` allows config-only installation elsewhere.

## Tracked configuration coverage

Tracked session-relevant state includes:

- Bash and Fish startup;
- Mango, Waybar, SwayNC, Rofi, Kitty, and Matugen;
- Starship and Fastfetch, including the Fastfetch image;
- GTK2/3/4 compatibility, KDE globals, Dolphin, cursor/input, Qt compatibility, and xsettingsd;
- fontconfig and Waybar icon fallback aliases;
- locale and XDG user-directory mapping;
- custom shell/Python helpers;
- custom Qt sources and fallback generated color files.

## Intentional exclusions

These are excluded rather than overlooked:

| Path/category | Reason |
|---|---|
| `~/.config/gh/hosts.yml` | Contains GitHub authentication material. |
| Browser profiles | Credentials, cookies, history, extension state, and large caches. |
| `~/.config/pulse/cookie` | Authentication cookie. |
| `~/.config/dconf/user` | Binary application state, not declarative Mango configuration. |
| `~/.config/omp` and `~/.local/bin/omp` | Harness state and a large external executable. |
| `~/.local/bin/matugen` | External prebuilt binary; installed from `matugen-bin` instead. |
| Custom utility ELF files | Rebuilt from the tracked source by `install.sh`. |
| Local font files | Replaced by declared repository font packages. |
| `~/.local/share/icons/MangoDynamic-*` | Generated from Breeze and the active Matugen palette. |
| Wallpaper cache and Pictures | Personal media and runtime state. |
| Plasma, LXDE, LXQt, MATE, and Openbox session state | Legacy or alternate-session settings not consumed by MangoWM. |
| `mimeapps.list` | Legacy associations reference several applications no longer installed. |
| Crash reports, shader caches, QML caches, session restore files | Runtime/generated state. |

The install manifest is therefore complete for the active MangoWM desktop while excluding credentials, caches, personal media, stale alternate-session state, and reproducible generated artifacts.
