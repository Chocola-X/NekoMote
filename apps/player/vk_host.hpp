#pragma once

#include <nekomote/render_vk/renderer.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "vk_util.hpp"

namespace nekomote::player
{
enum class ValidationPolicy
{
    Disabled,
    IfAvailable,
    Required
};
#ifdef NDEBUG
inline constexpr auto kDefaultValidationPolicy = ValidationPolicy::Disabled;
#else
inline constexpr auto kDefaultValidationPolicy = ValidationPolicy::IfAvailable;
#endif
enum class PresentationPolicy
{
    Synchronized,
    ApplicationPacedMeasurement
};
// This host owns device/presentation and the sole frame submission. Backend
// resources borrow the device and command buffer; destroy them after wait_ready
// and before this host. A single in-flight frame keeps shared CPU/GPU ownership
// explicit; presentation semaphores are owned per swapchain image.
class VkHost
{
  public:
    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue graphics_queue{VK_NULL_HANDLE};
    std::uint32_t graphics_family{0};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    std::string device_name;
    bool validation_enabled{false};
    std::atomic<std::uint32_t> validation_errors{0};

    explicit VkHost(PresentationPolicy policy = PresentationPolicy::Synchronized)
        : presentation_policy_(policy)
    {
    }
    VkHost(const VkHost&) = delete;
    VkHost& operator=(const VkHost&) = delete;
    ~VkHost()
    {
        destroy();
    }

    void create_instance(std::vector<const char*> extensions,
                         ValidationPolicy policy = kDefaultValidationPolicy)
    {
        std::uint32_t count = 0;
        render_vk::check_vk(vkEnumerateInstanceLayerProperties(&count, nullptr),
                            "enumerate layers");
        std::vector<VkLayerProperties> layers(count);
        render_vk::check_vk(vkEnumerateInstanceLayerProperties(&count, layers.data()),
                            "read layers");
        const char* validation = "VK_LAYER_KHRONOS_validation";
        validation_enabled = policy != ValidationPolicy::Disabled &&
                             std::any_of(layers.begin(), layers.end(), [&](const auto& l)
                                         { return std::strcmp(l.layerName, validation) == 0; });
        if (policy == ValidationPolicy::Required && !validation_enabled)
            throw std::runtime_error("requested Vulkan validation layer is unavailable");
        if (validation_enabled)
            extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "NekoMote";
        app.pEngineName = "NekoMote";
        app.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        if (validation_enabled)
        {
            info.enabledLayerCount = 1;
            info.ppEnabledLayerNames = &validation;
        }
        render_vk::check_vk(vkCreateInstance(&info, nullptr, &instance),
                            "create Vulkan 1.0 instance");
        if (validation_enabled)
        {
            VkDebugReportCallbackCreateInfoEXT callback{};
            callback.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
            callback.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
            callback.pfnCallback = debug_report;
            callback.pUserData = this;
            auto create = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
            if (!create)
                throw std::runtime_error("Vulkan validation callback unavailable");
            render_vk::check_vk(create(instance, &callback, nullptr, &debug_callback_),
                                "create validation callback");
        }
    }

