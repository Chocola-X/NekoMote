#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

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
    Color text{0.436f, 0.306f, 0.216f, 1.0f};
    Color text_hover{0.306f, 0.204f, 0.149f, 1.0f};
    Color accent{0.949f, 0.627f, 0.753f, 1.0f};
    Color divider{0.949f, 0.627f, 0.753f, 0.55f};
    Color hover_bg{0.949f, 0.627f, 0.753f, 0.3f};
    float corner_radius{18.0f};
    float padding{12.0f};
    float font_size{16.0f};
    float title_size{18.0f};
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
    void draw_text(std::vector<float>& vertices, std::vector<float>& uvs,
                   float x, float y, const char* text, float size);
    float height() const { return pixel_height_; }
    std::uint32_t texture_id() const { return texture_; }

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
    void set_font(FontAtlas* font) { font_ = font; }

    void begin_frame();
    void end_frame();

    void draw_rounded_rect(float x, float y, float w, float h, float radius,
                           Color fill, Color border = {0, 0, 0, 0},
                           float border_width = 0);
    void draw_text(float x, float y, const char* text, float size, Color color);
    void draw_icon(float x, float y, float w, float h, std::uint32_t texture_id,
                   Color tint = {1, 1, 1, 1});
    void draw_circle(float cx, float cy, float radius, Color fill,
                     Color border = {0, 0, 0, 0}, float border_width = 0);

    int viewport_width() const { return viewport_width_; }
    int viewport_height() const { return viewport_height_; }

private:
    bool initialized_{false};
    int viewport_width_{0};
    int viewport_height_{0};
    FontAtlas* font_{nullptr};

    std::uint32_t rect_shader_{0};
    std::uint32_t text_shader_{0};
    std::uint32_t icon_shader_{0};
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
    void flush_rects();
    void flush_text();
    void flush_icons();
};

struct IconState
{
    float x, y;
    float size{44.0f};
    bool dragging{false};
    bool hover{false};
    float press_scale{1.0f};
    float drag_offset_x{0};
    float drag_offset_y{0};
};

struct SidebarState
{
    bool open{false};
    float animation{0.0f};
    int expanded_drawer{0};
};

struct RadioButtonState
{
    bool hover{false};
};

class Context
{
public:
    Context() = default;

    void init(Renderer* renderer, Style style = {});

    void mouse_move(double x, double y);
    void mouse_button(int button, int action);
    bool mouse_in_rect(float x, float y, float w, float h) const;

    void begin_frame();
    void end_frame();

    bool toggle_icon(IconState& icon, std::uint32_t texture_id);
    void sidebar(SidebarState& sidebar, const Style& style);

    bool radio_button(const char* label, bool selected, float x, float y,
                      float w, float h, const Style& style);
    bool text_button(const char* label, float x, float y, float w, float h,
                     const Style& style);
    bool toggle_switch(const char* label, bool& value, float x, float y,
                       float w, float h, const Style& style);

    void label(const char* text, float x, float y, float size, Color color);
    void divider_line(float x, float y, float w, const Style& style);

    float mouse_x() const { return mouse_x_; }
    float mouse_y() const { return mouse_y_; }
    bool mouse_down() const { return mouse_down_; }

private:
    Renderer* renderer_{nullptr};
    Style style_;
    float mouse_x_{0};
    float mouse_y_{0};
    bool mouse_down_{false};
    bool mouse_pressed_{false};
    bool mouse_released_{false};
};

float ease_out_back(float t);
float ease_out_cubic(float t);
float lerp(float a, float b, float t);

}
