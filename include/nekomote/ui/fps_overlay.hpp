#pragma once

#include <nekomote/application/catalog.hpp>
#include <nekomote/application/frame_rate.hpp>
#include <nekomote/ui/painter.hpp>

namespace nekomote::ui
{
// Draw after scene rendering and before controls; never consumes pointer input.
void draw_fps_overlay(Painter& painter, application::FpsPosition position, double fps);

class FpsOverlay
{
  public:
    void render(Painter& painter, const application::WallpaperConfig& config);
    void reset() noexcept
    {
        rate_.reset();
    }

  private:
    application::FrameRate rate_;
};
} // namespace nekomote::ui
