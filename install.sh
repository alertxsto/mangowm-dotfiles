#!/usr/bin/env bash
set -Eeuo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
target_home="$HOME"
install_packages=1
enable_services=1
build_native=1
assume_yes=0
native_projects=(
  network-popup bluetooth-popup ppd-popup battery-popup mango-osd mango-notify wallpaper-overview
)

usage() {
  cat <<'EOF'
Usage: ./install.sh [options]

  --target-home PATH  Install into PATH instead of $HOME
  --skip-packages     Do not install pacman/AUR packages
  --skip-services     Do not enable system services
  --skip-build        Do not build the native Qt utilities
  --yes               Use selected steps without interactive questions
  -h, --help          Show this help

Interactive keys: Up/Down or j/k to move, Space to toggle, Enter to review;
                  y to start, e to edit, q to cancel.
EOF
}

die() {
  printf 'install.sh: %s\n' "$*" >&2
  exit 1
}

accent='' reset='' base='' muted='' focus='' warn='' brand=''
if [[ -t 1 && -z "${NO_COLOR:-}" && "${TERM:-}" != dumb ]]; then
  accent=$'\033[38;2;142;219;206m'
  reset=$'\033[0m'
  base=$'\033[48;2;20;24;32m\033[38;2;235;240;248m'
  muted=$'\033[38;2;168;180;198m'
  focus=$'\033[48;2;57;66;85m\033[38;2;235;240;248m'
  warn=$'\033[38;2;255;202;135m'
  brand=$'\033[38;2;190;165;244m'
fi

step() {
  printf '\n%s> %s%s\n' "$accent" "$1" "$reset"
}

tui_active=0
leave_tui() {
  if ((tui_active)); then
    printf '%s\033[?25h\033[?1049l' "$reset"
    tui_active=0
  fi
}

draw_brand() {
  if ((term_cols >= 118 && term_rows >= 26)); then
    printf '%s' "$brand"
    cat <<'BANNER'
     ▄████████  ▄█          ▄████████    ▄████████     ███     ▀████    ▐████▀    ▄████████     ███      ▄██████▄
    ███    ███ ███         ███    ███   ███    ███ ▀█████████▄   ███▌   ████▀    ███    ███ ▀█████████▄ ███    ███
    ███    ███ ███         ███    █▀    ███    ███    ▀███▀▀██    ███  ▐███      ███    █▀     ▀███▀▀██ ███    ███
    ███    ███ ███        ▄███▄▄▄      ▄███▄▄▄▄██▀     ███   ▀    ▀███▄███▀      ███            ███   ▀ ███    ███
  ▀███████████ ███       ▀▀███▀▀▀     ▀▀███▀▀▀▀▀       ███        ████▀██▄     ▀███████████     ███     ███    ███
    ███    ███ ███         ███    █▄  ▀███████████     ███       ▐███  ▀███             ███     ███     ███    ███
    ███    ███ ███▌    ▄   ███    ███   ███    ███     ███      ▄███     ███▄     ▄█    ███     ███     ███    ███
    ███    █▀  █████▄▄██   ██████████   ███    ███    ▄████▀   ████       ███▄  ▄████████▀     ▄████▀    ▀██████▀
               ▀                        ███    ███
BANNER
  else
    printf '  %salertxsto%s  /  MANGO DOTFILES\n' "$brand" "$base"
    if ((term_cols < 118)); then
      printf '  %sWiden to 118 columns for the full wordmark.%s\n' "$muted" "$base"
    else
      printf '  %sGrow to 26 rows for the full wordmark.%s\n' "$muted" "$base"
    fi
  fi
  printf '%s' "$base"
}

option_row() {
  local index="$1" label="$2" detail="$3" value="$4" locked="$5"
  local pointer=' ' mark='[ ]' style="$base"
  ((value)) && mark='[x]'
  if ((locked)); then
    mark='[-]'
    style="$muted"
  fi
  if ((cursor == index)); then
    pointer='>'
    style="$focus"
  fi
  printf '  %s %s  %s%s%s\n' "$pointer" "$mark" "$style" "$label" "$base"
  if ((term_cols >= 70)); then
    printf '         %s%s%s\n' "$muted" "$detail" "$base"
  fi
}

