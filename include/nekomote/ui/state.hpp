#pragma once

namespace nekomote::ui
{

struct IconState
{
    float x{0.0f};
    float y{8.0f};
    float size{44.0f};
    bool dragging{false};
    bool hover{false};
    float press_scale{1.0f};
    float drag_offset_x{0.0f};
    float drag_offset_y{0.0f};
    float start_x{0.0f};
    float start_y{0.0f};
    bool moved{false};
    bool initialized{false};
    bool position_reset{false};
};

struct SidebarState
{
    bool open{false};
    float animation{0.0f};
    float x{8.0f};
    float y{8.0f};
    float width{264.0f};
    float height{0.0f};
    bool is_free{false};
    bool dragging{false};
    float drag_offset_x{0.0f};
    float drag_offset_y{0.0f};
    float start_x{8.0f};
    float start_y{8.0f};
    bool moved{false};
    bool position_reset{false};
    float scroll_y{0.0f};
    int expanded_drawer{-1};
};

} // namespace nekomote::ui
