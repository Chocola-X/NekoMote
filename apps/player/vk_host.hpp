#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace nekomote::player
{

struct VkHost
{
    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue graphics_queue{VK_NULL_HANDLE};
    std::uint32_t graphics_family{0};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    VkSwapchainKHR swapchain{VK_NULL_HANDLE};
    VkRenderPass render_pass{VK_NULL_HANDLE};
    VkCommandPool command_pool{VK_NULL_HANDLE};
    VkFormat surface_format{VK_FORMAT_R8G8B8A8_UNORM};
    VkExtent2D extent{};

    struct Frame
    {
        VkImage image{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VkFramebuffer framebuffer{VK_NULL_HANDLE};
        VkCommandBuffer command_buffer{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
        VkSemaphore acquire_semaphore{VK_NULL_HANDLE};
        VkSemaphore render_semaphore{VK_NULL_HANDLE};
    };
    std::vector<Frame> frames;
    std::uint32_t current_frame{0};

    std::string device_name;

    ~VkHost()
    {
        destroy();
    }

    void destroy()
    {
        if (device)
            vkDeviceWaitIdle(device);
        for (auto& f : frames)
        {
            if (f.framebuffer)
                vkDestroyFramebuffer(device, f.framebuffer, nullptr);
            if (f.view)
                vkDestroyImageView(device, f.view, nullptr);
            if (f.fence)
                vkDestroyFence(device, f.fence, nullptr);
            if (f.acquire_semaphore)
                vkDestroySemaphore(device, f.acquire_semaphore, nullptr);
            if (f.render_semaphore)
                vkDestroySemaphore(device, f.render_semaphore, nullptr);
        }
        frames.clear();
        if (command_pool)
            vkDestroyCommandPool(device, command_pool, nullptr);
        if (render_pass)
            vkDestroyRenderPass(device, render_pass, nullptr);
        if (swapchain)
            vkDestroySwapchainKHR(device, swapchain, nullptr);
        if (surface)
            vkDestroySurfaceKHR(instance, surface, nullptr);
        if (device)
            vkDestroyDevice(device, nullptr);
        if (instance)
            vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
    }

    void create_instance(const std::vector<const char*>& required_extensions)
    {
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "NekoMote";
        app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app.pEngineName = "NekoMote";
        app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        app.apiVersion = VK_API_VERSION_1_0;

        std::vector<const char*> layers;
#ifndef NDEBUG
        if (check_layer("VK_LAYER_KHRONOS_validation"))
            layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = static_cast<std::uint32_t>(required_extensions.size());
        info.ppEnabledExtensionNames = required_extensions.data();
        info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
        info.ppEnabledLayerNames = layers.data();
        if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS)
            throw std::runtime_error("failed to create Vulkan instance");
    }

    static bool check_layer(const char* name)
    {
        std::uint32_t count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> props(count);
        vkEnumerateInstanceLayerProperties(&count, props.data());
        for (const auto& p : props)
            if (std::string(p.layerName) == name)
                return true;
        return false;
    }

    void select_physical_device(VkSurfaceKHR surf)
    {
        surface = surf;
        std::uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance, &count, devices.data());
        for (const auto& dev : devices)
        {
            std::uint32_t qcount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, nullptr);
            std::vector<VkQueueFamilyProperties> qprops(qcount);
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, qprops.data());
            for (std::uint32_t i = 0; i < qcount; ++i)
            {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surf, &present);
                if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && present)
                {
                    physical_device = dev;
                    graphics_family = i;
                    VkPhysicalDeviceProperties props;
                    vkGetPhysicalDeviceProperties(dev, &props);
                    device_name = props.deviceName;
                    return;
                }
            }
        }
        throw std::runtime_error("no suitable Vulkan physical device");
    }

    void create_device()
    {
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info{};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = graphics_family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        std::vector<const char*> extensions;
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        VkDeviceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queue_info;
        info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        if (vkCreateDevice(physical_device, &info, nullptr, &device) != VK_SUCCESS)
            throw std::runtime_error("failed to create Vulkan device");
        vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);
    }

    void create_swapchain(int width, int height)
    {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &caps);
        extent = caps.currentExtent;
        if (extent.width == 0xFFFFFFFF)
        {
            extent.width = static_cast<std::uint32_t>(width);
            extent.height = static_cast<std::uint32_t>(height);
        }
        std::uint32_t format_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count,
                                              formats.data());
        surface_format = formats[0].format;
        for (const auto& f : formats)
            if (f.format == VK_FORMAT_R8G8B8A8_UNORM)
            {
                surface_format = f.format;
                break;
            }
        VkSwapchainCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface;
        info.minImageCount = std::max(2u, caps.minImageCount);
        info.imageFormat = surface_format;
        info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        if (vkCreateSwapchainKHR(device, &info, nullptr, &swapchain) != VK_SUCCESS)
            throw std::runtime_error("failed to create swapchain");
        std::uint32_t image_count = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr);
        std::vector<VkImage> images(image_count);
        vkGetSwapchainImagesKHR(device, swapchain, &image_count, images.data());
        create_render_pass();
        for (const auto& image : images)
        {
            Frame frame{};
            frame.image = image;
            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = image;
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = surface_format;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.layerCount = 1;
            if (vkCreateImageView(device, &view_info, nullptr, &frame.view) != VK_SUCCESS)
                throw std::runtime_error("failed to create swapchain image view");
            VkFramebufferCreateInfo fb_info{};
            fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb_info.renderPass = render_pass;
            fb_info.attachmentCount = 1;
            fb_info.pAttachments = &frame.view;
            fb_info.width = extent.width;
            fb_info.height = extent.height;
            fb_info.layers = 1;
            if (vkCreateFramebuffer(device, &fb_info, nullptr, &frame.framebuffer) != VK_SUCCESS)
                throw std::runtime_error("failed to create framebuffer");
            VkFenceCreateInfo fence_info{};
            fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            vkCreateFence(device, &fence_info, nullptr, &frame.fence);
            VkSemaphoreCreateInfo sem_info{};
            sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            vkCreateSemaphore(device, &sem_info, nullptr, &frame.acquire_semaphore);
            vkCreateSemaphore(device, &sem_info, nullptr, &frame.render_semaphore);
            frames.push_back(frame);
        }
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = graphics_family;
        if (vkCreateCommandPool(device, &pool_info, nullptr, &command_pool) != VK_SUCCESS)
            throw std::runtime_error("failed to create command pool");
        VkCommandBufferAllocateInfo cmd_info{};
        cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_info.commandPool = command_pool;
        cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_info.commandBufferCount = static_cast<std::uint32_t>(frames.size());
        std::vector<VkCommandBuffer> cmds(frames.size());
        vkAllocateCommandBuffers(device, &cmd_info, cmds.data());
        for (std::size_t i = 0; i < frames.size(); ++i)
            frames[i].command_buffer = cmds[i];
    }

    void create_render_pass()
    {
        VkAttachmentDescription attachment{};
        attachment.format = surface_format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &ref;
        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        if (vkCreateRenderPass(device, &info, nullptr, &render_pass) != VK_SUCCESS)
            throw std::runtime_error("failed to create render pass");
    }

    std::uint32_t acquire_next_image()
    {
        current_frame = (current_frame + 1) % static_cast<std::uint32_t>(frames.size());
        auto& f = frames[current_frame];
        vkWaitForFences(device, 1, &f.fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
        vkResetFences(device, 1, &f.fence);
        std::uint32_t image_index = 0;
        VkResult result = vkAcquireNextImageKHR(device, swapchain,
                                                 std::numeric_limits<std::uint64_t>::max(),
                                                 f.acquire_semaphore, VK_NULL_HANDLE, &image_index);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error("failed to acquire swapchain image");
        return image_index;
    }

    void present(std::uint32_t image_index)
    {
        auto& f = frames[current_frame];
        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &f.render_semaphore;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain;
        present_info.pImageIndices = &image_index;
        vkQueuePresentKHR(graphics_queue, &present_info);
    }
};

} // namespace nekomote::player