draw_tui() {
  local screen="$1" target_label="$target_home"
  read -r term_rows term_cols < <(stty size < /dev/tty)
  [[ "$term_rows" =~ ^[1-9][0-9]*$ && "$term_cols" =~ ^[1-9][0-9]*$ ]] || {
    term_rows=24
    term_cols=80
  }
  if ((${#target_label} > term_cols - 13 && term_cols > 30)); then
    target_label="...${target_label: -$((term_cols - 18))}"
  fi

  printf '\033[H%s\033[2J' "$base"
  draw_brand
  printf '\n  %sSETUP  /  %s%s\n' "$accent" "${distro_id:-unknown}" "$base"
  printf '  %sTarget%s  %s\n' "$muted" "$base" "$target_label"
  if [[ "$screen" == select ]]; then
    printf '\n  Choose components  %s(dotfiles always installed)%s\n\n' "$muted" "$base"
    option_row 0 'Packages' "$package_detail" "$install_packages" "$package_locked"
    option_row 1 'Qt utilities' "$build_detail" "$build_native" "$build_locked"
    option_row 2 'Login services' "$service_detail" "$enable_services" "$service_locked"
    printf '\n  %sUP/DOWN or j/k: move    SPACE: toggle    ENTER: review%s\n' "$accent" "$base"
    printf '  %sq: cancel%s\n' "$muted" "$base"
  else
    printf '\n  Review your plan\n\n'
    printf '  Dotfiles       install with backups\n'
    printf '  Packages       %s\n' "$package_action"
    printf '  Qt utilities   %s\n' "$build_action"
    printf '  Login services %s\n' "$service_action"
    printf '\n  %sChanging packages/services may ask for sudo.%s\n' "$warn" "$base"
    printf '\n  %sy: start    e: edit selection    q: cancel%s\n' "$accent" "$base"
  fi
}

read_tui_key() {
  local key suffix=''
  if ! IFS= read -rsn1 key < /dev/tty; then
    leave_tui
    die 'terminal input closed; installation cancelled'
  fi
  if [[ "$key" == $'\033' ]]; then
    IFS= read -rsn2 -t 0.2 suffix < /dev/tty || true
    case "$suffix" in
      '[A') key=k ;;
      '[B') key=j ;;
      *) key='' ;;
    esac
  fi
  REPLY="$key"
}

select_install_steps() {
  local cursor=0 screen=select term_rows=24 term_cols=80
  local package_detail='pacman + AUR via yay' build_detail="${#native_projects[@]} local Qt tools"
  local service_detail='systemd/OpenRC system and user units'
  local package_locked=0 build_locked=0 service_locked=0
  local package_action build_action service_action
  ((install_packages)) || { package_locked=1; package_detail='Disabled by --skip-packages'; }
  ((build_native)) || { build_locked=1; build_detail='Disabled by --skip-build'; }
  ((enable_services)) || { service_locked=1; service_detail='Disabled by --skip-services'; }
  if [[ "$target_home" != "$HOME" ]]; then
    enable_services=0
    service_locked=1
    service_detail='Unavailable for a custom target home'
  fi
  case "$distro_id" in
    artix|arch|cachyos) ;;
    *)
      install_packages=0
      enable_services=0
      package_locked=1
      service_locked=1
      package_detail='Unsupported distro: config-only install'
      service_detail='Unsupported distro: config-only install'
      ;;
  esac

  printf '\033[?1049h\033[?25l'
  tui_active=1
  trap 'leave_tui' EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  while :; do
    package_action='skip' build_action='skip' service_action='skip'
    ((install_packages)) && package_action='install'
    ((build_native)) && build_action='build'
    ((enable_services)) && service_action='enable'
    draw_tui "$screen"
    read_tui_key
    if [[ "$screen" == select ]]; then
      case "$REPLY" in
        k) cursor=$(((cursor + 2) % 3)) ;;
        j) cursor=$(((cursor + 1) % 3)) ;;
        ' ')
          case "$cursor" in
            0) ((package_locked)) || install_packages=$((1 - install_packages)) ;;
            1) ((build_locked)) || build_native=$((1 - build_native)) ;;
            2) ((service_locked)) || enable_services=$((1 - enable_services)) ;;
          esac
          ;;
        '') screen=review ;;
        q|Q) leave_tui; printf 'Installation cancelled; no changes made.\n'; exit 0 ;;
      esac
    else
      case "$REPLY" in
        y|Y) leave_tui; trap - EXIT INT TERM; return ;;
        e|E) screen=select ;;
        q|Q) leave_tui; printf 'Installation cancelled; no changes made.\n'; exit 0 ;;
      esac
    fi
  done
}