    void select_physical_device(VkSurfaceKHR owned_surface)
    {
        surface = owned_surface;
        std::uint32_t count = 0;
        render_vk::check_vk(vkEnumeratePhysicalDevices(instance, &count, nullptr),
                            "enumerate devices");
        std::vector<VkPhysicalDevice> devices(count);
        render_vk::check_vk(vkEnumeratePhysicalDevices(instance, &count, devices.data()),
                            "read devices");
        int best = -1;
        for (auto candidate : devices)
        {
            std::uint32_t n = 0;
            render_vk::check_vk(
                vkEnumerateDeviceExtensionProperties(candidate, nullptr, &n, nullptr),
                "enumerate device extensions");
            std::vector<VkExtensionProperties> extensions(n);
            render_vk::check_vk(
                vkEnumerateDeviceExtensionProperties(candidate, nullptr, &n, extensions.data()),
                "read device extensions");
            if (!std::any_of(
                    extensions.begin(), extensions.end(), [](const auto& e)
                    { return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0; }))
                continue;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
            std::vector<VkQueueFamilyProperties> queues(n);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, queues.data());
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                              : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                                                                                                : 1;
            for (std::uint32_t i = 0; i < n; ++i)
            {
                VkBool32 present = VK_FALSE;
                render_vk::check_vk(
                    vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present),
                    "query presentation queue");
                if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present && score > best)
                {
                    best = score;
                    physical_device = candidate;
                    graphics_family = i;
                    device_name = properties.deviceName;
                }
            }
        }
        if (!physical_device)
            throw std::runtime_error("no Vulkan graphics/presentation device");
    }

    void create_device()
    {
        float priority = 1;
        VkDeviceQueueCreateInfo queue{};
        queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex = graphics_family;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queue;
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = &extension;
        render_vk::check_vk(vkCreateDevice(physical_device, &info, nullptr, &device),
                            "create device");
        vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);
        VkCommandPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = graphics_family;
        render_vk::check_vk(vkCreateCommandPool(device, &pool, nullptr, &command_pool_),
                            "create frame pool");
        VkCommandBufferAllocateInfo commands{};
        commands.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commands.commandPool = command_pool_;
        commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commands.commandBufferCount = 1;
        render_vk::check_vk(vkAllocateCommandBuffers(device, &commands, &commands_),
                            "allocate frame commands");
        VkFenceCreateInfo fence{};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        render_vk::check_vk(vkCreateFence(device, &fence, nullptr, &fence_), "create frame fence");
        VkSemaphoreCreateInfo semaphore{};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        render_vk::check_vk(vkCreateSemaphore(device, &semaphore, nullptr, &acquired_),
                            "create acquire semaphore");
        render_pass_ = render_vk::create_color_render_pass(device, VK_FORMAT_R8G8B8A8_UNORM);
    }

    render_vk::DeviceContext context() const
    {
        return {instance,       physical_device, device,
                graphics_queue, graphics_family, VK_FORMAT_R8G8B8A8_UNORM};
    }

    void wait_ready()
    {
        if (pending_)
        {
            render_vk::check_vk(
                vkWaitForFences(device, 1, &fence_, VK_TRUE, render_vk::kGpuWaitNanoseconds),
                "wait frame completion");
            pending_ = false;
        }
    }
    void wait_noexcept() noexcept
    {
        if (device)
            vkDeviceWaitIdle(device);
        pending_ = false;
    }

    render_vk::Target begin_frame(int width, int height, bool present)
    {
        wait_ready();
        if (recording_)
            throw std::runtime_error("Vulkan frame already recording");
        if (width <= 0 || height <= 0)
            throw std::runtime_error("invalid Vulkan frame size");
        if (present &&
            (recreate_ || !swapchain_ || width != requested_width_ || height != requested_height_))
            create_swapchain(width, height);
        ensure_output(width, height);
        present_frame_ = present;
        if (present)
        {
            auto result = vkAcquireNextImageKHR(device, swapchain_, render_vk::kGpuWaitNanoseconds,
                                                acquired_, VK_NULL_HANDLE, &image_index_);
            if (result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                create_swapchain(width, height);
                result = vkAcquireNextImageKHR(device, swapchain_, render_vk::kGpuWaitNanoseconds,
                                               acquired_, VK_NULL_HANDLE, &image_index_);
            }
            if (result == VK_SUBOPTIMAL_KHR)
                recreate_ = true;
            else
                render_vk::check_vk(result, "acquire presentation image");
        }
        render_vk::check_vk(vkResetCommandBuffer(commands_, 0), "reset frame commands");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        render_vk::check_vk(vkBeginCommandBuffer(commands_, &begin), "begin frame commands");
        render_vk::record_image_barrier(commands_, output_.handle, output_layout_,
                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        output_layout_ = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        recording_ = true;
        return {commands_, framebuffer_, width, height};
    }

    void present()
    {
        submit();
        if (!present_frame_)
            return;
        auto& image = swap_images_[image_index_];
        VkPresentInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &image.finished;
        info.swapchainCount = 1;
        info.pSwapchains = &swapchain_;
        info.pImageIndices = &image_index_;
        const auto result = vkQueuePresentKHR(graphics_queue, &info);
        if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR)
            recreate_ = true;
        else
            render_vk::check_vk(result, "present frame");
        present_frame_ = false;
    }

    ImageRgba8 capture(int width, int height)
    {
        if (width != output_.width || height != output_.height)
            throw std::runtime_error("capture size differs from Vulkan target");
        submit();
        wait_ready();
        ImageRgba8 result;
        result.width = width;
        result.height = height;
        result.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                             4);
        auto buffer = render_vk::create_buffer(
            device, physical_device, result.pixels.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        auto commands = render_vk::begin_one_time_commands(device, command_pool_);
        render_vk::record_image_barrier(commands, output_.handle, output_layout_,
                                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                            1};
        vkCmdCopyImageToBuffer(commands, output_.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               buffer.handle, 1, &copy);
        render_vk::record_image_barrier(commands, output_.handle,
                                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        render_vk::end_one_time_commands(device, graphics_queue, command_pool_, commands);
        void* mapped = nullptr;
        render_vk::check_vk(vkMapMemory(device, buffer.memory, 0, result.pixels.size(), 0, &mapped),
                            "map captured pixels");
        const auto stride = static_cast<std::size_t>(width) * 4;
        for (int y = 0; y < height; ++y)
            std::memcpy(result.pixels.data() + static_cast<std::size_t>(y) * stride,
                        static_cast<const std::uint8_t*>(mapped) +
                            static_cast<std::size_t>(height - 1 - y) * stride,
                        stride);
        vkUnmapMemory(device, buffer.memory);
        output_layout_ = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // Match GL's PNG contract: GPU storage is premultiplied, PNG is straight.
        for (std::size_t i = 0; i < result.pixels.size(); i += 4)
        {
            const auto alpha = result.pixels[i + 3];
            if (alpha)
                for (std::size_t c = 0; c < 3; ++c)
                    result.pixels[i + c] = static_cast<std::uint8_t>(std::min(
                        255U,
                        (static_cast<unsigned>(result.pixels[i + c]) * 255U + alpha / 2U) / alpha));
        }
        return result;
    }

  private:
    PresentationPolicy presentation_policy_;
    struct SwapImage
    {
        VkImage image;
        VkSemaphore finished;
    };
    std::vector<SwapImage> swap_images_;
    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    VkFormat surface_format_{VK_FORMAT_UNDEFINED};
    VkExtent2D extent_{};
    int requested_width_{0}, requested_height_{0};
    bool recreate_{false}, pending_{false}, recording_{false}, present_frame_{false};
    std::uint32_t image_index_{0};
    VkCommandPool command_pool_{VK_NULL_HANDLE};
    VkCommandBuffer commands_{VK_NULL_HANDLE};
    VkFence fence_{VK_NULL_HANDLE};
    VkSemaphore acquired_{VK_NULL_HANDLE};
    VkRenderPass render_pass_{VK_NULL_HANDLE};
    VkFramebuffer framebuffer_{VK_NULL_HANDLE};
    render_vk::Image output_;
    VkImageLayout output_layout_{VK_IMAGE_LAYOUT_UNDEFINED};
    VkDebugReportCallbackEXT debug_callback_{VK_NULL_HANDLE};

    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(VkDebugReportFlagsEXT flags,
                                                       VkDebugReportObjectTypeEXT, std::uint64_t,
                                                       std::size_t, std::int32_t, const char*,
                                                       const char* message, void* user)
    {
        auto* host = static_cast<VkHost*>(user);
        if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
            ++host->validation_errors;
        std::fprintf(stderr, "Vulkan validation: %s\n", message);
        return VK_FALSE;
    }
    void ensure_output(int width, int height)
    {
        if (output_.width == width && output_.height == height)
            return;
        wait_ready();
        if (framebuffer_)
            vkDestroyFramebuffer(device, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
        output_ = render_vk::create_image(
            device, physical_device, width, height, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = render_pass_;
        info.attachmentCount = 1;
        info.pAttachments = &output_.view;
        info.width = static_cast<std::uint32_t>(width);
        info.height = static_cast<std::uint32_t>(height);
        info.layers = 1;
        render_vk::check_vk(vkCreateFramebuffer(device, &info, nullptr, &framebuffer_),
                            "create output framebuffer");
        output_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    void destroy_swapchain() noexcept
    {
        for (auto& image : swap_images_)
            if (image.finished)
                vkDestroySemaphore(device, image.finished, nullptr);
        swap_images_.clear();
        if (swapchain_)
            vkDestroySwapchainKHR(device, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    void create_swapchain(int width, int height)
    {
        render_vk::check_vk(vkDeviceWaitIdle(device), "wait swapchain recreation");
        pending_ = false;
        destroy_swapchain();
        VkSurfaceCapabilitiesKHR caps{};
        render_vk::check_vk(
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &caps),
            "query surface limits");
        if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
            throw std::runtime_error("Vulkan surface cannot accept GPU blit");
        extent_ = caps.currentExtent;
        if (extent_.width == std::numeric_limits<std::uint32_t>::max())
            extent_ = {std::clamp(static_cast<std::uint32_t>(width), caps.minImageExtent.width,
                                  caps.maxImageExtent.width),
                       std::clamp(static_cast<std::uint32_t>(height), caps.minImageExtent.height,
                                  caps.maxImageExtent.height)};
        if (!extent_.width || !extent_.height)
            throw std::runtime_error("Vulkan surface is unavailable");
        std::uint32_t n = 0;
        render_vk::check_vk(
            vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &n, nullptr),
            "query surface formats");
        std::vector<VkSurfaceFormatKHR> formats(n);
        render_vk::check_vk(
            vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &n, formats.data()),
            "read surface formats");
        if (formats.empty())
            throw std::runtime_error("Vulkan surface has no format");
        auto selected = formats.front();
        for (const auto& format : formats)
            if (format.format == VK_FORMAT_B8G8R8A8_UNORM ||
                format.format == VK_FORMAT_R8G8B8A8_UNORM)
            {
                selected = format;
                break;
            }
        if (selected.format == VK_FORMAT_UNDEFINED)
            selected.format = VK_FORMAT_B8G8R8A8_UNORM;
        if (selected.format != VK_FORMAT_B8G8R8A8_UNORM &&
            selected.format != VK_FORMAT_R8G8B8A8_UNORM)
            throw std::runtime_error(
                "Vulkan surface needs RGBA/BGRA8 UNORM for GL-matching output");
        surface_format_ = selected.format;
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physical_device, surface_format_, &properties);
        if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
            throw std::runtime_error("surface format cannot blit");
        vkGetPhysicalDeviceFormatProperties(physical_device, VK_FORMAT_R8G8B8A8_UNORM, &properties);
        if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
            throw std::runtime_error("RGBA8 cannot blit");
        std::uint32_t image_count = std::max(2u, caps.minImageCount);
        if (caps.maxImageCount)
            image_count = std::min(image_count, caps.maxImageCount);
        VkSwapchainCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface;
        info.minImageCount = image_count;
        info.imageFormat = selected.format;
        info.imageColorSpace = selected.colorSpace;
        info.imageExtent = extent_;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        for (const auto alpha :
             {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
              VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if (caps.supportedCompositeAlpha & alpha)
            {
                info.compositeAlpha = alpha;
                break;
            }
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        if (presentation_policy_ == PresentationPolicy::ApplicationPacedMeasurement)
        {
            // Match GL measurement's swap interval zero. FIFO can block an
            // unfocused X11 window in the presentation engine, outside our
            // application FPS budget and command-recording measurement.
            std::uint32_t mode_count = 0;
            render_vk::check_vk(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface,
                                                                          &mode_count, nullptr),
                                "query measurement present modes");
            std::vector<VkPresentModeKHR> modes(mode_count);
            render_vk::check_vk(vkGetPhysicalDeviceSurfacePresentModesKHR(
                                    physical_device, surface, &mode_count, modes.data()),
                                "read measurement present modes");
            if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) == modes.end())
                throw std::runtime_error("Vulkan measurement requires IMMEDIATE presentation");
            info.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
        info.clipped = VK_TRUE;
        render_vk::check_vk(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain_),
                            "create swapchain");
        render_vk::check_vk(vkGetSwapchainImagesKHR(device, swapchain_, &n, nullptr),
                            "query swapchain images");
        std::vector<VkImage> images(n);
        render_vk::check_vk(vkGetSwapchainImagesKHR(device, swapchain_, &n, images.data()),
                            "read swapchain images");
        swap_images_.reserve(n);
        for (auto image : images)
        {
            swap_images_.push_back({image, VK_NULL_HANDLE});
            VkSemaphoreCreateInfo semaphore{};
            semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            render_vk::check_vk(
                vkCreateSemaphore(device, &semaphore, nullptr, &swap_images_.back().finished),
                "create presentation semaphore");
        }
        requested_width_ = width;
        requested_height_ = height;
        recreate_ = false;
    }
    void submit()
    {
        if (!recording_)
            return;
        VkSemaphore finished = VK_NULL_HANDLE;
        if (present_frame_)
        {
            auto& image = swap_images_[image_index_];
            finished = image.finished;
            render_vk::record_image_barrier(commands_, output_.handle, output_layout_,
                                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            render_vk::record_image_barrier(commands_, image.image, VK_IMAGE_LAYOUT_UNDEFINED,
                                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageBlit blit{};
            blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.srcOffsets[0] = {0, output_.height, 0};
            blit.srcOffsets[1] = {output_.width, 0, 1};
            blit.dstOffsets[1] = {static_cast<std::int32_t>(extent_.width),
                                  static_cast<std::int32_t>(extent_.height), 1};
            vkCmdBlitImage(commands_, output_.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                           VK_FILTER_NEAREST);
            render_vk::record_image_barrier(commands_, image.image,
                                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
            render_vk::record_image_barrier(commands_, output_.handle,
                                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }
        render_vk::check_vk(vkEndCommandBuffer(commands_), "end frame commands");
        // The swap-image UNDEFINED layout transition has a TOP_OF_PIPE source
        // scope. Wait there too: waiting only at TRANSFER permits the transition
        // to race the presentation engine's acquire read (sync validation).
        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkSubmitInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &commands_;
        if (present_frame_)
        {
            info.waitSemaphoreCount = 1;
            info.pWaitSemaphores = &acquired_;
            info.pWaitDstStageMask = &wait_stage;
            info.signalSemaphoreCount = 1;
            info.pSignalSemaphores = &finished;
        }
        // A reset fence is always paired with the only submission that signals it.
        render_vk::check_vk(vkResetFences(device, 1, &fence_), "reset submitted fence");
        render_vk::check_vk(vkQueueSubmit(graphics_queue, 1, &info, fence_), "submit frame");
        recording_ = false;
        pending_ = true;
    }
    void destroy() noexcept
    {
        wait_noexcept();
        if (framebuffer_)
            vkDestroyFramebuffer(device, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
        output_.reset();
        destroy_swapchain();
        if (render_pass_)
            vkDestroyRenderPass(device, render_pass_, nullptr);
        if (acquired_)
            vkDestroySemaphore(device, acquired_, nullptr);
        if (fence_)
            vkDestroyFence(device, fence_, nullptr);
        if (command_pool_)
            vkDestroyCommandPool(device, command_pool_, nullptr);
        if (device)
            vkDestroyDevice(device, nullptr);
        if (surface)
            vkDestroySurfaceKHR(instance, surface, nullptr);
        if (debug_callback_)
        {
            auto destroy_callback = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));
            if (destroy_callback)
                destroy_callback(instance, debug_callback_, nullptr);
        }
        if (instance)
            vkDestroyInstance(instance, nullptr);
        device = VK_NULL_HANDLE;
        instance = VK_NULL_HANDLE;
    }
};

struct VkCompletionGuard
{
    VkHost* host{nullptr};
    ~VkCompletionGuard()
    {
        if (host)
            host->wait_noexcept();
    }
};
} // namespace nekomote::player
