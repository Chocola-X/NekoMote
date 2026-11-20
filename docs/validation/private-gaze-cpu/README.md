# Local experiment: cursor-metadata CPU on a private compositor

This is a manual NixOS validation harness, not a CI test. Supply absolute paths to the Release build's `bin` directory and a freshly generated synthetic fixture. From a clean checkout with the importer built (it may be in a separate build directory):

```sh
export BIN_DIR=/absolute/path/to/release/bin
export IMPORTER=/absolute/path/to/nekomote_import
export FIXTURE_ROOT=$(mktemp -d)
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=tools/testing python3 - <<'PY'
import os
from pathlib import Path
from run_layer_shell_test import write_fixture
write_fixture(Path(os.environ['FIXTURE_ROOT']), Path(os.environ['IMPORTER']), 30)
PY
cd docs/validation/private-gaze-cpu
```

Then run:

```sh
bash run.sh preflight kwin             # try X11 windowed, output-count=2
bash run.sh preflight kwin-virtual     # try virtual, output-count=2
bash run.sh preflight niri
bash run.sh smoke niri                 # no CPU measurements
bash run.sh cpu niri                   # ~2.5 minutes, reports temporary work directory
python3 summarize.py "$RESULT_DIR/cpu.csv"  # set RESULT_DIR to the printed artifacts directory
```

`BIN_DIR` selects the directory containing the agreed strict Release `nekomote_layer_shell` and matching `nekomote_host_session_probe`. Each invocation makes a private snapshot. Hashes are printed at startup. `FIXTURE_ROOT/assets` must contain the tiny generated catalog, not a user's models. The harness does not build or install packages or use host desktop services.

The launcher is derived from the old smoke `launch` / `test.sh`, not executed through them: private bwrap PID, network, IPC and mount namespaces, cleared environment, new HOME/XDG trees, private `/tmp` and `/dev`, no `/dev/input`, `/dev/dri`, host runtime, Wayland/X11 sockets, or host bus. Session and system bus addresses and `NEKOMOTE_TEST_SESSION_BUS` all point to the same activation-free private bus. `XDG_SESSION_ID=test`; `probe --serve` models ScreenSaver and login1, including LockedHint and PreparingForSleep. Only the fake service is sent `--set hint-lock` / `hint-unlock`. The only pointer movement is XWarpPointer **inside the private Xvfb**, not via a host input device. No client requests screen pixels or cursor images. No raw Wayland trace or observed coordinates are stored. Do not run the inner script on a host.

## Coverage and method

* No installed Sway executable was found. KWin/X11 windowed reported that private Xvfb lacked DRI3 1.2+. KWin virtual exposed two 640×480 outputs, but did **not advertise `ext_output_image_capture_source_manager_v1` to the isolated client**. Neither is a valid two-output measurement. These are environment observations, not statements about all KWin/Sway versions.
* Fallback: real niri `26.04-unstable-2026-11-14` (`ebbb9ff71c44`) winit inside Xvfb; registry shows one `winit` 1280×800@60 Hz output, scale 1. OpenGL software path, no hardware device: private GLX probe reports Mesa 26.3.0 llvmpipe (LLVM 21.1.8, 256-bit); the experiment forces llvmpipe for niri and wallpaper. Xvfb is a separate process excluded from the niri column.
* Both arms: one wallpaper instance on that output, passthrough, no session pause, audio muted. Identical synthetic asset tree; explicit 30 FPS **cap** in `--state` used with `--assets` so the panel state actually loads. The diagnostic `--model` path does not call `set_assets_dir()` and is not the timed workload. The source session's `interaction.eye` is null: this measures metadata delivery, wakeups, ordinary dispatch/rendering and composition, **not realistic character eye animation**.
* Protocol witness (not timed): the desktop client must stay alive, commit, create a pointer cursor session and receive multiple `position` events during prescribed motion. Independent lock smoke requires active rendering but zero creations without services and at locked startup; fake unlock must create and deliver positions; fake relock must destroy the existing cursor session. It tests client subscription lifetime, not compositor lockscreen behavior.
* CPU blocks restart the wallpaper at each mode change while keeping the same niri. Four local/desktop pairs (LD, DL, LD, DL); idle/motion order also reverses between pairs. Each phase resets the pointer; idle holds it stationary, motion repeats the same 2 s triangle at a target of 60 updates/s. Three seconds warm-up per phase excluded; six seconds measured. No Wayland debugging in any timed client. Raw rows include `utime+stime` deltas from `/proc/<pid>/stat` for each multithreaded process, clock ticks/s and actual monotonic elapsed seconds. 100% = one core; total wallpaper = its sole instance. Paired changes are desktop minus local in percentage points. Four short pairs on an unpinned shared machine are **not** confidence bounds, hardware GPU data, physical dual-output or cross-boundary cost. Do not double the numbers to estimate two screens.

## Measured result (final fail-closed Release)

| Phase | Process | Local median | Desktop median | Paired Δ median [min, max] (pp) |
|---|---|---:|---:|---:|
| idle | wallpaper (= total) | 31.75% | 31.75% | +0.17 [−0.33, +0.33] |
| idle | niri | 28.33% | 28.17% | −0.17 [−0.67, +0.33] |
| motion | wallpaper (= total) | 32.42% | 32.67% | +0.17 [+0.00, +0.50] |
| motion | niri | 82.75% | 82.75% | +0.08 [−0.50, +0.33] |

Motion increased the compositor's CPU substantially even in **local** mode. The additional median difference associated with desktop gaze was small in this fixture; these data cannot establish zero overhead. See `cpu.csv` for all 16 unrounded rows. The CPU-run protocol witness saw 1 creation, 1 enter and 231 position events. Each of the four timed desktop launches also reported receiving cursor position metadata. The independent final smoke observed 0 creations without services, 0 at locked startup, 1 creation and 240 positions after unlocking, then 1 destroy with no increase in position count after relocking. Only request/event counts were retained.

Provenance: commit `1f38c42d5b970a9575f496e359805d05451cb3a8`, including the startup lock-notification race fix. Built with `CMAKE_BUILD_TYPE=Release` and `NEKOMOTE_WERROR=ON`. Binary hashes identify the immutable input pair for both the smoke run and all timed rounds:

| Input | SHA-256 |
|---|---|
| `nekomote_layer_shell` | `3e464edef4e4c7c6ac54be8135082a35c3f5f500fac1a09b0cf2a662d5165f0d` |
| `nekomote_host_session_probe` | `d45734a78dec8aaa87fb5da7f6828f0eebe4287c1bbda7faa1185fc7bf817b64` |
| synthetic `model.nkm` | `7bdb651d0e4d835adcb304f0877fdff778d47a6542d25498a313392c91d24398` |
| synthetic `session.nks.json` | `3c3a8363fc8c329906ea78ac05a4505ca980382ac6083a405826a834f9e6a01d` |
