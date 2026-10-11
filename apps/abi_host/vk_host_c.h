#ifndef NEKOMOTE_VK_HOST_C_H
#define NEKOMOTE_VK_HOST_C_H

#include <stdint.h>
#include <vulkan/vulkan.h>

#include <nekomote/abi.h>

/* Minimal C Vulkan host for abi_host. Owns instance/device/swapchain and the
 * sole in-flight frame submission. The wallpaper instance borrows the device
 * for command recording; destroy the wallpaper before this host. */
typedef struct
{
    /* Public after create_instance/select_device — pass to nekomote_wallpaper_create_vk */
    VkInstance instance;
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

/* Returns 0 on success, -1 on error (message to stderr). */
int vk_host_c_create_instance(VkHostC* host, const char** extensions, uint32_t ext_count);
int vk_host_c_select_device(VkHostC* host, VkSurfaceKHR surface);
int vk_host_c_create_device(VkHostC* host);

/* Per-frame: begin returns a target for nekomote_wallpaper_set_vk_target. */
void vk_host_c_wait_ready(VkHostC* host);
int vk_host_c_begin_frame(VkHostC* host, int w, int h, int present,
                          nekomote_vk_target* out_target);
int vk_host_c_present(VkHostC* host);

/* GPU readback for --capture. pixels must be w*h*4 bytes. Returns 0 on success. */
int vk_host_c_capture(VkHostC* host, int w, int h, uint8_t* pixels);

void vk_host_c_destroy(VkHostC* host);

#endif
