#!/usr/bin/env bash
# NixOS-only, invoked with bash; never run the inner program on a host.
set -euo pipefail
umask 077
here=$(cd -- "$(dirname -- "$0")" && pwd)
mode=${1:-preflight}; comp=${2:-kwin}
case $mode in preflight|smoke|cpu) ;; *) exit 2;; esac
case $comp in kwin|kwin-virtual|niri) ;; *) exit 2;; esac
: "${BIN_DIR:?Set BIN_DIR to the Release executable directory}" "${FIXTURE_ROOT:?Set FIXTURE_ROOT to the synthetic fixture root}"
work=$(mktemp -d); echo "Artifacts: $work"
bins=$BIN_DIR
cp -r "$FIXTURE_ROOT/assets" "$work/"
mkdir -p "$work"/{runtime,home,tmp,config,data,state,cache,empty,build}
# Snapshot the agreed pair before launch: rebuilding in BIN_DIR cannot mix versions.
cp "$bins/nekomote_layer_shell" "$bins/nekomote_host_session_probe" "$work/build/"
b=(/nix/store/*bubblewrap-0.13*/bin/bwrap); wi=(/nix/store/*wayland-utils*/bin/wayland-info); xt=$(ldd "$(command -v glxinfo)" | awk '/libX11.so.6 =>/{print $3}')
test -f "$xt"
exec timeout -k 3 240 "${b[0]}" --unshare-all --die-with-parent --new-session --clearenv --setenv PATH /run/current-system/sw/bin --ro-bind /nix/store /nix/store --ro-bind /etc /etc --ro-bind /bin /bin --ro-bind /run/current-system /run/current-system --ro-bind /run/opengl-driver /run/opengl-driver --dev /dev --proc /proc --tmpfs /tmp --ro-bind "$here" /experiment --ro-bind "$work/build" /build --bind "$work" /work --chdir /work --setenv HOME /work/home --setenv TMPDIR /work/tmp --setenv XDG_RUNTIME_DIR /work/runtime --setenv XDG_CONFIG_HOME /work/config --setenv XDG_DATA_HOME /work/data --setenv XDG_CACHE_HOME /work/cache --setenv XDG_STATE_HOME /work/state --setenv XDG_DATA_DIRS /work/empty --setenv XDG_CONFIG_DIRS /work/empty --setenv LIBGL_ALWAYS_SOFTWARE 1 --setenv GALLIUM_DRIVER llvmpipe --setenv __EGL_VENDOR_LIBRARY_FILENAMES /run/opengl-driver/share/glvnd/egl_vendor.d/50_mesa.json --setenv PYTHONDONTWRITEBYTECODE 1 python3 /experiment/run.py "$mode" "$comp" "${wi[0]}" "$xt"
