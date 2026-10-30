# Source-system audit (Artix snapshot)

This records the original live Artix/OpenRC system from which the tracked
configuration was collected; it is not a claim that every supported target
uses OpenRC. Normal installation now also supports Arch Linux and CachyOS
(systemd) via `/etc/os-release` IDs `arch` and `cachyos`, while retaining Artix
(`artix`) and its OpenRC service setup.

## Session path

- Distribution: Artix Linux with OpenRC.
- Login manager: SDDM.
- SDDM launches `/usr/share/wayland-sessions/mango.desktop`, whose command is `mango`.
- Mango loads `~/.config/mango/config.conf` and starts the session services directly.
- User OpenRC services provide D-Bus, PipeWire, PipeWire Pulse, and WirePlumber.
- XDG Desktop Portal is D-Bus activated; `mango-portals.conf` routes Wayland
  screen capture to the wlroots backend and keeps GTK for general dialogs.

Mango session startup order:

1. Export the KDE Qt platform theme into the D-Bus activation environment.
2. Publish the declared cursor theme to dconf, GTK2/3/4, Qt, and XSettings with `sync-cursor`.
3. Generate the wallpaper palette, then start Waybar. These are deliberately ordered because `theme-wallpaper` sends Waybar `SIGUSR2`; running both concurrently can terminate Waybar before its signal handler is ready.
4. Start the Mango workspace watcher, mango-notify, the custom OSD server, and the KDE polkit agent.

## Desktop stack

| Area | Implementation |
|---|---|
| Compositor | MangoWM Git build |
| Bar | Waybar with nine custom Mango tag modules and switchable bar/dock profiles |
| Notifications | mango-notify, native C++ / Qt Quick service and controls |
| Launcher | Rofi |
| Terminal and shell | Foot (default), Kitty, Fish, Starship, Fastfetch |
| Browser | Brave Origin stable, launched through `brave-session` |
| File manager | Dolphin |
| Wallpaper | swaybg plus a custom floating Qt gallery |
| Palette | Curated named light/dark themes |
| Audio | PipeWire, WirePlumber, wpctl |
| Network | NetworkManager, nmcli, custom Qt Wi-Fi popup |
| OSD | Custom Qt local-socket server |
| Toolkit integration | KDE color schemes, GTK3/GTK4 CSS, xsettingsd, dynamic Breeze folder icons, centralized cursor propagation |

## Dynamic theme pipeline

`theme-wallpaper` derives a curated theme and dark/light variant from
`~/Pictures/Wallpapers/<Theme>/<Variant>/`. The named renderer writes Mango,
Waybar, Rofi, Kitty, Foot, btop, and LazyVim palettes from one semantic color
definition. Wallpapers outside that hierarchy use Material Dark.
The native notification service reads the shared Rofi palette directly and
reloads through a file watch or `mango-notify --reload`; it needs no CSS template.
`sync-desktop-colors` then writes:

- KDE's `MangoDynamic.colors` and `kdeglobals` state;
- GTK3 and GTK4 named colors and the light/dark preference;
- a recolored Breeze folder icon overlay;
- Fish colors, Starship, and Fastfetch output.

Brave Origin uses the existing KDE palette through its Qt 6 system-theme backend.
Select **Use QT** in browser Appearance once per profile. Open browser windows
may retain the old palette until a page reload or restart; immediate live recoloring
is not guaranteed. The installer sets Origin as the HTML/HTTP/HTTPS default and
backs up the retired managed Beta launcher without removing packages or profiles.

Generated palette files are tracked as a working fallback. Runtime generation happens in the installed home, not in the Git checkout.

## Native utilities

The repository contains source and qmake projects for:

- `network-popup`: Qt Quick NetworkManager frontend;
- `bluetooth-popup`: Qt Quick BlueZ frontend;
- `ppd-popup`: Qt Quick frontend for genuine power-profiles-daemon;
- `battery-popup`: Qt Quick read-only battery information;
- `mango-osd`: Qt Quick volume and brightness OSD with a per-user local socket;
- `mango-notify`: standard D-Bus notification daemon, Qt Quick toasts/history, and event-driven desktop controls;
- `wallpaper-overview`: Qt Quick gallery with cached thumbnails.

`install.sh` rebuilds the native utilities. Prebuilt ELF files are intentionally not tracked.

