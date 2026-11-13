# Plasma pointer tracking and occlusion pause

This note describes the desktop integration used by the Plasma 6 wallpaper
adapter. It is optional adapter policy; the core runtime and ABI remain
desktop-agnostic.

## Problem

The wallpaper item sits below Plasma's FolderView/containment. On X11 the
containment consumes hover/move events, so the item-local Qt path cannot be
used as a reliable source for gaze. Standard Wayland clients do not receive a
global pointer position at all. The wallpaper must not depend on input events
that never arrive.

The adapter therefore has two platform providers and a shared idle policy:

| Platform | Pointer source | Occlusion source |
| --- | --- | --- |
| X11 (`xcb`) | `XQueryPointer` polling | X11 top-level window tree, frame extents and opacity |
| Wayland (KWin 6) | KWin script `workspace.cursorPos` over session DBus | KWin `windowList()` per-output rectangle coverage |
| Anything else | surface-local Qt input | none |

If a provider is unavailable, local `InputKind` handling remains active. Local
hover is suppressed only after a global position has actually arrived, so a
failed bridge cannot silently disable input.

## X11 pointer provider

`adapters/plasma/pointer_provider.cpp` opens a private Xlib display and polls
the root pointer every 16 ms while playback is active. The host window's root
rectangle, X11 attributes and `QWindow::devicePixelRatio()` are converted to
item-local logical coordinates by `pointer_geometry.hpp`. Positions on another
output are clamped to the wallpaper window; only changed positions enqueue a
`Move`, avoiding duplicate events and input queue pressure.

The provider is compiled only when `X11_FOUND`; `NEKOMOTE_PLASMA_X11` guards
the implementation. A build without X11 keeps compiling and uses the Wayland
or local path.

## Wayland KWin bridge

KWin 6 exposes `workspace.cursorPos`, `cursorPosChanged`, `windowList()` and
window geometry/state signals to scripts. The adapter uses them as a local,
non-standard bridge:

1. It registers `org.kde.nekomote.Pointer` at `/NekoMote/Pointer` on the
   session bus and exports `update(int,int)` and
   `setCovered(QString,bool)`.
2. It writes `~/.cache/nekomote/kwin-pointer.js` and loads it through
   `org.kde.kwin.Scripting` as `nekomote-pointer`.
3. `cursorPosChanged` forwards the cursor position, throttled to one call per
   16 ms. `KWinPointerProvider` maps the compositor's logical coordinates to
   the item.
4. Window add/remove, geometry, minimized/fullscreen, output, activity and
   virtual-desktop signals trigger a coverage recomputation. Each output is
   handled independently; a fullscreen app on another output does not pause
   this wallpaper.
5. `KWinOcclusionProvider` looks up the coverage for `QScreen::name()`.
6. A five-second watchdog checks `isScriptLoaded` and reloads the script after
   a KWin restart or service registration failure.

This is not a Wayland standard API and not a guaranteed global-pointer
capability. It is a KWin-specific adapter mechanism. If KWin script loading is
disabled, the API changes, or another compositor is used, the provider returns
no position and the adapter falls back to surface-local input.

## Occlusion semantics

X11 and the KWin bridge use the same coverage rule: the union of opaque
top-level rectangles must cover at least 99.5% of the wallpaper window/output.

- Panels/docks and maximized windows count, so a maximized window plus the
  bottom panel pauses the wallpaper.
- The wallpaper's own desktop window is excluded.
- Minimized, hidden, deleted, and other-activity/desktop windows are ignored.
- X11 applies `_NET_FRAME_EXTENTS` and `_NET_WM_WINDOW_OPACITY`; the KWin
  bridge uses `frameGeometry`, `opacity`, `activities` and `desktops`.
- Transparent (opacity < 1) windows are not counted. Per-pixel ARGB
  transparency, shaped windows, compositor effects, and translucent panels
  can still produce edge cases.
- Multi-output is handled per output. Mixed-DPI multi-output has not been
  validated.

When covered, the item enters the same paused state as a hidden wallpaper:
the render timer and pointer timer stop, and the runtime advances no frames.
When uncovered again, playback resumes within the 250 ms occlusion poll.

## Idle frame limit

`idle_fps_limit()` in `idle_fps.hpp` caps a configured rate above 30 FPS to 30
FPS after ten seconds without input. Input resets the activity timestamp and
the next timer tick restores the configured rate. A configured value of 30 or
lower is unchanged; unlimited (0) is never throttled.

## Tests

`tests/unit/test_plasma_adapters.cpp` is part of `nekomote_unit_tests` and is
discovered by the normal CTest suite, including the headless CI container. It
covers:

- X11 window-origin/DPR mapping and clamping;
- Wayland output/window mapping and clamping;
- panel + maximized-window coverage, overlap, tolerance edge, frame extents;
- idle FPS transitions.

The CI cannot run a KWin session or an X server. Provider integration is
therefore verified separately with a real X11 session and a KWin 6 Wayland
session; the plan and observed limitations are recorded in this document
rather than asserted as unit-test coverage.

## Disabling the bridge

```bash
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript nekomote-pointer
rm -f ~/.cache/nekomote/kwin-pointer.js
systemctl --user restart plasma-plasmashell.service
```

The plugin also switches back to `org.kde.image` without removing the
provider; the KWin script then remains loaded until unloaded.
