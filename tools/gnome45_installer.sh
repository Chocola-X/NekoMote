#!/bin/bash
set -euo pipefail

package="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
uuid=nekomote@nekopara.uk
program=nekomote_gnome45
bin="$HOME/.local/bin"
extensions="${XDG_DATA_HOME:-$HOME/.local/share}/gnome-shell/extensions"
target="$extensions/$uuid"

case "${1:-install}" in
    install)
        source="$package/share/gnome-shell/extensions/$uuid"
        test -x "$package/bin/$program"
        test -s "$source/metadata.json"
        test -s "$source/schemas/gschemas.compiled"
        mkdir -p -- "$bin" "$extensions"
        # Stage beside the destination so an incomplete copy never replaces it.
        stage="$(mktemp -d "$extensions/.nekomote-install.XXXXXX")"
        trap 'rm -rf -- "$stage"' EXIT
        cp -R -- "$source" "$stage/$uuid"
        install -m 755 -- "$package/bin/$program" "$stage/$program"
        install -m 755 -- "$stage/$program" "$bin/$program"
        rm -rf -- "$target"
        mv -- "$stage/$uuid" "$target"
        printf 'Installed %s. Log in to GNOME Wayland and enable %s using Extensions.\n' "$program" "$uuid"
        ;;
    uninstall)
        rm -f -- "$bin/$program"
        rm -rf -- "$target"
        ;;
    *)
        echo 'Usage: bash install.sh [install|uninstall]' >&2
        exit 2
        ;;
esac
