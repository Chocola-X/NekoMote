#pragma once

#include <nekomote/abi.h>

#include <QSize>
#include <memory>

namespace nekomote::qt_quick
{
// Owns a separate Vulkan 1.0 device and a GPU-only image shared with the current
// Qt OpenGL context. All methods and destruction require that same current
// context/thread. Destroy ABI wallpaper resources before destroying this bridge.
// Resource mutations require wait_ready(); it waits Vulkan work, not Qt sampling.
class VulkanGlBridge
{
  public:
    VulkanGlBridge();
    ~VulkanGlBridge();
    VulkanGlBridge(const VulkanGlBridge&) = delete;
    VulkanGlBridge& operator=(const VulkanGlBridge&) = delete;

    nekomote_vk_device_context device_context() const noexcept;
    void wait_ready();
    // Teardown-only drain before the ABI wallpaper releases Vulkan resources.
    void wait_noexcept() noexcept;
    // Returns a recording primary command buffer outside a render pass. Borrowed
    // until present(); the color attachment is cleared and attachment-optimal.
    nekomote_vk_target begin_frame(QSize size);
    // Records ownership release and submits once. GL waits, copies the image to
    // qt_fbo, then returns ownership using an external GPU semaphore.
    void present(unsigned qt_fbo);
    unsigned validation_errors() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nekomote::qt_quick
