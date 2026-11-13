# Plasma pointer tracking and visibility pause

This note describes the optional desktop integration used by the Plasma 6
wallpaper adapter. The core runtime and ABI remain desktop-agnostic.

## Scope

The wallpaper item sits below Plasma's FolderView/containment, so item-local
hover/move events are not a reliable source for gaze. Standard Wayland clients
do not receive a global pointer position. This change therefore adds only:

- an X11 global pointer source for gaze;
- an X11 event-driven visibility pause;
- no global-pointer or occlusion mechanism on Wayland.

`PointerScope::LocalSurface` remains the intentional Wayland fallback. Idle
frame-rate policy is not part of the adapter; pacing belongs to the
application/session layer and is left for a separate change.

## X11 pointer

`adapters/plasma/pointer_provider.cpp` opens a private Xlib display and polls
`XQueryPointer` every 16 ms while playback is active. The host window's root
rectangle, X11 attributes and `QWindow::devicePixelRatio()` are converted to
item-local logical coordinates by `pointer_geometry.hpp`. Positions on another
output are clamped to the wallpaper window; only changed positions enqueue a
`Move`.

The provider is compiled only when `X11_FOUND`; `NEKOMOTE_PLASMA_X11` guards
the implementation. A build without X11 keeps compiling and uses the
surface-local Qt path.

## X11 visibility pause

`adapters/plasma/occlusion_provider.cpp` opens its own Xlib connection and
selects `VisibilityChangeMask` on the wallpaper host window. A
`QSocketNotifier` reads `VisibilityNotify` events without polling the X11
window tree:

- `VisibilityFullyObscured` suspends playback;
- `VisibilityUnobscured` and `VisibilityPartiallyObscured` resume it.

The event is cached as a boolean. The existing wallpaper lifecycle timer polls
that boolean every 250 ms; it does not perform X11 roundtrips. A changed host
window id reselects the mask, and the item clears the cached pause when it is
itself hidden or suspended.

This depends on the compositor delivering `VisibilityNotify` for the desktop
window. It has been designed for native X11; KWin/XWayland compositing and
other window managers need separate validation. It does not inspect the
window stack, frame extents or opacity, so there is no per-window coverage
heuristic and no polling cost per top-level window.

## Wayland

Wayland clients receive only surface-local pointer coordinates. The adapter
does not inject KWin scripts, query compositor internals, or claim a global
pointer. It keeps `PointerScope::LocalSurface`; click/touch and gaze only work
when local events reach the wallpaper surface. Occlusion pause is not
implemented on Wayland; surface exposure and compositor frame pacing are the
intended mechanisms.

## Tests

`tests/unit/test_plasma_adapters.cpp` is part of `nekomote_unit_tests` and is
discovered by the normal CTest suite, including the headless CI container. It
covers:

- X11 window-origin/DPR mapping and clamping;
- the visibility-state policy (`FullyObscured` suspends; other states do not).

The CI cannot run an X server. X11 provider integration and `VisibilityNotify`
delivery are validated separately; they are not asserted as unit-test coverage.

## Disabling

Set the wallpaper plugin to `org.kde.image`; no compositor-side state or cache
file is created by this implementation.
