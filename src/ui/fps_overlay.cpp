#include <nekomote/ui/fps_overlay.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace nekomote::ui
{
void draw_fps_overlay(Painter& painter, application::FpsPosition position, double fps)
{
    auto* font = painter.font();
    if (!font)
        return;
    const auto tenths = static_cast<unsigned>(
        std::lround(std::clamp(std::isfinite(fps) ? fps : 0.0, 0.0, 99999.9) * 10));
    char text[24];
    std::snprintf(text, sizeof(text), "%u.%u FPS", tenths / 10, tenths % 10);
    constexpr float size = 18, margin = 12, outline = 1.25f;
    const float width = font->text_width(text, size);
    const bool right = position == application::FpsPosition::TopRight ||
                       position == application::FpsPosition::BottomRight;
    const bool bottom = position == application::FpsPosition::BottomLeft ||
                        position == application::FpsPosition::BottomRight;
    const float x =
        right ? std::max(margin, static_cast<float>(painter.viewport_width()) - width - margin)
              : margin;
    const float y =
        bottom
            ? std::max(margin, static_cast<float>(painter.viewport_height()) - size * 1.3f - margin)
            : margin;
    painter.set_global_alpha(1);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx || dy)
                painter.draw_text(x + static_cast<float>(dx) * outline,
                                  y + static_cast<float>(dy) * outline, text, size, {1, 1, 1, 1});
    painter.draw_text(x, y, text, size, Style{}.reset_text);
}

void FpsOverlay::render(Painter& painter, const application::WallpaperConfig& config)
{
    if (!config.show_fps)
    {
        reset();
        return;
    }
    const double now =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    draw_fps_overlay(painter, config.fps_position, rate_.observe(now));
}
} // namespace nekomote::ui
