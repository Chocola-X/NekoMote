#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace nekomote::player::ui
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

struct Glyph
{
    unsigned int codepoint;
    int x0, y0, x1, y1;
    float advance;
    int atlas_x, atlas_y, atlas_w, atlas_h;
};

class FontAtlas
{
  public:
    FontAtlas() = default;
    ~FontAtlas();
    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    bool init(const std::string& font_path, float pixel_height);
    void ensure_char(unsigned int codepoint);
    float text_width(const char* text, float size);
    void draw_text(std::vector<float>& vertices, std::vector<float>& uvs, float x, float y,
                   const char* text, float size);
    float height() const
    {
        return pixel_height_;
    }
    std::uint32_t texture_id() const
    {
        return texture_;
    }

  private:
    bool initialized_{false};
    std::vector<unsigned char> font_data_;
    std::vector<unsigned char> font_info_storage_;
    float pixel_height_{16.0f};
    std::unordered_map<unsigned int, Glyph> glyphs_;
    std::uint32_t texture_{0};
    int texture_width_{1024};
    int texture_height_{1024};
    int pack_x_{0};
    int pack_y_{0};
    int pack_row_height_{0};
    float scale_{1.0f};

    bool create_texture();
    void upload_glyph(const Glyph& glyph, const unsigned char* bitmap);
};

class Renderer
{
  public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(int viewport_width, int viewport_height);
    void resize(int viewport_width, int viewport_height);
    void set_font(FontAtlas* font)
    {
        font_ = font;
    }
    FontAtlas* font() const
    {
        return font_;
    }

    void begin_frame();
    void end_frame();

    void draw_rounded_rect(float x, float y, float w, float h, float radius, Color fill,
                           Color border = {0, 0, 0, 0}, float border_width = 0);
    void draw_text(float x, float y, const char* text, float size, Color color);
    void draw_icon(float x, float y, float w, float h, std::uint32_t texture_id,
                   Color tint = {1, 1, 1, 1});
    void draw_circle(float cx, float cy, float radius, Color fill, Color border = {0, 0, 0, 0},
                     float border_width = 0);
    void draw_solid_rect(float x, float y, float w, float h, Color color);
    void draw_arrow(float cx, float cy, float size, bool points_up, Color fill);

    void push_clip_rect(float x, float y, float w, float h);
    void pop_clip_rect();

    void set_global_alpha(float alpha)
    {
        global_alpha_ = std::clamp(alpha, 0.0f, 1.0f);
    }
    float global_alpha() const
    {
        return global_alpha_;
    }

    int viewport_width() const
    {
        return viewport_width_;
    }
    int viewport_height() const
    {
        return viewport_height_;
    }

    bool is_initialized() const
    {
        return initialized_;
    }

  private:
    bool initialized_{false};
    float global_alpha_{1.0f};
    int viewport_width_{0};
    int viewport_height_{0};
    FontAtlas* font_{nullptr};

    std::uint32_t rect_shader_{0};
    std::uint32_t text_shader_{0};
    std::uint32_t icon_shader_{0};
    std::uint32_t tri_shader_{0};
    std::uint32_t vao_{0};
    std::uint32_t vbo_{0};
    std::uint32_t uv_vbo_{0};

    std::vector<float> rect_vertices_;
    std::vector<float> rect_uvs_;
    std::vector<float> text_vertices_;
    std::vector<float> text_uvs_;
    std::vector<float> icon_vertices_;
    std::vector<float> icon_uvs_;
    std::vector<std::uint32_t> icon_textures_;

    bool create_rect_shader();
    bool create_text_shader();
    bool create_icon_shader();
    bool create_tri_shader();
    void flush_rects();
    void flush_text();
    void flush_icons();
};

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
    float scroll_y{0.0f};
    int expanded_drawer{-1};
};

class Context
{
  public:
    Context() = default;

    void init(Renderer* renderer, Style style = {});

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
    void sidebar(SidebarState& sidebar, const Style& style, float delta_time = 0.016f);
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
    bool mouse_down() const
    {
        return mouse_down_;
    }
    bool mouse_pressed() const
    {
        return mouse_pressed_;
    }

    float scroll_delta() const
    {
        return scroll_delta_;
    }
    void consume_scroll()
    {
        scroll_delta_ = 0.0f;
    }

  private:
    Renderer* renderer_{nullptr};
    Style style_;
    float mouse_x_{0};
    float mouse_y_{0};
    float scroll_delta_{0.0f};
    bool mouse_down_{false};
    bool mouse_pressed_{false};
    bool mouse_released_{false};
    bool captures_mouse_{false};
};

float ease_out_back(float t);
float ease_out_cubic(float t);
float lerp(float a, float b, float t);

} // namespace nekomote::player::ui
