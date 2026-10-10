#pragma once

namespace nekomote::ui
{

struct Color
{
    float r, g, b, a;
    constexpr Color(float r_ = 0, float g_ = 0, float b_ = 0, float a_ = 1)
        : r(r_), g(g_), b(b_), a(a_)
    {
    }
};

struct Style
{
    Color panel_bg{1.0f, 1.0f, 1.0f, 0.78f};
    Color text{0.435f, 0.306f, 0.216f, 1.0f};       // #6f4e37
    Color text_hover{0.306f, 0.204f, 0.149f, 1.0f}; // #4e3426
    Color accent{0.949f, 0.627f, 0.753f, 1.0f};     // #f2a0c0
    Color divider{0.949f, 0.627f, 0.753f, 0.55f};
    Color hover_bg{0.949f, 0.627f, 0.753f, 0.3f};
    Color open_bg{0.949f, 0.627f, 0.753f, 0.22f};
    Color switch_off{0.435f, 0.306f, 0.216f, 0.22f};
    Color badge_bg{0.949f, 0.627f, 0.753f, 0.25f};
    Color reset_text{0.761f, 0.322f, 0.498f, 1.0f}; // #c2527f
    float corner_radius{18.0f};
    float padding{16.0f};
    float font_size{14.0f};
    float title_size{16.0f};
};

} // namespace nekomote::ui
