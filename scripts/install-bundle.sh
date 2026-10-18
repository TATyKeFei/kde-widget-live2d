#!/usr/bin/env bash
set -euo pipefail

if [[ ${1:-} == --help ]]; then
    printf 'Usage: bash install.sh [INSTALL_PREFIX]\nDefault: $HOME/.local\n'
    exit 0
fi
if [[ $# -gt 1 ]]; then
    printf 'Usage: bash install.sh [INSTALL_PREFIX]\n' >&2
    exit 2
fi
bundle_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
prefix=${1:-"$HOME/.local"}
case "$prefix" in
    /*) ;;
    *) printf 'INSTALL_PREFIX must be an absolute path.\n' >&2; exit 2 ;;
esac
if [[ ! -f "$bundle_dir/payload/lib/qt6/qml/org/kde/plasma/live2d/liblive2dbackend.so" ]]; then
    printf 'Bundle payload is missing; extract the complete tar.gz first.\n' >&2
    exit 1
fi
mkdir -p -- "$prefix"
cp -a -- "$bundle_dir/payload/." "$prefix/"

config_dir=${XDG_CONFIG_HOME:-"$HOME/.config"}
env_file="$config_dir/plasma-workspace/env/live2d.sh"
mkdir -p -- "$(dirname -- "$env_file")"
# Produce POSIX shell quoting, including prefixes containing spaces or apostrophes.
qml_dir="$prefix/lib/qt6/qml"
quoted_qml_dir=${qml_dir//\'/\'\\\'\'}
{
    printf '#!/bin/sh\n'
    printf "export QML_IMPORT_PATH='%s'\${QML_IMPORT_PATH:+:\$QML_IMPORT_PATH}\n" "$quoted_qml_dir"
} > "$env_file"
chmod 755 "$env_file"

printf 'Installed to %s\n' "$prefix"
printf 'Reload the desktop from a terminal in your Plasma session (no logout needed):\n\n'
printf 'source %q\n' "$env_file"
printf 'systemctl --user import-environment QML_IMPORT_PATH\n'
printf 'systemctl --user restart plasma-plasmashell.service\n'
printf '\nThen add the Live2D Companion widget from the desktop widget picker.\n'
