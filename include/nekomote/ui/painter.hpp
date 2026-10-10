#pragma once

#include <nekomote/ui/font.hpp>
#include <nekomote/ui/style.hpp>

#include <cstdint>

namespace nekomote::ui
{

// Minimal backend-agnostic drawing protocol. UI layout generates commands
// through this interface; the GL backend (render_gl) provides the concrete
// implementation. No GLenums, GLSL, or native handles appear here.
// Icon texture IDs are opaque uint32 tokens created by the backend's
// upload_icon and passed back to draw_icon; the ui module never interprets
// them as GL handles.
class Painter
{
  public:
    virtual ~Painter() = default;

    virtual void begin_frame() = 0;
    virtual void end_frame() = 0;

    virtual void draw_rounded_rect(float x, float y, float w, float h, float radius, Color fill,
                                   Color border = {0, 0, 0, 0}, float border_width = 0) = 0;
    virtual void draw_text(float x, float y, const char* text, float size, Color color) = 0;
    virtual void draw_icon(float x, float y, float w, float h, std::uint32_t texture_id,
                           Color tint = {1, 1, 1, 1}) = 0;
    virtual void draw_circle(float cx, float cy, float radius, Color fill,
                             Color border = {0, 0, 0, 0}, float border_width = 0) = 0;
    virtual void draw_solid_rect(float x, float y, float w, float h, Color color) = 0;
    virtual void draw_arrow(float cx, float cy, float size, bool points_up, Color fill) = 0;

    virtual void push_clip_rect(float x, float y, float w, float h) = 0;
    virtual void pop_clip_rect() = 0;

    virtual void set_global_alpha(float alpha) = 0;
    virtual float global_alpha() const = 0;

    virtual int viewport_width() const = 0;
    virtual int viewport_height() const = 0;

    virtual FontMetrics* font() = 0;
    virtual bool is_initialized() const = 0;
};

} // namespace nekomote::ui
