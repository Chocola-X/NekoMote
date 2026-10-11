#pragma once

#include <nekomote/image.hpp>
#include <nekomote/render_frame.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vulkan/vulkan.h>

namespace nekomote::render_vk
{

// Vulkan device context created and owned by the host. The renderer does not
// create or destroy instance/device; it only records commands and manages GPU
// resources on the provided device. Targets Vulkan 1.0 core for broad driver
// compatibility; no 1.1+ features are used.
struct DeviceContext
{
    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue graphics_queue{VK_NULL_HANDLE};
    std::uint32_t graphics_queue_family{0};
    VkFormat color_format{VK_FORMAT_R8G8B8A8_UNORM};
};

// Native Vulkan framebuffer restricted to this host/backend boundary. The
// render pass is created internally by the renderer for the configured color
// format; the host provides a framebuffer compatible with that pass.
struct Target
{
    VkFramebuffer framebuffer{VK_NULL_HANDLE};
    int width{0}, height{0};
};

struct View
{
    float origin_x{0}, origin_y{0}, scale{1};
    std::array<float, 4> clear{0, 0, 0, 0};
    float alpha{1};
    bool flip_x{false};
};

enum class TargetContents
{
    Clear,
    Preserve
};

struct Capacity
{
    std::size_t vertices{0}, indices{0}, commands{0}, textures{0};
    std::size_t mask_commands{0}, masks{0};
};

class Renderer
{
  public:
    explicit Renderer(const DeviceContext& ctx);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void reserve(Capacity capacity);
    void upload_texture(TextureId id, const ImageRgba8& image);
    void prepare(RenderFrame frame);
    void resize_output(int width, int height);
    void render(Target target, const View& view, TargetContents contents = TargetContents::Clear);
    ImageRgba8 capture(Target target);
    std::string diagnostics() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nekomote::render_vk
