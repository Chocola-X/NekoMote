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
} // namespace nekomote::plasma
