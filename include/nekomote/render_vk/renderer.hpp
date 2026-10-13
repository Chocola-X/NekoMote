#pragma once

#include <nekomote/image.hpp>
#include <nekomote/render_frame.hpp>

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

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

// Host owns a single in-flight submission. Before prepare/resource mutation it
// waits for the preceding submission. render only records into commands; it
// never submits or owns presentation synchronization. The target is already in
// COLOR_ATTACHMENT_OPTIMAL; a compatible single-color LOAD/STORE pass is used.
// Storage follows GL-compatible lower-left logical row order to preserve
// raster rounding. The host flips rows once at presentation/diagnostic capture.
// All renderer resources must be destroyed before the host device.
struct Target
{
    VkCommandBuffer commands{VK_NULL_HANDLE};
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
    void upload_texture(TextureId id, ImageViewRgba8 image, TextureRegion region = {});
    void prepare(RenderFrame frame);
    void resize_output(int width, int height);
    void render(Target target, const View& view, TargetContents contents = TargetContents::Clear);
    std::string diagnostics() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nekomote::render_vk
