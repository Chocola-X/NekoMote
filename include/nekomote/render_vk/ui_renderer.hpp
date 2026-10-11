#pragma once

#include <nekomote/image.hpp>
#include <nekomote/ui/font.hpp>
#include <nekomote/ui/painter.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vulkan/vulkan.h>
#include <vector>

namespace nekomote::render_vk
{

struct DeviceContext;

class FontAtlas
{
  public:
    FontAtlas() = default;
    ~FontAtlas();
    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    bool init(VkDevice device, VkPhysicalDevice physical_device, VkQueue queue,
              std::uint32_t queue_family, VkCommandPool pool, int texture_width,
              int texture_height);
    void reset();
    void resize_atlas(int new_width, int new_height);
    void upload_glyph(const nekomote::ui::Glyph& glyph, const unsigned char* bitmap);

    VkImageView view() const { return view_; }
    int width() const { return texture_width_; }
    int height() const { return texture_height_; }

  private:
    VkDevice device_{VK_NULL_HANDLE};
    VkImage image_{VK_NULL_HANDLE};
    VkImageView view_{VK_NULL_HANDLE};
    VkDeviceMemory memory_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    std::uint32_t queue_family_{0};
    VkCommandPool pool_{VK_NULL_HANDLE};
    int texture_width_{0}, texture_height_{0};
};

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
    void set_render_target(VkFramebuffer framebuffer);

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
    void draw_solid_rect(float x, float y, float w, float h,
                         nekomote::ui::Color color) override;
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
