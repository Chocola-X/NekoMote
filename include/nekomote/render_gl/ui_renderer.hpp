#pragma once

#include <nekomote/image.hpp>
#include <nekomote/ui/font.hpp>
#include <nekomote/ui/painter.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace nekomote::render_gl
{

// GPU font atlas texture. Owns a GL R8 texture and uploads glyph bitmaps
// delivered by FontMetrics via callbacks. All GL calls require a current
// context on the owning graphics thread.
class FontAtlas
{
  public:
    FontAtlas() = default;
    ~FontAtlas();
    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    bool init(int texture_width, int texture_height);
    void reset();
    void resize_atlas(int new_width, int new_height);
    void upload_glyph(const nekomote::ui::Glyph& glyph, const unsigned char* bitmap);
    std::uint32_t texture_id() const
    {
        return texture_;
    }
    int texture_width() const
    {
        return texture_width_;
    }
    int texture_height() const
    {
        return texture_height_;
    }

  private:
    std::uint32_t texture_{0};
    int texture_width_{0};
    int texture_height_{0};
};

// GL implementation of ui::Painter. Owns shaders, VAO/VBO, font atlas and
// font metrics. All methods and destruction require a current GL context
// on the owning graphics thread.
class UiRenderer : public nekomote::ui::Painter
{
  public:
    UiRenderer() = default;
    ~UiRenderer();
    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;
    void reset();

    bool init(int viewport_width, int viewport_height);
    void resize(int viewport_width, int viewport_height);
    // Backend-local host target, bound by the next begin_frame().
    void set_render_target(std::uint32_t framebuffer);
    bool init_font(const std::string& font_path, float pixel_height);

    // Icon texture management. Returns an opaque ID for use with draw_icon.
    std::uint32_t upload_icon(ImageViewRgba8 image);
    void destroy_icon(std::uint32_t id);

    // ui::Painter
    void begin_frame() override;
    void end_frame() override;
    void draw_rounded_rect(float x, float y, float w, float h, float radius,
                           nekomote::ui::Color fill, nekomote::ui::Color border,
                           float border_width) override;
    void draw_text(float x, float y, const char* text, float size,
                   nekomote::ui::Color color) override;
    void draw_icon(float x, float y, float w, float h, std::uint32_t texture_id,
                   nekomote::ui::Color tint) override;
    void draw_circle(float cx, float cy, float radius, nekomote::ui::Color fill,
                     nekomote::ui::Color border, float border_width) override;
    void draw_solid_rect(float x, float y, float w, float h, nekomote::ui::Color color) override;
    void draw_arrow(float cx, float cy, float size, bool points_up,
                    nekomote::ui::Color fill) override;
    void push_clip_rect(float x, float y, float w, float h) override;
    void pop_clip_rect() override;
    void set_global_alpha(float alpha) override;
    float global_alpha() const override;
    int viewport_width() const override;
    int viewport_height() const override;
    nekomote::ui::FontMetrics* font() override;
    bool is_initialized() const override;

    std::size_t draw_call_count() const
    {
        return draw_call_count_;
    }
    void reset_draw_call_count()
    {
        draw_call_count_ = 0;
    }

  private:
    bool initialized_{false};
    std::uint32_t framebuffer_{0};
    float global_alpha_{1.0f};
    int viewport_width_{0};
    int viewport_height_{0};
    std::size_t draw_call_count_{0};

    nekomote::ui::FontMetrics font_metrics_;
    FontAtlas font_atlas_;
    bool font_ready_{false};

    std::uint32_t rect_shader_{0};
    std::uint32_t text_shader_{0};
    std::uint32_t icon_shader_{0};
    std::uint32_t tri_shader_{0};
    struct RectUniforms
    {
        int viewport{-1}, fill{-1}, border{-1}, border_width{-1}, rect_size{-1}, radius{-1},
            pixel_scale{-1};
    } rect_uniforms_;
    struct TextureUniforms
    {
        int viewport{-1}, color{-1}, sampler{-1};
    } text_uniforms_, icon_uniforms_;
    struct TriangleUniforms
    {
        int viewport{-1};
    } tri_uniforms_;
    std::uint32_t vao_{0};
    std::uint32_t vbo_{0};
    std::uint32_t uv_vbo_{0};
    std::uint32_t color_vbo_{0};

    std::vector<float> rect_vertices_;
    std::vector<float> rect_uvs_;
    std::vector<float> text_vertices_;
    std::vector<float> text_uvs_;
    std::vector<float> text_colors_;
    nekomote::ui::Color text_color_{};
    std::vector<float> tri_vertices_;
    std::vector<float> tri_colors_;
    nekomote::ui::Color tri_color_{};
    std::vector<float> icon_vertices_;
    std::vector<float> icon_uvs_;
    std::uint32_t icon_texture_{0};
    nekomote::ui::Color icon_tint_{1, 1, 1, 1};
    nekomote::ui::Color rect_cur_fill_{};
    nekomote::ui::Color rect_cur_border_{};
    float rect_cur_border_width_{0};
    float rect_cur_w_{0};
    float rect_cur_h_{0};
    float rect_cur_radius_{0};

    struct ClipRect
    {
        float left{0}, top{0}, right{0}, bottom{0};
        int sx{0}, sy{0}, sw{0}, sh{0};
    };
    std::vector<ClipRect> clip_stack_;
    bool clip_enabled_{false};
    float clip_left_{0}, clip_top_{0}, clip_right_{0}, clip_bottom_{0};

    bool create_rect_shader();
    bool create_text_shader();
    bool create_icon_shader();
    bool create_tri_shader();
    void flush_all();
    void flush_rects();
    void flush_text();
    void flush_triangles();
    void flush_icons();
};

} // namespace nekomote::render_gl