while (($#)); do
  case "$1" in
    --target-home)
      (($# >= 2)) || die '--target-home requires a path'
      target_home="$(realpath -m -- "$2")"
      shift 2
      ;;
    --skip-packages)
      install_packages=0
      shift
      ;;
    --skip-services)
      enable_services=0
      shift
      ;;
    --skip-build)
      build_native=0
      shift
      ;;
    --yes)
      assume_yes=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
done

((EUID != 0)) || die 'run this script as a regular user; it invokes sudo when required'

# Allow a config-only install on other distributions. Selection happens before
# validation so a terminal user can opt out of system-level changes.
distro_id=""
if [[ -r /etc/os-release ]]; then
  distro_id="$(. /etc/os-release; printf '%s' "$ID")"
fi

if (( !assume_yes )) && [[ -t 0 && -t 1 ]]; then
  [[ "${TERM:-}" != dumb ]] || die 'an interactive terminal is required (set TERM correctly or use --yes)'
  select_install_steps
fi

if ((install_packages || enable_services)); then
  [[ -n "$distro_id" ]] || die 'cannot identify the operating system'
  case "$distro_id" in
    artix|arch|cachyos) ;;
    *) die "unsupported operating system: $distro_id (use --skip-packages --skip-services for a dotfiles-only install)" ;;
  esac
fi
if ((enable_services)); then
  [[ "$target_home" == "$HOME" ]] || die '--skip-services is required with a custom --target-home'
fi

package_action='skip' build_action='skip' service_action='skip'
((install_packages)) && package_action='install'
((build_native)) && build_action='build'
((enable_services)) && service_action='enable'
printf '\nPlan for %s (%s):\n' "$target_home" "${distro_id:-config only}"
printf '  Packages: %s\n  Qt build: %s\n  Services: %s\n  Dotfiles: install with backups\n' \
  "$package_action" "$build_action" "$service_action"
mkdir -p -- "$target_home"

bootstrap_yay() {
  command -v sudo >/dev/null || die 'sudo is required to install packages'
  sudo pacman -S --needed --noconfirm git base-devel

  local build_dir
  build_dir="$(mktemp -d)"
  # A failed AUR bootstrap may leave this temporary directory for inspection.
  git clone --depth 1 https://aur.archlinux.org/yay.git "$build_dir/yay"
  (
    cd -- "$build_dir/yay"
    makepkg -si --needed --noconfirm
  )
  rm -rf -- "$build_dir"
}

install_artix_scenefx() {
  pacman -T 'scenefx>=0.5' &>/dev/null && return

  # SceneFX is in Arch's extra repo, but may be absent from Artix.
  # Do not use scenefx-git: its AUR recipe still targets wlroots 0.19.
  if pacman -Sp --nodeps 'scenefx>=0.5' &>/dev/null; then
    yay -S --needed --noconfirm 'scenefx>=0.5'
    return
  fi

  step 'Building SceneFX 0.5 for Artix'
  sudo pacman -S --needed --noconfirm base-devel
  local build_dir
  build_dir="$(mktemp -d)"
  cp -- "$repo_dir/system/packages/scenefx/PKGBUILD" "$build_dir/PKGBUILD"
  # Keep a failed build for inspection, as with the other local builds.
  (
    cd -- "$build_dir"
    makepkg --syncdeps --install --needed --noconfirm
  )
  rm -rf -- "$build_dir"
}

disable_legacy_power_services() {
  local service runlevel
  for service in tuned-ppd tuned; do
    if [[ "$distro_id" == artix ]]; then
      if [[ -f "/etc/init.d/$service" ]]; then
        sudo rc-service "$service" stop
        for runlevel in /etc/runlevels/*; do
          [[ -e "$runlevel/$service" || -L "$runlevel/$service" ]] || continue
          sudo rc-update del "$service" "${runlevel##*/}"
        done
      fi
    elif [[ -n "$(systemctl list-unit-files "$service.service" --no-legend)" ]]; then
      sudo systemctl disable --now "$service.service"
    fi
  done
}

