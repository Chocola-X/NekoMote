#!/bin/bash
# Run INSIDE an isolated rootfs, with private network namespace, /tmp and config.
# PARENT_DISPLAY/XAUTHORITY grant Xephyr access to a visible parent X11 window only.
set -euo pipefail
: "${NEKOMOTE_X11_VALIDATION_ROOT:?Set a private writable validation directory}"
: "${PARENT_DISPLAY:?Set the visible parent DISPLAY}"
mkdir -p "$NEKOMOTE_X11_VALIDATION_ROOT"/{home,config,runtime,cache,data}
chmod 700 "$NEKOMOTE_X11_VALIDATION_ROOT/runtime"
export HOME="$NEKOMOTE_X11_VALIDATION_ROOT/home"
export XDG_CONFIG_HOME="$NEKOMOTE_X11_VALIDATION_ROOT/config"
export XDG_RUNTIME_DIR="$NEKOMOTE_X11_VALIDATION_ROOT/runtime"
export XDG_CACHE_HOME="$NEKOMOTE_X11_VALIDATION_ROOT/cache"
export XDG_DATA_HOME="$NEKOMOTE_X11_VALIDATION_ROOT/data"
if [[ "${NEKOMOTE_PRIVATE_X11:-}" != 1 ]]; then
    export NEKOMOTE_PRIVATE_X11=1
    exec dbus-run-session -- bash "$0" "$@"
fi
XAUTHORITY="${PARENT_XAUTHORITY:-}" DISPLAY="$PARENT_DISPLAY" \
    Xephyr :91 -screen 1280x720 -resizeable -nolisten tcp -ac \
    > "$NEKOMOTE_X11_VALIDATION_ROOT/xephyr.log" 2>&1 &
xephyr_pid=$!
trap 'kill "$xephyr_pid" 2>/dev/null || true' EXIT
export DISPLAY=:91 LIBGL_ALWAYS_SOFTWARE=1
unset XAUTHORITY
for ((attempt = 0; attempt < 60; ++attempt)); do
    kill -0 "$xephyr_pid"
    if xdpyinfo > "$NEKOMOTE_X11_VALIDATION_ROOT/xdpyinfo.log" 2>&1; then
        break
    fi
    sleep 0.2
done
kill -0 "$xephyr_pid"
glxinfo -B > "$NEKOMOTE_X11_VALIDATION_ROOT/glxinfo.log" 2>&1
python3 "$(dirname "$0")/../tests/x11/validate_desktop.py" "$@"
