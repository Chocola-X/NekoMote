#pragma once

#include <algorithm>

namespace nekomote::plasma
{
struct PointerPoint
{
    int x;
    int y;
};

struct PointerSize
{
    int width;
    int height;
};

struct PointerRect
{
    int left;
    int top;
    int width;
    int height;
};

struct PointerPointF
{
    double x;
    double y;
};

// X11 reports root/window geometry in device pixels while Qt item coordinates
// are logical. Keep this conversion in one place so DPR and window offsets
// cannot diverge between the provider and its tests.
inline PointerPointF x11_local_logical(PointerPoint cursor, PointerPoint window_origin,
                                       PointerSize window_size, double dpr)
{
    if (dpr <= 0)
        dpr = 1;
    const int right = window_origin.x + window_size.width;
    const int bottom = window_origin.y + window_size.height;
    const int x = std::clamp(cursor.x, window_origin.x, right);
    const int y = std::clamp(cursor.y, window_origin.y, bottom);
    return {(x - window_origin.x) / dpr, (y - window_origin.y) / dpr};
}

// KWin's workspace.cursorPos and QScreen::geometry already use the
// compositor's logical coordinate space.
inline PointerPointF wayland_local_logical(PointerPoint cursor, PointerRect output,
                                           PointerSize window_size)
{
    const int right = output.left + output.width;
    const int bottom = output.top + output.height;
    const int x = std::clamp(cursor.x, output.left, right);
    const int y = std::clamp(cursor.y, output.top, bottom);
    PointerPointF local{static_cast<double>(x - output.left), static_cast<double>(y - output.top)};
    local.x = std::clamp(local.x, 0.0, static_cast<double>(window_size.width));
    local.y = std::clamp(local.y, 0.0, static_cast<double>(window_size.height));
    return local;
}
} // namespace nekomote::plasma