require_native_power_profiles_daemon() {
  [[ "$(pacman -Qq power-profiles-daemon 2>/dev/null || true)" == power-profiles-daemon ]] ||
    die 'install the genuine power-profiles-daemon package before enabling services'
}

retire_legacy_power_config() {
  local path backup_suffix
  backup_suffix=".pre-ppd-$(date +%Y%m%d-%H%M%S)-$$"
  for path in /etc/init.d/tuned-ppd /etc/tuned/ppd.conf; do
    # Preserve administrator changes; package-owned files belong to pacman.
    if [[ -e "$path" ]] && ! pacman -Qo "$path" &>/dev/null; then
      sudo mv -- "$path" "$path$backup_suffix"
      printf 'Retired %s to %s\n' "$path" "$path$backup_suffix"
    fi
  done
}

install_system_packages() {
  command -v pacman >/dev/null || die 'this installer requires pacman to install packages'

  command -v yay >/dev/null || bootstrap_yay

  local section=repo line
  local -a repo_packages=() aur_packages=() artix_packages=()
  while IFS= read -r line || [[ -n "$line" ]]; do
    case "$line" in
      '# Common AUR packages') section=aur ;;
      '# Artix/OpenRC packages') section=artix ;;
      ''|'#'*) ;;
      *)
        case "$section" in
          repo) repo_packages+=("$line") ;;
          aur) aur_packages+=("$line") ;;
          artix) artix_packages+=("$line") ;;
        esac
        ;;
    esac
  done < "$repo_dir/packages.txt"

  if [[ "$distro_id" == artix ]]; then
    install_artix_scenefx
  fi

  local -a packages=("${repo_packages[@]}")
  if [[ "$distro_id" == artix ]]; then
    packages+=("${artix_packages[@]}")
  fi
  packages+=("${aur_packages[@]}")
  local -a install_flags=(--needed --noconfirm)
  if [[ "$(pacman -Qq tuned-ppd 2>/dev/null || true)" == tuned-ppd ]]; then
    # Replace the conflicting provider in one dependency-checked transaction.
    # Never remove it first: desktop packages may depend on its PPD provision.
    install_flags=(--needed)
    printf 'Approve replacing tuned-ppd with power-profiles-daemon when prompted.\n'
  fi
  disable_legacy_power_services
  yay -S "${install_flags[@]}" "${packages[@]}"
  require_native_power_profiles_daemon
}

if ((install_packages)); then
  step 'Installing packages'
  install_system_packages
fi

backup_root=""
ensure_backup_root() {
  if [[ -z "$backup_root" ]]; then
    backup_root="$target_home/.dotfiles-backup/$(date +%Y%m%d-%H%M%S)-$$"
    mkdir -p -- "$backup_root"
  fi
}

backup_destination() {
  local destination="$1" relative="$2"
  if [[ -e "$destination" || -L "$destination" ]]; then
    ensure_backup_root
    mkdir -p -- "$backup_root/$(dirname -- "$relative")"
    mv -- "$destination" "$backup_root/$relative"
  fi
}

install_file() {
  local source="$1" relative="$2"
  local destination="$target_home/$relative"
  local mode=0644 temporary escaped_home

  [[ -x "$source" ]] && mode=0755
  mkdir -p -- "$(dirname -- "$destination")"

  temporary="$(mktemp)"
  if LC_ALL=C grep -aq '__HOME__' "$source"; then
    escaped_home="$target_home"
    if [[ "$relative" == *.desktop ]]; then
      # Escape both the desktop string and quoted Exec argument layers.
      escaped_home="${target_home//\\/\\\\\\\\}"
      escaped_home="${escaped_home//\"/\\\\\"}"
      escaped_home="${escaped_home//\$/\\\\\$}"
      escaped_home="${escaped_home//\`/\\\\\`}"
      escaped_home="${escaped_home//%/%%}"
      escaped_home="${escaped_home//$'\n'/\\n}"
      escaped_home="${escaped_home//$'\r'/\\r}"
      escaped_home="${escaped_home//$'\t'/\\t}"
    fi
    escaped_home="${escaped_home//\\/\\\\}"
    escaped_home="${escaped_home//&/\\&}"
    escaped_home="${escaped_home//|/\\|}"
    sed "s|__HOME__|$escaped_home|g" "$source" > "$temporary"
  else
    cat -- "$source" > "$temporary"
  fi

  if [[ -f "$destination" && ! -L "$destination" ]] && cmp -s -- "$temporary" "$destination"; then
    chmod "$mode" "$destination"
    rm -f -- "$temporary"
    return
  fi

  backup_destination "$destination" "$relative"
  install -m "$mode" "$temporary" "$destination"
  rm -f -- "$temporary"
}

