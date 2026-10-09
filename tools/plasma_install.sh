#!/bin/bash
# Experimental KPackage-only installer; not a complete Plasma deployment.
# QML/native module installation and relocation acceptance are pending G27.
# Use temporary prefixes for investigation; see docs/agent-next-handoff.md.
# Does NOT install to system paths, change real desktop settings, restart
# the shell, or copy PCM/audio services. All steps are auditable.
#
# Usage:
#   tools/plasma_install.sh --install   [--stage DIR] [--prefix DIR]
#   tools/plasma_install.sh --uninstall [--prefix DIR]
#   tools/plasma_install.sh --status    [--prefix DIR]
#
# Default stage: build/plasma/adapters/plasma/stage
# Current default prefix: ~/.local/share/plasma; XDG_DATA_HOME handling pending G27.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
STAGE="${NEKOMOTE_STAGE:-$REPO/build/plasma/adapters/plasma/stage}"
PREFIX="${NEKOMOTE_PREFIX:-$HOME/.local/share/plasma}"
ACTION=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --install)   ACTION="install"; shift ;;
        --uninstall) ACTION="uninstall"; shift ;;
        --status)    ACTION="status"; shift ;;
        --stage)     STAGE="$2"; shift 2 ;;
        --prefix)    PREFIX="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

if [[ -z "$ACTION" ]]; then
    echo "usage: $0 --install|--uninstall|--status [--stage DIR] [--prefix DIR]" >&2
    exit 1
fi

WALLPAPER_DIR="$PREFIX/wallpapers/org.nekomote.wallpaper"
PLASMOID_DIR="$PREFIX/plasmoids/org.nekomote.validation"
LOG_FILE="$PREFIX/.nekomote-install.log"

case "$ACTION" in
    status)
        if [[ -f "$LOG_FILE" ]]; then
            echo "Install log: $LOG_FILE"
            cat "$LOG_FILE"
        else
            echo "No install record found at $LOG_FILE"
        fi
        if [[ -d "$WALLPAPER_DIR" ]]; then
            echo "Wallpaper package: installed at $WALLPAPER_DIR"
        else
            echo "Wallpaper package: not installed"
        fi
        if [[ -d "$PLASMOID_DIR" ]]; then
            echo "Validation plasmoid: installed at $PLASMOID_DIR"
        else
            echo "Validation plasmoid: not installed"
        fi
        ;;
    install)
        if [[ ! -d "$STAGE" ]]; then
            echo "stage directory not found: $STAGE" >&2
            echo "run: cmake --build build/plasma --target nekomote_plasma_stage" >&2
            exit 1
        fi
        if [[ ! -f "$STAGE/manifest.json" ]]; then
            echo "stage manifest not found: $STAGE/manifest.json" >&2
            exit 1
        fi
        mkdir -p "$PREFIX"
        echo "$(date -Iseconds) install stage=$STAGE prefix=$PREFIX" > "$LOG_FILE"
        echo "manifest sha256=$(sha256sum "$STAGE/manifest.json" | cut -d' ' -f1)" >> "$LOG_FILE"

        rm -rf "$WALLPAPER_DIR" "$PLASMOID_DIR"
        mkdir -p "$(dirname "$WALLPAPER_DIR")" "$(dirname "$PLASMOID_DIR")"
        cp -r "$STAGE/package/org.nekomote.wallpaper" "$WALLPAPER_DIR"
        echo "wallpaper: $WALLPAPER_DIR ($(find "$WALLPAPER_DIR" -type f | wc -l) files)" >> "$LOG_FILE"

        cp -r "$STAGE/package/org.nekomote.validation" "$PLASMOID_DIR"
        echo "plasmoid: $PLASMOID_DIR ($(find "$PLASMOID_DIR" -type f | wc -l) files)" >> "$LOG_FILE"

        echo "Installed to $PREFIX"
        echo "Wallpaper: $WALLPAPER_DIR"
        echo "Plasmoid:  $PLASMOID_DIR"
        echo "Log:       $LOG_FILE"
        echo ""
        echo "To use: System Settings > Wallpaper > NekoMote"
        echo "No desktop settings changed, no shell restart, no system install."
        ;;
    uninstall)
        if [[ -f "$LOG_FILE" ]]; then
            echo "Removing per install log:"
            cat "$LOG_FILE"
        fi
        rm -rf "$WALLPAPER_DIR" "$PLASMOID_DIR" "$LOG_FILE"
        echo "Removed $WALLPAPER_DIR"
        echo "Removed $PLASMOID_DIR"
        echo "No system paths touched."
        ;;
esac
