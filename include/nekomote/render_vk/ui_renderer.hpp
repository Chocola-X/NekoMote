#pragma once

#include <nekomote/image.hpp>
#include <nekomote/render_vk/renderer.hpp>
#include <nekomote/ui/font.hpp>
#include <nekomote/ui/painter.hpp>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nekomote::render_vk
{

class UiRenderer : public nekomote::ui::Painter
{
  public:
    UiRenderer();
    ~UiRenderer();
    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;

    void reset();

    bool init(const DeviceContext& ctx, int viewport_width, int viewport_height);
    void resize(int viewport_width, int viewport_height);
    void set_render_target(Target target);

    bool init_font(const std::string& font_path, float pixel_height);

    std::uint32_t upload_icon(const ImageRgba8& image);
    void destroy_icon(std::uint32_t id);

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

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nekomote::render_vk
