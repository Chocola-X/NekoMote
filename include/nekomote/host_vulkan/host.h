#ifndef NEKOMOTE_HOST_VULKAN_H
#define NEKOMOTE_HOST_VULKAN_H

#include <nekomote/abi.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

/* Shared Vulkan presentation host for standalone shells and the C ABI sample.
 * Zero-initialize before use. All calls belong to the shell's graphics thread.
 * Owns instance/device/surface/swapchain and one in-flight submission. Transfers
 * surface ownership on select_device, including failure. Call wait_ready before
 * ANY mutation of the borrowing wallpaper (input, resize, advance or resource
 * release). On failure, stop device work before destroying the wallpaper, then
 * destroy this host, then the native window/display. destroy accepts partial
 * initialization. Normal presentation never reads pixels back to the CPU.
 * NEKOMOTE_VK_VALIDATION=1 requires Khronos validation (diagnostics only). */
typedef struct
{
    /* Public after create_instance/select_device — pass to nekomote_wallpaper_create_vk */
    VkInstance instance;
    VkDebugReportCallbackEXT debug_callback;
    uint32_t validation_errors;
    uint32_t validation_warnings;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    uint32_t graphics_family;

    /* Internal */
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat surface_format;
    VkExtent2D extent;
    VkCommandPool command_pool;
    VkCommandBuffer commands;
    VkFence fence;
    VkSemaphore acquired;
    VkRenderPass render_pass;
    VkFramebuffer framebuffer;
    VkImage output_image;
    VkDeviceMemory output_memory;
    VkImageView output_view;
    int output_w, output_h;
    VkImageLayout output_layout;
    VkSemaphore* swap_finished; /* per swapchain image */
    VkImage* swap_images;       /* cached swapchain images */
    uint32_t swap_image_count;
    uint32_t image_index;
    int pending;
    int recording;
    int present_frame;
    int recreate;
    int requested_w, requested_h;
} VkHostC;

#ifdef __cplusplus
extern "C"
{
#endif

    /* Returns 0 on success, -1 on error (operation + VkResult to stderr).
     * Call destroy after any partial initialization failure. */
    int vk_host_c_create_instance(VkHostC* host, const char** extensions, uint32_t ext_count);
    int vk_host_c_select_device(VkHostC* host, VkSurfaceKHR surface);
    int vk_host_c_create_device(VkHostC* host);

    /* Per-frame: begin returns a target for nekomote_wallpaper_set_vk_target. */
    int vk_host_c_wait_ready(VkHostC* host);
    int vk_host_c_begin_frame(VkHostC* host, int w, int h, int present,
                              nekomote_vk_target* out_target);
    /* 0: presented/offscreen complete, 1: out-of-date (no commit; retry), -1: error.
     * Wayland callers must cancel an uncommitted frame callback on retry. */
    int vk_host_c_present(VkHostC* host);

    /* Diagnostic only: w*h*4 bytes, top-left straight RGBA. May submit the current
     * recording; present must still follow if begin_frame requested presentation. */
    int vk_host_c_capture(VkHostC* host, int w, int h, uint8_t* pixels);

    void vk_host_c_destroy(VkHostC* host);

#ifdef __cplusplus
}
#endif

#endif