install_link() {
  local source="$1" relative="$2"
  local destination="$target_home/$relative" link_target
  link_target="$(readlink -- "$source")"
  mkdir -p -- "$(dirname -- "$destination")"

  if [[ -L "$destination" && "$(readlink -- "$destination")" == "$link_target" ]]; then
    return
  fi

  backup_destination "$destination" "$relative"
  ln -s -- "$link_target" "$destination"
}

install_tree() {
  local relative_root="$1" relative source
  for relative in "${install_sources[@]}"; do
    [[ "$relative" == "$relative_root/"* ]] || continue
    source="$repo_dir/$relative"
    if [[ -L "$source" ]]; then
      install_link "$source" "$relative"
    elif [[ -f "$source" ]]; then
      install_file "$source" "$relative"
    fi
  done
}

# Checkouts use the index as an allowlist but install working-tree contents.
# Distribution archives have no index and contain their distribution files.
manifest="$(mktemp)"
if [[ -e "$repo_dir/.git" || -L "$repo_dir/.git" ]]; then
  command -v git >/dev/null || die 'git is required to install from a checkout'
  git -C "$repo_dir" ls-files -z -- .config .local Pictures > "$manifest" \
    || die 'cannot enumerate tracked checkout files'
else
  (cd -- "$repo_dir" && find .config .local Pictures \( -type f -o -type l \) -print0) > "$manifest"
fi
mapfile -d '' -t install_sources < "$manifest"
rm -f -- "$manifest"
for index in "${!install_sources[@]}"; do
  install_sources[index]="${install_sources[index]#./}"
done

step 'Installing home configuration'
install_file "$repo_dir/.bash_profile" '.bash_profile'
install_file "$repo_dir/.bashrc" '.bashrc'
# Make the user D-Bus address available to Mango-launched browser notifications.
install_file "$repo_dir/.pam_environment" '.pam_environment'
install_tree '.config'
install_tree '.local'

# Retire the old managed Beta launcher without touching browser profiles/packages.
backup_destination "$target_home/.local/share/applications/brave-browser-beta.desktop" \
  '.local/share/applications/brave-browser-beta.desktop'

# Retire the former power popup instead of leaving an obsolete launcher/backend.
for relative in .local/bin/tuned-popup .local/share/tuned-popup \
  .local/share/applications/tuned-popup.desktop; do
  backup_destination "$target_home/$relative" "$relative"
done