Wi-Fi, Bluetooth, and power profiles share `popup-ui/PopupWindow.qml`: 180 ms entry from
the right, 140 ms exit to the right, and no vertical motion. Their rules explicitly
set both compositor animation types to `none`, as well as disabling animation:
Mango's unmap snapshot path does not check the per-client `no_animation` flag.
This prevents the inherited vertical close slide after QML finishes. Escape,
focus loss, window close, and socket toggle-close converge on the same QML
dismissal; the process exits after the visual transition completes.

Notifications use one separate dynamic-height, top-right Qt window with a 440 px
width rule, no compositor animation, blur, or shadow, and no fixed height.
Mango starts the daemon; a per-user `org.freedesktop.Notifications` D-Bus service
entry provides activation as well. Waybar streams native JSON status and uses
the control CLI for panel and DND toggles. Legacy `~/.config/swaync` contents are
inactive and left untouched by the installer. It neither removes the SwayNC
package nor stops any live notification daemon.

## Portability findings

- The Wi-Fi popup selects an available NetworkManager interface at runtime; Waybar's network interface filter remains `wl*`.
- The installer detects the first device under `/sys/class/backlight` and rewrites the installed brightness helper.
- Palette generation is implemented with Python's standard library and has no external theme-generator dependency.
- MangoWM requires SceneFX 0.5 with wlroots 0.20. On Artix, the installer uses an installed/repository provider or builds the checksum-pinned release from `system/packages/scenefx/PKGBUILD`; it does not mix in Arch repositories or use the older AUR `scenefx-git` recipe.
- Power profiles now use genuine `power-profiles-daemon` and the packaged `power-profiles-daemon-openrc` service on Artix. Existing `tuned-ppd` providers are replaced in one dependency-checked transaction after stopping and disabling TuneD; no compatibility providers or forced dependency removal are used. The popup uses QtDBus, and Waybar's `custom/ppd` indicator subscribes to PPD property changes through Python/Gio instead of polling TuneD files.
- The curated `Pictures/Wallpapers` gallery and `Pictures/blinders.jpg` are committed and seeded by the installer without replacing existing images. The fallback palette remains usable if all images are absent.
- Originally, package/service automation targeted only Artix with OpenRC; it now selects Artix/OpenRC or Arch/CachyOS/systemd, with `--skip-packages --skip-services` available for config-only installation elsewhere. Mango exports the session environment to D-Bus and to the systemd user manager where present.
- Artix's `pipewire-pulse` and `wireplumber` user services are separate from `pipewire-openrc`; all three packages are listed explicitly.
- `sync-default-apps` creates the target config directory before registering MIME defaults and isolates `xdg-mime` from inherited KDE runtime variables.

## Tracked configuration coverage

Tracked session-relevant state includes:

- Bash and Fish startup;
- Mango, Waybar, mango-notify, Rofi, Kitty, Foot, btop, and LazyVim;
- Starship and Fastfetch, including the Fastfetch image;
- GTK2/3/4 compatibility, KDE globals, Dolphin, cursor/input, Qt compatibility, and xsettingsd;
- a single cursor declaration in the Mango config, fanned out to dconf, GTK2/3/4, Qt, and XSettings by `sync-cursor`;
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
| `~/.config/dconf/user` | Binary application state, not declarative Mango configuration. The cursor keys it holds are written by `sync-cursor` from the Mango config. |
| `~/.config/omp` and `~/.local/bin/omp` | Harness state and a large external executable. |
| Custom utility ELF files | Rebuilt from the tracked source by `install.sh`. |
| Local font files | Replaced by declared repository font packages. |
| `~/.local/share/icons/MangoDynamic-*` | Generated from Breeze and the active named palette. |
| `~/.gtkrc-2.0`, `~/.icons/default/index.theme` | Generated by `sync-cursor` from the Mango cursor declaration. |
| Wallpaper cache and other Pictures | The selected wallpaper cache is runtime state; personal images outside the bundled gallery and default are not tracked. |
| Plasma, LXDE, LXQt, MATE, and Openbox session state | Legacy or alternate-session settings not consumed by MangoWM. |
| `mimeapps.list` | Legacy associations reference several applications no longer installed. |
| Crash reports, shader caches, QML caches, session restore files | Runtime/generated state. |

The install manifest is therefore complete for the active MangoWM desktop while excluding credentials, caches, personal media, stale alternate-session state, and reproducible generated artifacts.
