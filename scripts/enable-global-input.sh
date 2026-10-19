#!/usr/bin/env bash
set -euo pipefail

# A per-user ACL on currently connected keyboard/mouse devices, without adding
# the user to a group or changing global udev policy. Hotplug needs a new grant.
case "${1:-}" in
    '') revoke=false ;;
    --revoke) revoke=true ;;
    *) printf 'Usage: bash %s [--revoke]\n' "$0" >&2; exit 2 ;;
esac
if [[ $EUID == 0 ]]; then
    printf 'Run this script as your desktop user; it calls sudo only for setfacl.\n' >&2
    exit 1
fi
for tool in udevadm setfacl sudo; do
    command -v "$tool" >/dev/null || { printf 'Missing command: %s\n' "$tool" >&2; exit 1; }
done
devices=()
shopt -s nullglob
for device in /dev/input/event*; do
    properties=$(udevadm info --query=property --name="$device")
    if [[ $properties == *ID_INPUT_KEYBOARD=1* || $properties == *ID_INPUT_MOUSE=1* ]]; then
        devices+=("$device")
    fi
done
if ((${#devices[@]} == 0)); then
    printf 'No keyboard or mouse input devices found.\n' >&2
    exit 1
fi
if $revoke; then
    sudo setfacl -x "u:$(id -u)" -- "${devices[@]}"
    printf 'Removed input ACL for this user. Switch model input off and mouse tracking to screen position to close existing readers.\n'
else
    printf 'Grant read access to this user on current keyboard/mouse devices.\n'
    printf 'This allows applications running as your user to read those devices.\n'
    sudo setfacl -m "u:$(id -u):r" -- "${devices[@]}"
    printf 'Granted. The widget retries automatically; no logout required.\n'
    printf 'Run this script again after reconnecting devices or rebooting.\n'
fi