# Seed the bundled gallery without replacing wallpapers already in Pictures.
for relative in "${install_sources[@]}"; do
  [[ "$relative" == Pictures/* ]] || continue
  source="$repo_dir/$relative"
  [[ -f "$source" && ! -L "$source" ]] || continue
  destination="$target_home/$relative"
  if [[ ! -e "$destination" && ! -L "$destination" ]]; then
    install -Dm0644 "$source" "$destination"
  fi
done

# Preserve defaults in place so the helper can merge unrelated associations.
for relative in .config/mimeapps.list .config/xdg-terminals.list; do
  destination="$target_home/$relative"
  if [[ -e "$destination" || -L "$destination" ]]; then
    ensure_backup_root
    if [[ ! -e "$backup_root/$relative" && ! -L "$backup_root/$relative" ]]; then
      mkdir -p -- "$backup_root/$(dirname -- "$relative")"
      if [[ -L "$destination" && -e "$destination" ]]; then
        # Capture contents, not just a link to a file the helper may modify.
        cp -aL -- "$destination" "$backup_root/$relative"
      else
        cp -a -- "$destination" "$backup_root/$relative"
      fi
    fi
  fi
done

HOME="$target_home" XDG_CONFIG_HOME="$target_home/.config" \
  XDG_DATA_HOME="$target_home/.local/share" \
  "$target_home/.local/bin/sync-default-apps"

# Keep the selected Waybar variant active after refreshing tracked files.
waybar_mode_file="$target_home/.config/waybar/.mode"
if [[ -r "$waybar_mode_file" ]]; then
  waybar_mode="$(<"$waybar_mode_file")"
  if [[ "$waybar_mode" == bar || "$waybar_mode" == dock ]]; then
    cp -- "$target_home/.config/waybar/configs/$waybar_mode.jsonc" \
      "$target_home/.config/waybar/config.jsonc"
    cp -- "$target_home/.config/waybar/styles/$waybar_mode.css" \
      "$target_home/.config/waybar/style.css"
  fi
fi

adapt_hardware() {
  local backlight_path='' backlight_device=''

  for backlight_path in /sys/class/backlight/*; do
    [[ -e "$backlight_path" ]] || continue
    backlight_device="$(basename -- "$backlight_path")"
    break
  done
  if [[ -n "$backlight_device" && "$backlight_device" != amdgpu_bl2 ]]; then
    sed -i "s/amdgpu_bl2/$backlight_device/g" \
      "$target_home/.local/bin/brightness-control"
  fi
}

adapt_hardware

install_built_binary() {
  local source="$1" name="$2"
  local destination="$target_home/.local/bin/$name"
  if [[ -f "$destination" && ! -L "$destination" ]] && cmp -s -- "$source" "$destination"; then
    chmod 0755 "$destination"
    return
  fi
  backup_destination "$destination" ".local/bin/$name"
  install -Dm0755 "$source" "$destination"
}

build_native_utilities() {
  command -v qmake6 >/dev/null || die 'qmake6 is required to build the native utilities'
  command -v make >/dev/null || die 'make is required to build the native utilities'

  local build_root project project_dir jobs
  build_root="$(mktemp -d)"
  # A failed native build may leave this temporary directory for inspection.
  jobs="$(nproc)"
  # Bound compiler memory usage on laptops; projects are built sequentially.
  if ((jobs > 2)); then
    jobs=2
  fi

  for project in "${native_projects[@]}"; do
    step "Building $project"
    project_dir="$build_root/$project"
    mkdir -p -- "$project_dir"
    (
      cd -- "$project_dir"
      qmake6 "$target_home/.local/share/$project/$project.pro"
      make -j"$jobs"
    )
    install_built_binary "$project_dir/$project" "$project"
  done
  rm -rf -- "$build_root"
}

if ((build_native)); then
  step 'Building Qt utilities'
  build_native_utilities
fi

enable_openrc_services() {
  [[ "$target_home" == "$HOME" ]] || die '--skip-services is required with a custom --target-home'
  command -v rc-update >/dev/null || die 'OpenRC rc-update is unavailable'
  command -v rc-service >/dev/null || die 'OpenRC rc-service is unavailable'
  require_native_power_profiles_daemon
  disable_legacy_power_services
  retire_legacy_power_config

  sudo rc-update add dbus boot
  sudo rc-update add NetworkManager default
  sudo rc-update add bluetoothd default
  sudo rc-update add power-profiles-daemon default
  sudo rc-update add sddm default

  sudo rc-service power-profiles-daemon start

  rc-update --user add dbus default
  rc-update --user add pipewire default
  rc-update --user add pipewire-pulse default
  rc-update --user add wireplumber default
}

enable_systemd_services() {
  command -v systemctl >/dev/null || die 'systemd systemctl is unavailable'
  require_native_power_profiles_daemon
  disable_legacy_power_services
  retire_legacy_power_config

  sudo systemctl enable NetworkManager.service bluetooth.service \
    power-profiles-daemon.service sddm.service
  sudo systemctl start power-profiles-daemon.service

  # User units belong to the invoking account, not a root or custom-home target.
  systemctl --user enable pipewire.socket pipewire-pulse.socket wireplumber.service
}

if ((enable_services)); then
  step 'Configuring services'
  if [[ "$distro_id" == artix ]]; then
    enable_openrc_services
  else
    enable_systemd_services
  fi
fi

if [[ -n "$backup_root" ]]; then
  printf 'Existing files backed up to %s\n' "$backup_root"
fi
printf 'Installed MangoWM dotfiles into %s\n' "$target_home"
printf 'Log out, choose the Mango session in SDDM, then log back in.\n'
