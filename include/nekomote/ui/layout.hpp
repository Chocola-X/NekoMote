#pragma once

#include <nekomote/ui/painter.hpp>
#include <nekomote/ui/state.hpp>
#include <nekomote/ui/style.hpp>

namespace nekomote::ui
{

// Immediate-mode layout, hit-test, and event routing. All drawing goes
// through Painter; no GL dependency. The shell forwards raw mouse/scroll
// events; Context consumes UI events and reports capture state so the
// shell knows whether to forward remaining events to the model.
class Context
{
  public:
    Context() = default;

    void init(Painter* painter, Style style = {});

    void mouse_move(double x, double y);
    void mouse_button(int button, int action);
    void mouse_scroll(double amount);
    bool mouse_in_rect(float x, float y, float w, float h) const;

    void begin_frame();
    void end_frame();

    bool captures_mouse() const
    {
        return captures_mouse_;
    }
    void set_captures_mouse(bool cap)
    {
        captures_mouse_ = cap;
    }

    bool eval_captures_mouse(const IconState& icon, const SidebarState& sidebar) const
    {
        const bool mouse_over_icon = mouse_in_rect(icon.x, icon.y, icon.size, icon.size);
        const float eff_h = std::max(sidebar.height, 44.0f);
        const bool mouse_over_sidebar =
            sidebar.animation > 0.05f && mouse_in_rect(sidebar.x, sidebar.y, sidebar.width, eff_h);
        return mouse_over_icon || mouse_over_sidebar || icon.dragging || sidebar.dragging;
    }

    bool toggle_icon(IconState& icon, std::uint32_t texture_id);
    void sidebar(SidebarState& sidebar, float delta_time = 0.016f);
    void sidebar_header(const SidebarState& sidebar, const char* title, const Style& style);

    bool drawer_header(const char* label, bool is_open, float x, float y, float w, float h,
                       const Style& style);
    bool drawer_item(const char* label, bool is_selected, float x, float y, float w, float h,
                     const Style& style);
    bool radio_row(const char* label, const char* badge, bool selected, float x, float y, float w,
                   float h, const Style& style);
    bool switch_row(const char* label, bool& value, float x, float y, float w, float h,
                    const Style& style);
    bool reset_button(const char* label, float x, float y, float w, float h, const Style& style);

    void section_header(const char* label, float x, float y, float w, float h, const Style& style);
    void divider_line(float x, float y, float w, const Style& style);
    void label(const char* text, float x, float y, float size, Color color);
    void draw_scrollbar(float x, float y, float w, float h, float content_height, float scroll_y,
                        const Style& style);

    float mouse_x() const
    {
        return mouse_x_;
    }
    float mouse_y() const
    {
        return mouse_y_;
    }

    float scroll_delta() const
    {
        return scroll_delta_;
    }

  private:
    Painter* painter_{nullptr};
    Style style_;
    float mouse_x_{0};
    float mouse_y_{0};
    float scroll_delta_{0.0f};
    bool mouse_down_{false};
    bool mouse_pressed_{false};
    bool mouse_released_{false};
    bool captures_mouse_{false};
};

} // namespace nekomote::ui
