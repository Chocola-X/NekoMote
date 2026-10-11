#include <nekomote/ui/easing.hpp>
#include <nekomote/ui/layout.hpp>

#include <algorithm>
#include <cmath>

namespace nekomote::ui
{

void Context::init(Painter* painter, Style style)
{
    painter_ = painter;
    style_ = style;
}

void Context::mouse_move(double x, double y)
{
    mouse_x_ = static_cast<float>(x);
    mouse_y_ = static_cast<float>(y);
}

void Context::mouse_button(int button, int action)
{
    if (button == 0)
    {
        mouse_down_ = action == 1;
        if (action == 1)
            mouse_pressed_ = true;
        if (action == 0)
            mouse_released_ = true;
    }
}

void Context::mouse_scroll(double amount)
{
    scroll_delta_ += static_cast<float>(amount);
}

bool Context::mouse_in_rect(float x, float y, float w, float h) const
{
    return mouse_x_ >= x && mouse_x_ <= x + w && mouse_y_ >= y && mouse_y_ <= y + h;
}

void Context::begin_frame()
{
    if (painter_)
        painter_->begin_frame();
}

void Context::end_frame()
{
    mouse_pressed_ = false;
    mouse_released_ = false;
    scroll_delta_ = 0.0f;
}

bool Context::toggle_icon(IconState& icon, std::uint32_t texture_id)
{
    if (!painter_)
        return false;
    const float vw = static_cast<float>(painter_->viewport_width());
    const float vh = static_cast<float>(painter_->viewport_height());
    if (!icon.initialized)
    {
        icon.x = std::max(8.0f, vw - icon.size - 8.0f);
        icon.y = 8.0f;
        icon.initialized = true;
    }
    else if (!icon.dragging)
    {
        icon.x = std::clamp(icon.x, 8.0f, std::max(8.0f, vw - icon.size - 8.0f));
        icon.y = std::clamp(icon.y, 8.0f, std::max(8.0f, vh - icon.size - 8.0f));
    }
    icon.hover = mouse_in_rect(icon.x, icon.y, icon.size, icon.size);
    if (icon.hover && mouse_pressed_ && !icon.dragging)
    {
        icon.dragging = true;
        icon.drag_offset_x = mouse_x_ - icon.x;
        icon.drag_offset_y = mouse_y_ - icon.y;
        icon.start_x = icon.x;
        icon.start_y = icon.y;
        icon.moved = false;
    }
    bool clicked = false;
    if (icon.dragging)
    {
        icon.x = mouse_x_ - icon.drag_offset_x;
        icon.y = mouse_y_ - icon.drag_offset_y;
        icon.x = std::clamp(icon.x, 8.0f, std::max(8.0f, vw - icon.size - 8.0f));
        icon.y = std::clamp(icon.y, 8.0f, std::max(8.0f, vh - icon.size - 8.0f));
        if (std::abs(icon.x - icon.start_x) + std::abs(icon.y - icon.start_y) > 3.0f)
            icon.moved = true;
        if (mouse_released_ || !mouse_down_)
        {
            icon.dragging = false;
            if (mouse_released_ && !icon.moved && icon.hover)
                clicked = true;
        }
    }
    const float target_scale = icon.dragging ? 0.96f : (icon.hover ? 1.08f : 1.0f);
    icon.press_scale = lerp(icon.press_scale, target_scale, 0.25f);
    const float draw_size = icon.size * icon.press_scale;
    const float offset = (draw_size - icon.size) * 0.5f;
    if (texture_id)
        painter_->draw_icon(icon.x - offset, icon.y - offset, draw_size, draw_size, texture_id,
                            {1, 1, 1, icon.hover ? 1.0f : 0.88f});
    else
    {
        // The optional bitmap must not make the control entry invisible.
        painter_->draw_rounded_rect(icon.x, icon.y, icon.size, icon.size, 8, style_.panel_bg,
                                    style_.accent, 2);
        for (int line = 0; line < 3; ++line)
            painter_->draw_solid_rect(icon.x + 11, icon.y + 12 + static_cast<float>(line) * 8,
                                      icon.size - 22, 3, style_.text);
    }
    return clicked;
}

void Context::sidebar(SidebarState& sidebar, float delta_time)
{
    if (!painter_)
        return;

    const float vw = static_cast<float>(painter_->viewport_width());
    const float vh = static_cast<float>(painter_->viewport_height());
    const float header_h = 44.0f;
    const float eff_h = std::max(sidebar.height, header_h);

    if (sidebar.open && sidebar.animation <= 0.001f && sidebar.is_free)
    {
        if (sidebar.x + sidebar.width < 16.0f || sidebar.x > vw - 16.0f || sidebar.y > vh - 16.0f)
        {
            sidebar.is_free = false;
            sidebar.x = 8.0f;
            sidebar.y = 8.0f;
        }
    }

    if (sidebar.animation > 0.001f)
    {
        const bool hover_header = mouse_in_rect(sidebar.x, sidebar.y, sidebar.width, header_h);
        if (hover_header && mouse_pressed_ && !sidebar.dragging)
        {
            sidebar.dragging = true;
            sidebar.drag_offset_x = mouse_x_ - sidebar.x;
            sidebar.drag_offset_y = mouse_y_ - sidebar.y;
        }
        if (sidebar.dragging)
        {
            sidebar.is_free = true;
            sidebar.x = mouse_x_ - sidebar.drag_offset_x;
            sidebar.y = mouse_y_ - sidebar.drag_offset_y;
            sidebar.x = std::clamp(sidebar.x, 8.0f, std::max(8.0f, vw - sidebar.width - 8.0f));
            sidebar.y = std::clamp(sidebar.y, 8.0f, std::max(8.0f, vh - eff_h - 8.0f));
            if (mouse_released_ || !mouse_down_)
                sidebar.dragging = false;
        }
    }
    else
    {
        sidebar.dragging = false;
    }

    const float dt = std::clamp(delta_time, 0.0001f, 0.1f);
    if (sidebar.open)
    {
        const float open_speed = 1.0f / 0.28f;
        sidebar.animation = std::min(1.0f, sidebar.animation + dt * open_speed);
    }
    else
    {
        const float close_speed = 1.0f / 0.24f;
        sidebar.animation = std::max(0.0f, sidebar.animation - dt * close_speed);
        if (sidebar.animation <= 0.001f)
            sidebar.animation = 0.0f;
    }

    if (!sidebar.is_free)
    {
        const float slide_anim = sidebar.open ? ease_out_back(sidebar.animation)
                                              : (sidebar.animation * sidebar.animation);
        sidebar.x = 8.0f - (sidebar.width + 16.0f) * (1.0f - slide_anim);
        sidebar.y = 8.0f;
    }
    else if (!sidebar.dragging)
    {
        sidebar.x = std::clamp(sidebar.x, 8.0f, std::max(8.0f, vw - sidebar.width - 8.0f));
        sidebar.y = std::clamp(sidebar.y, 8.0f, std::max(8.0f, vh - eff_h - 8.0f));
    }
}

void Context::sidebar(SidebarState& sidebar, const Style& /*style*/, float delta_time)
{
    this->sidebar(sidebar, delta_time);
}

void Context::sidebar_header(const SidebarState& sidebar, const char* title, const Style& style)
{
    if (!painter_)
        return;
    const float header_h = 44.0f;
    painter_->draw_text(sidebar.x + 16.0f, sidebar.y + 12.0f, title, style.title_size, style.text);
    painter_->draw_text(sidebar.x + 16.5f, sidebar.y + 12.0f, title, style.title_size, style.text);
    divider_line(sidebar.x, sidebar.y + header_h - 1.0f, sidebar.width, style);
}

bool Context::drawer_header(const char* label, bool is_open, float x, float y, float w, float h,
                            const Style& style)
{
    if (!painter_)
        return false;
    const bool hover = mouse_in_rect(x, y, w, h);
    if (is_open)
        painter_->draw_rounded_rect(x, y, w, h, 0, style.open_bg, {0, 0, 0, 0});
    else if (hover)
        painter_->draw_rounded_rect(x, y, w, h, 0, style.hover_bg, {0, 0, 0, 0});
    const Color text_col = (hover || is_open) ? style.text_hover : style.text;
    const float ty = y + (h - 15.0f) * 0.5f;
    painter_->draw_text(x + 16.0f, ty, label, 15.0f, text_col);
    const float arrow_x = x + w - 24.0f;
    const float arrow_y = y + h * 0.5f;
    painter_->draw_arrow(arrow_x, arrow_y, 4.0f, is_open, text_col);
    divider_line(x, y + h - 1.0f, w, style);
    return hover && mouse_pressed_;
}

bool Context::drawer_item(const char* label, bool is_selected, float x, float y, float w, float h,
                          const Style& style)
{
    if (!painter_)
        return false;
    const bool hover = mouse_in_rect(x, y, w, h);
    if (hover)
        painter_->draw_rounded_rect(x, y, w, h, 0, style.hover_bg, {0, 0, 0, 0});
    else if (is_selected)
        painter_->draw_rounded_rect(x, y, w, h, 0, Color(0.949f, 0.627f, 0.753f, 0.15f),
                                    {0, 0, 0, 0});
    const Color text_col = (hover || is_selected) ? style.text_hover : style.text;
    const float offset_x = hover ? 35.0f : 30.0f;
    const float ty = y + (h - style.font_size) * 0.5f;
    painter_->draw_text(x + offset_x, ty, label, style.font_size, text_col);
    return hover && mouse_pressed_;
}

bool Context::radio_row(const char* label, const char* badge, bool selected, float x, float y,
                        float w, float h, const Style& style)
{
    if (!painter_)
        return false;
    const bool hover = mouse_in_rect(x, y, w, h);
    if (hover)
        painter_->draw_rounded_rect(x, y, w, h, 0, style.hover_bg, {0, 0, 0, 0});
    const float dot_r = 7.0f;
    const float dot_cx = x + 30.0f + dot_r;
    const float dot_cy = y + h * 0.5f;
    painter_->draw_circle(dot_cx, dot_cy, dot_r, selected ? style.accent : Color(0, 0, 0, 0),
                          style.accent, 2.0f);
    const Color text_col = (hover || selected) ? style.text_hover : style.text;
    const float text_x = dot_cx + dot_r + 10.0f;
    const float ty = y + (h - style.font_size) * 0.5f;
    painter_->draw_text(text_x, ty, label, style.font_size, text_col);
    if (badge && badge[0] != '\0')
    {
        const float bw =
            painter_->font() ? painter_->font()->text_width(badge, 11.0f) + 14.0f : 36.0f;
        const float bh = 18.0f;
        const float bx = x + w - bw - 16.0f;
        const float by = y + (h - bh) * 0.5f;
        painter_->draw_rounded_rect(bx, by, bw, bh, 8.0f, style.badge_bg, {0, 0, 0, 0});
        painter_->draw_text(bx + 7.0f, by + 2.0f, badge, 11.0f, style.text);
    }
    return hover && mouse_pressed_;
}

bool Context::switch_row(const char* label, bool& value, float x, float y, float w, float h,
                         const Style& style)
{
    if (!painter_)
        return false;
    const bool hover = mouse_in_rect(x, y, w, h);
    if (hover)
        painter_->draw_rounded_rect(x, y, w, h, 0, style.hover_bg, {0, 0, 0, 0});
    const Color text_col = hover ? style.text_hover : style.text;
    const float ty = y + (h - style.font_size) * 0.5f;
    painter_->draw_text(x + 30.0f, ty, label, style.font_size, text_col);
    const float sw_w = 40.0f, sw_h = 22.0f;
    const float sw_x = x + w - sw_w - 16.0f;
    const float sw_y = y + (h - sw_h) * 0.5f;
    const Color off_bg{0.435f, 0.306f, 0.216f, 0.28f};
    painter_->draw_rounded_rect(sw_x, sw_y, sw_w, sw_h, 11.0f, value ? style.accent : off_bg,
                                {0, 0, 0, 0});
    const float knob_r = 9.0f;
    const float knob_x = value ? (sw_x + sw_w - knob_r - 2.0f) : (sw_x + knob_r + 2.0f);
    const float knob_y = sw_y + 11.0f;
    painter_->draw_circle(knob_x, knob_y, knob_r, Color(1, 1, 1, 1), Color(0.3f, 0.2f, 0.2f, 0.20f),
                          1.0f);
    if (hover && mouse_pressed_)
    {
        value = !value;
        return true;
    }
    return false;
}

bool Context::reset_button(const char* label, float x, float y, float w, float h,
                           const Style& style)
{
    if (!painter_)
        return false;
    const bool hover = mouse_in_rect(x, y, w, h);
    if (hover)
        painter_->draw_rounded_rect(x, y, w, h, 0, style.hover_bg, {0, 0, 0, 0});
    divider_line(x, y, w, style);
    const float ty = y + (h - style.font_size) * 0.5f;
    painter_->draw_text(x + 30.0f, ty, label, style.font_size, style.reset_text);
    painter_->draw_text(x + 30.5f, ty, label, style.font_size, style.reset_text);
    return hover && mouse_pressed_;
}

void Context::section_header(const char* label, float x, float y, float /*w*/, float h,
                             const Style& style)
{
    if (!painter_)
        return;
    const float ty = y + (h - style.font_size) * 0.5f;
    painter_->draw_text(x + 30.0f, ty, label, style.font_size, style.text_hover);
    painter_->draw_text(x + 30.5f, ty, label, style.font_size, style.text_hover);
}

void Context::divider_line(float x, float y, float w, const Style& style)
{
    if (painter_)
        painter_->draw_solid_rect(x, y, w, 1.0f, style.divider);
}

void Context::label(const char* text, float x, float y, float size, Color color)
{
    if (painter_)
        painter_->draw_text(x, y, text, size, color);
}

void Context::draw_scrollbar(float x, float y, float w, float h, float content_height,
                             float scroll_y, const Style& style)
{
    if (!painter_ || content_height <= h)
        return;
    const float bar_w = 4.0f;
    const float bar_x = x + w - bar_w - 3.0f;
    const float bar_h = std::max(24.0f, h * (h / content_height));
    const float max_scroll = content_height - h;
    const float ratio = std::clamp(scroll_y / max_scroll, 0.0f, 1.0f);
    const float bar_y = y + ratio * (h - bar_h);
    painter_->draw_rounded_rect(bar_x, bar_y, bar_w, bar_h, 2.0f,
                                Color(style.accent.r, style.accent.g, style.accent.b, 0.85f),
                                {0, 0, 0, 0});
}

} // namespace nekomote::ui
