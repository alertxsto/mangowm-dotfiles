#!/usr/bin/env bash
set -Eeuo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
target_home="$HOME"
install_packages=1
enable_services=1
build_native=1

usage() {
  cat <<'EOF'
Usage: ./install.sh [options]

  --target-home PATH  Install into PATH instead of $HOME
  --skip-packages     Do not install pacman/AUR packages
  --skip-services     Do not enable OpenRC services
  --skip-build        Do not build the native Qt utilities
  -h, --help          Show this help
EOF
}

die() {
  printf 'install.sh: %s\n' "$*" >&2
  exit 1
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

install_system_packages() {
  command -v pacman >/dev/null || die 'this installer requires an Arch or Artix system with pacman'
  [[ -r /etc/os-release ]] || die 'cannot identify the operating system'

  local distro_id
  distro_id="$(. /etc/os-release; printf '%s' "$ID")"
  [[ "$distro_id" == artix ]] || die 'the package and service set currently targets Artix Linux'

  command -v yay >/dev/null || bootstrap_yay

  local section=repo line
  local -a repo_packages=() aur_packages=() artix_packages=()
  while IFS= read -r line || [[ -n "$line" ]]; do
    case "$line" in
      '# AUR packages') section=aur ;;
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

  local -a conflicting_packages=()
  pacman -Qq power-profiles-daemon &>/dev/null &&
    conflicting_packages+=(power-profiles-daemon)
  pacman -Qq power-profiles-daemon-openrc &>/dev/null &&
    conflicting_packages+=(power-profiles-daemon-openrc)
  if ((${#conflicting_packages[@]})); then
    sudo pacman -Rns --noconfirm "${conflicting_packages[@]}"
  fi

  yay -S --needed --noconfirm \
    "${repo_packages[@]}" \
    "${artix_packages[@]}" \
    "${aur_packages[@]}"
}

if ((install_packages)); then
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
    escaped_home="${target_home//\\/\\\\}"
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
  local source_root="$1" relative_root="$2" source relative
  while IFS= read -r -d '' source; do
    relative="${source#"$source_root"/}"
    if [[ -L "$source" ]]; then
      install_link "$source" "$relative_root/$relative"
    else
      install_file "$source" "$relative_root/$relative"
    fi
  done < <(find "$source_root" \( -type f -o -type l \) -print0)
}

install_file "$repo_dir/.bash_profile" '.bash_profile'
install_file "$repo_dir/.bashrc" '.bashrc'
# Make the user D-Bus address available to Mango-launched browser notifications.
install_file "$repo_dir/.pam_environment" '.pam_environment'
install_tree "$repo_dir/.config" '.config'
install_tree "$repo_dir/.local" '.local'

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

# Clean up the retired Matugen pipeline left by older installs.
rm -rf -- "$target_home/.config/matugen"
rm -f -- "$target_home/.config/fish/conf.d/matugen-colors.fish"

adapt_hardware() {
  local wifi_interface='' backlight_path='' backlight_device=''

  if command -v nmcli >/dev/null; then
    while IFS=: read -r device type _state; do
      if [[ "$type" == wifi && "$device" != p2p-* ]]; then
        wifi_interface="$device"
        break
      fi
    done < <(nmcli -t -f DEVICE,TYPE,STATE device 2>/dev/null || true)
  fi

  if [[ -n "$wifi_interface" && "$wifi_interface" != wlan0 ]]; then
    sed -i "s/\"wlan0\"/\"$wifi_interface\"/g" \
      "$target_home/.local/share/network-popup/main.cpp"
  fi

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

  for project in network-popup bluetooth-popup tuned-popup mango-osd wallpaper-overview; do
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
  build_native_utilities
fi

enable_openrc_services() {
  [[ "$target_home" == "$HOME" ]] || die '--skip-services is required with a custom --target-home'
  command -v rc-update >/dev/null || die 'OpenRC rc-update is unavailable'
  command -v rc-service >/dev/null || die 'OpenRC rc-service is unavailable'
  sudo install -Dm0755 "$repo_dir/system/openrc/tuned-ppd" /etc/init.d/tuned-ppd
  sudo install -Dm0644 "$repo_dir/system/tuned/ppd.conf" /etc/tuned/ppd.conf

  sudo rc-update add dbus boot
  sudo rc-update add NetworkManager default
  sudo rc-update add bluetoothd default
  sudo rc-update add tuned default
  sudo rc-update add tuned-ppd default
  sudo rc-update add sddm default

  sudo rc-service tuned start
  sudo rc-service tuned-ppd start

  rc-update --user add dbus default
  rc-update --user add pipewire default
  rc-update --user add pipewire-pulse default
  rc-update --user add wireplumber default
}

if ((enable_services)); then
  enable_openrc_services
fi

if [[ -n "$backup_root" ]]; then
  printf 'Existing files backed up to %s\n' "$backup_root"
fi
printf 'Installed MangoWM dotfiles into %s\n' "$target_home"
printf 'Log out, choose the Mango session in SDDM, then log back in.\n'
