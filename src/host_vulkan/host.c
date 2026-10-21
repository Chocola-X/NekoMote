#include <nekomote/host_vulkan/host.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VK_CHECK(op, msg)                                                                          \
    do                                                                                             \
    {                                                                                              \
        VkResult _r = (op);                                                                        \
        if (_r != VK_SUCCESS)                                                                      \
        {                                                                                          \
            fprintf(stderr, "[host_vulkan] %s: VkResult %d\n", msg, (int)_r);                      \
            return -1;                                                                             \
        }                                                                                          \
    } while (0)

static const uint64_t GPU_WAIT_NS = 5000000000ULL;

static uint32_t find_memory_type(VkPhysicalDevice pd, uint32_t filter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(pd, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    fprintf(stderr, "[host_vulkan] no suitable memory type\n");
    return 0xFFFFFFFF;
}

static void record_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout old_l,
                           VkImageLayout new_l)
{
    VkPipelineStageFlags src_stage = 0, dst_stage = 0;
    VkAccessFlags src_access = 0, dst_access = 0;
    switch (old_l)
    {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            src_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            src_access = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            src_access = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            src_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            break;
        default:
            break;
    }
    switch (new_l)
    {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dst_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dst_access = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dst_access = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            break;
        default:
            break;
    }
    VkImageMemoryBarrier b = {0};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = old_l;
    b.newLayout = new_l;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    b.srcAccessMask = src_access;
    b.dstAccessMask = dst_access;
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &b);
}

static int create_output_image(VkHostC* h, int w, int hgt)
{
    if (h->framebuffer)
        vkDestroyFramebuffer(h->device, h->framebuffer, NULL);
    h->framebuffer = VK_NULL_HANDLE;

    if (h->output_view)
        vkDestroyImageView(h->device, h->output_view, NULL);
    if (h->output_image)
        vkDestroyImage(h->device, h->output_image, NULL);
    if (h->output_memory)
        vkFreeMemory(h->device, h->output_memory, NULL);
    h->output_view = VK_NULL_HANDLE;
    h->output_image = VK_NULL_HANDLE;
    h->output_memory = VK_NULL_HANDLE;

    VkImageCreateInfo img = {0};
    img.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img.imageType = VK_IMAGE_TYPE_2D;
    img.format = VK_FORMAT_R8G8B8A8_UNORM;
    img.extent.width = (uint32_t)w;
    img.extent.height = (uint32_t)hgt;
    img.extent.depth = 1;
    img.mipLevels = 1;
    img.arrayLayers = 1;
    img.samples = VK_SAMPLE_COUNT_1_BIT;
    img.tiling = VK_IMAGE_TILING_OPTIMAL;
    img.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    img.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(h->device, &img, NULL, &h->output_image), "create output image");

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(h->device, h->output_image, &req);
    uint32_t mt = find_memory_type(h->physical_device, req.memoryTypeBits,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt == 0xFFFFFFFF)
        return -1;
    VkMemoryAllocateInfo alloc = {0};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = mt;
    VK_CHECK(vkAllocateMemory(h->device, &alloc, NULL, &h->output_memory), "alloc output memory");
    VK_CHECK(vkBindImageMemory(h->device, h->output_image, h->output_memory, 0), "bind output");

    VkImageViewCreateInfo view = {0};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = h->output_image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_R8G8B8A8_UNORM;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(h->device, &view, NULL, &h->output_view), "create output view");

    VkFramebufferCreateInfo fb = {0};
    fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb.renderPass = h->render_pass;
    fb.attachmentCount = 1;
    fb.pAttachments = &h->output_view;
    fb.width = (uint32_t)w;
    fb.height = (uint32_t)hgt;
    fb.layers = 1;
    VK_CHECK(vkCreateFramebuffer(h->device, &fb, NULL, &h->framebuffer), "create framebuffer");

    h->output_w = w;
    h->output_h = hgt;
    h->output_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return 0;
}

static int create_swapchain(VkHostC* h, int w, int hgt)
{
    VK_CHECK(vkDeviceWaitIdle(h->device), "wait swapchain rebuild");
    h->pending = 0;

    for (uint32_t i = 0; i < h->swap_image_count; ++i)
        if (h->swap_finished[i])
            vkDestroySemaphore(h->device, h->swap_finished[i], NULL);
    free(h->swap_finished);
    h->swap_finished = NULL;
    free(h->swap_images);
    h->swap_images = NULL;
    h->swap_image_count = 0;
    if (h->swapchain)
        vkDestroySwapchainKHR(h->device, h->swapchain, NULL);
    h->swapchain = VK_NULL_HANDLE;

    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(h->physical_device, h->surface, &caps),
             "query surface caps");
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
    {
        fprintf(stderr, "[host_vulkan] surface cannot accept blit\n");
        return -1;
    }
    h->extent = caps.currentExtent;
    if (h->extent.width == 0xFFFFFFFF || h->extent.height == 0xFFFFFFFF)
    {
        uint32_t cw = (uint32_t)w;
        uint32_t ch = (uint32_t)hgt;
        if (h->extent.width == 0xFFFFFFFF)
            h->extent.width = cw < caps.minImageExtent.width   ? caps.minImageExtent.width
                              : cw > caps.maxImageExtent.width ? caps.maxImageExtent.width
                                                               : cw;
        if (h->extent.height == 0xFFFFFFFF)
            h->extent.height = ch < caps.minImageExtent.height   ? caps.minImageExtent.height
                               : ch > caps.maxImageExtent.height ? caps.maxImageExtent.height
                                                                 : ch;
    }
    if (!h->extent.width || !h->extent.height)
    {
        fprintf(stderr, "[host_vulkan] surface unavailable\n");
        return -1;
    }

    uint32_t n = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(h->physical_device, h->surface, &n, NULL),
             "query surface formats");
    if (!n)
    {
        fprintf(stderr, "[host_vulkan] no surface formats\n");
        return -1;
    }
    VkSurfaceFormatKHR* formats = malloc(sizeof(VkSurfaceFormatKHR) * n);
    if (!formats)
    {
        fprintf(stderr, "[host_vulkan] alloc failed: surface formats\n");
        return -1;
    }
    const VkResult read_formats =
        vkGetPhysicalDeviceSurfaceFormatsKHR(h->physical_device, h->surface, &n, formats);
    if (read_formats != VK_SUCCESS)
    {
        free(formats);
        fprintf(stderr, "[host_vulkan] read surface formats: VkResult %d\n", (int)read_formats);
        return -1;
    }
    VkSurfaceFormatKHR selected = formats[0];
    for (uint32_t i = 0; i < n; ++i)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
            formats[i].format == VK_FORMAT_R8G8B8A8_UNORM)
        {
            selected = formats[i];
            break;
        }
    free(formats);
    if (selected.format == VK_FORMAT_UNDEFINED)
        selected.format = VK_FORMAT_B8G8R8A8_UNORM;
    if (selected.format != VK_FORMAT_B8G8R8A8_UNORM && selected.format != VK_FORMAT_R8G8B8A8_UNORM)
    {
        fprintf(stderr, "[host_vulkan] no UNORM presentation format\n");
        return -1;
    }
    VkFormatProperties source_props, dest_props;
    vkGetPhysicalDeviceFormatProperties(h->physical_device, VK_FORMAT_R8G8B8A8_UNORM,
                                        &source_props);
    vkGetPhysicalDeviceFormatProperties(h->physical_device, selected.format, &dest_props);
    if (!(source_props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
        !(dest_props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
    {
        fprintf(stderr, "[host_vulkan] no GPU presentation blit support\n");
        return -1;
    }
    h->surface_format = selected.format;

    uint32_t image_count = caps.minImageCount < 2 ? 2 : caps.minImageCount;
    if (caps.maxImageCount)
        image_count = image_count > caps.maxImageCount ? caps.maxImageCount : image_count;

    VkSwapchainCreateInfoKHR info = {0};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = h->surface;
    info.minImageCount = image_count;
    info.imageFormat = selected.format;
    info.imageColorSpace = selected.colorSpace;
    info.imageExtent = h->extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    const VkCompositeAlphaFlagBitsKHR alpha_modes[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
    for (size_t i = 0; i < sizeof(alpha_modes) / sizeof(alpha_modes[0]); ++i)
        if (caps.supportedCompositeAlpha & alpha_modes[i])
        {
            info.compositeAlpha = alpha_modes[i];
            break;
        }
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    VK_CHECK(vkCreateSwapchainKHR(h->device, &info, NULL, &h->swapchain), "create swapchain");

    VK_CHECK(vkGetSwapchainImagesKHR(h->device, h->swapchain, &n, NULL), "query swap images");
    h->swap_images = malloc(sizeof(VkImage) * n);
    if (!h->swap_images)
    {
        fprintf(stderr, "[host_vulkan] alloc failed: swapchain images\n");
        return -1;
    }
    VK_CHECK(vkGetSwapchainImagesKHR(h->device, h->swapchain, &n, h->swap_images),
             "read swap images");
    h->swap_finished = calloc(n, sizeof(VkSemaphore));
    if (!h->swap_finished)
    {
        fprintf(stderr, "[host_vulkan] alloc failed: swap semaphores\n");
        free(h->swap_images);
        h->swap_images = NULL;
        return -1;
    }
    h->swap_image_count = n;
    for (uint32_t i = 0; i < n; ++i)
    {
        VkSemaphoreCreateInfo sem = {0};
        sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(h->device, &sem, NULL, &h->swap_finished[i]),
                 "create swap semaphore");
    }
    h->requested_w = w;
    h->requested_h = hgt;
    h->recreate = 0;
    return 0;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL validation_report(VkDebugReportFlagsEXT flags,
                                                        VkDebugReportObjectTypeEXT object_type,
                                                        uint64_t object, size_t location,
                                                        int32_t message_code, const char* layer,
                                                        const char* message, void* userdata)
{
    (void)object_type;
    (void)object;
    (void)location;
    (void)message_code;
    VkHostC* h = userdata;
    if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
        __atomic_fetch_add(&h->validation_errors, 1U, __ATOMIC_RELAXED);
    else
        __atomic_fetch_add(&h->validation_warnings, 1U, __ATOMIC_RELAXED);
    fprintf(stderr, "[host_vulkan][validation] %s: %s\n", layer, message);
    return VK_FALSE;
}

int vk_host_c_create_instance(VkHostC* h, const char** extensions, uint32_t ext_count)
{
    const char* validation = getenv("NEKOMOTE_VK_VALIDATION");
    const int enable_validation = validation && strcmp(validation, "1") == 0;
    if (!h || h->instance || (ext_count && !extensions) || ext_count == UINT32_MAX ||
        (validation && *validation && strcmp(validation, "0") && !enable_validation))
    {
        fprintf(stderr, "[host_vulkan] invalid instance input or NEKOMOTE_VK_VALIDATION (0|1)\n");
        return -1;
    }
    VkApplicationInfo app = {0};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "NekoMote";
    app.pEngineName = "NekoMote";
    app.apiVersion = VK_API_VERSION_1_0;

    VkDebugReportCallbackCreateInfoEXT callback = {0};
    callback.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    callback.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
                     VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
    callback.pfnCallback = validation_report;
    callback.pUserData = h;
    const char* layer = "VK_LAYER_KHRONOS_validation";
    VkInstanceCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = ext_count;
    info.ppEnabledExtensionNames = extensions;
    const char** enabled_extensions = NULL;
    if (enable_validation)
    {
        enabled_extensions = calloc((size_t)ext_count + 1, sizeof(*enabled_extensions));
        if (!enabled_extensions)
            return -1;
        if (ext_count)
            memcpy(enabled_extensions, extensions, (size_t)ext_count * sizeof(*extensions));
        enabled_extensions[ext_count] = VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
        info.enabledExtensionCount++;
        info.ppEnabledExtensionNames = enabled_extensions;
        info.enabledLayerCount = 1;
        info.ppEnabledLayerNames = &layer;
        info.pNext = &callback;
    }
    const VkResult result = vkCreateInstance(&info, NULL, &h->instance);
    free(enabled_extensions);
    if (result != VK_SUCCESS)
    {
        fprintf(stderr, "[host_vulkan] create instance: VkResult %d%s\n", (int)result,
                enable_validation ? " (Khronos validation required)" : "");
        return -1;
    }
    if (enable_validation)
    {
        PFN_vkCreateDebugReportCallbackEXT create_callback =
            (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(
                h->instance, "vkCreateDebugReportCallbackEXT");
        if (!create_callback)
        {
            fprintf(stderr, "[host_vulkan] validation callback unavailable\n");
            return -1;
        }
        VK_CHECK(create_callback(h->instance, &callback, NULL, &h->debug_callback),
                 "create validation callback");
        fprintf(stderr, "[host_vulkan] Khronos validation enabled\n");
    }
    return 0;
}

int vk_host_c_select_device(VkHostC* h, VkSurfaceKHR surface)
{
    h->surface = surface;
    uint32_t count = 0;
    VkPhysicalDevice* devices = NULL;
    VkExtensionProperties* extensions = NULL;
    VkQueueFamilyProperties* queues = NULL;
    int result = -1;
#define SELECT_CHECK(op, message)                                                                  \
    do                                                                                             \
    {                                                                                              \
        const VkResult status = (op);                                                              \
        if (status != VK_SUCCESS)                                                                  \
        {                                                                                          \
            fprintf(stderr, "[host_vulkan] %s: VkResult %d\n", message, (int)status);              \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
    SELECT_CHECK(vkEnumeratePhysicalDevices(h->instance, &count, NULL), "enumerate devices");
    if (!count)
        goto cleanup;
    devices = calloc(count, sizeof(*devices));
    if (!devices)
        goto cleanup;
    SELECT_CHECK(vkEnumeratePhysicalDevices(h->instance, &count, devices), "read devices");
    int best = -1;
    for (uint32_t d = 0; d < count; ++d)
    {
        uint32_t n = 0;
        SELECT_CHECK(vkEnumerateDeviceExtensionProperties(devices[d], NULL, &n, NULL),
                     "enumerate device extensions");
        if (!n)
            continue;
        extensions = calloc(n, sizeof(*extensions));
        if (!extensions)
            goto cleanup;
        SELECT_CHECK(vkEnumerateDeviceExtensionProperties(devices[d], NULL, &n, extensions),
                     "read device extensions");
        int has_swapchain = 0;
        for (uint32_t i = 0; i < n; ++i)
            if (strcmp(extensions[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                has_swapchain = 1;
        free(extensions);
        extensions = NULL;
        if (!has_swapchain)
            continue;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &n, NULL);
        if (!n)
            continue;
        queues = calloc(n, sizeof(*queues));
        if (!queues)
            goto cleanup;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &n, queues);
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[d], &properties);
        const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                          : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                                                                                            : 1;
        for (uint32_t i = 0; i < n; ++i)
        {
            VkBool32 present = VK_FALSE;
            SELECT_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], i, surface, &present),
                         "query presentation queue");
            if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present && score > best)
            {
                best = score;
                h->physical_device = devices[d];
                h->graphics_family = i;
            }
        }
        free(queues);
        queues = NULL;
    }
    if (h->physical_device)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(h->physical_device, &properties);
        fprintf(stderr, "[host_vulkan] Vulkan 1.0 device=%s\n", properties.deviceName);
        result = 0;
    }
cleanup:
    free(queues);
    free(extensions);
    free(devices);
    if (result)
        fprintf(stderr, "[host_vulkan] cannot select graphics/present device\n");
#undef SELECT_CHECK
    return result;
}

int vk_host_c_create_device(VkHostC* h)
{
    float priority = 1;
    VkDeviceQueueCreateInfo queue = {0};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = h->graphics_family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    const char* ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = &ext;
    VK_CHECK(vkCreateDevice(h->physical_device, &info, NULL, &h->device), "create device");
    vkGetDeviceQueue(h->device, h->graphics_family, 0, &h->graphics_queue);

    VkCommandPoolCreateInfo pool = {0};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = h->graphics_family;
    VK_CHECK(vkCreateCommandPool(h->device, &pool, NULL, &h->command_pool), "create command pool");

    VkCommandBufferAllocateInfo cmd = {0};
    cmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd.commandPool = h->command_pool;
    cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(h->device, &cmd, &h->commands), "alloc commands");

    VkFenceCreateInfo fence = {0};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(h->device, &fence, NULL, &h->fence), "create fence");

    VkSemaphoreCreateInfo sem = {0};
    sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(vkCreateSemaphore(h->device, &sem, NULL, &h->acquired), "create acquire semaphore");

    /* Render pass for RGBA8 UNORM, LOAD/STORE, compatible with render_vk::Renderer */
    VkAttachmentDescription attachment = {0};
    attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkSubpassDependency deps[2] = {0};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    deps[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    VkRenderPassCreateInfo rp = {0};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments = &attachment;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 2;
    rp.pDependencies = deps;
    VK_CHECK(vkCreateRenderPass(h->device, &rp, NULL, &h->render_pass), "create render pass");
    return 0;
}

int vk_host_c_wait_ready(VkHostC* h)
{
    if (h->pending)
    {
        VK_CHECK(vkWaitForFences(h->device, 1, &h->fence, VK_TRUE, GPU_WAIT_NS), "wait frame");
        h->pending = 0;
    }
    return 0;
}

int vk_host_c_begin_frame(VkHostC* h, int w, int hgt, int present, nekomote_vk_target* out_target)
{
    if (vk_host_c_wait_ready(h) != 0)
        return -1;
    if (h->recording)
    {
        fprintf(stderr, "[host_vulkan] frame already recording\n");
        return -1;
    }
    if (!out_target || w <= 0 || hgt <= 0)
    {
        fprintf(stderr, "[host_vulkan] invalid frame size\n");
        return -1;
    }
    if (present && (h->recreate || !h->swapchain || w != h->requested_w || hgt != h->requested_h))
    {
        if (create_swapchain(h, w, hgt) != 0)
            return -1;
    }
    if (h->output_w != w || h->output_h != hgt)
    {
        if (create_output_image(h, w, hgt) != 0)
            return -1;
    }
    h->present_frame = present;
    if (present)
    {
        VkResult r = vkAcquireNextImageKHR(h->device, h->swapchain, GPU_WAIT_NS, h->acquired,
                                           VK_NULL_HANDLE, &h->image_index);
        if (r == VK_ERROR_OUT_OF_DATE_KHR)
        {
            if (create_swapchain(h, w, hgt) != 0)
                return -1;
            r = vkAcquireNextImageKHR(h->device, h->swapchain, GPU_WAIT_NS, h->acquired,
                                      VK_NULL_HANDLE, &h->image_index);
        }
        if (r == VK_SUBOPTIMAL_KHR)
            h->recreate = 1;
        else if (r != VK_SUCCESS)
        {
            fprintf(stderr, "[host_vulkan] acquire: VkResult %d\n", (int)r);
            return -1;
        }
    }
    VK_CHECK(vkResetCommandBuffer(h->commands, 0), "reset commands");
    VkCommandBufferBeginInfo begin = {0};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(h->commands, &begin), "begin commands");
    record_barrier(h->commands, h->output_image, h->output_layout,
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    h->output_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    h->recording = 1;

    out_target->commands = h->commands;
    out_target->framebuffer = h->framebuffer;
    out_target->width = w;
    out_target->height = hgt;
    return 0;
}

static int submit(VkHostC* h)
{
    if (!h->recording)
        return 0;
    VkSemaphore finished = VK_NULL_HANDLE;
    if (h->present_frame)
    {
        finished = h->swap_finished[h->image_index];
        record_barrier(h->commands, h->output_image, h->output_layout,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkImage swap_image = h->swap_images[h->image_index];
        record_barrier(h->commands, swap_image, VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageBlit blit = {0};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = 1;
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.layerCount = 1;
        blit.srcOffsets[0].x = 0;
        blit.srcOffsets[0].y = h->output_h;
        blit.srcOffsets[1].x = h->output_w;
        blit.srcOffsets[1].y = 0;
        blit.srcOffsets[1].z = 1;
        blit.dstOffsets[1].x = (int32_t)h->extent.width;
        blit.dstOffsets[1].y = (int32_t)h->extent.height;
        blit.dstOffsets[1].z = 1;
        vkCmdBlitImage(h->commands, h->output_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       swap_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_NEAREST);
        record_barrier(h->commands, swap_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        record_barrier(h->commands, h->output_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    VK_CHECK(vkEndCommandBuffer(h->commands), "end commands");
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &h->commands;
    if (h->present_frame)
    {
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &h->acquired;
        si.pWaitDstStageMask = &wait_stage;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &finished;
    }
    VK_CHECK(vkResetFences(h->device, 1, &h->fence), "reset fence");
    VK_CHECK(vkQueueSubmit(h->graphics_queue, 1, &si, h->fence), "submit");
    h->recording = 0;
    h->pending = 1;
    return 0;
}

int vk_host_c_present(VkHostC* h)
{
    if (submit(h) != 0)
        return -1;
    if (!h->present_frame)
        return 0;
    VkSemaphore finished = h->swap_finished[h->image_index];
    VkPresentInfoKHR pi = {0};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &finished;
    pi.swapchainCount = 1;
    pi.pSwapchains = &h->swapchain;
    pi.pImageIndices = &h->image_index;
    VkResult r = vkQueuePresentKHR(h->graphics_queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR)
    {
        h->recreate = 1;
        h->present_frame = 0;
        return 1;
    }
    if (r == VK_SUBOPTIMAL_KHR)
        h->recreate = 1;
    else if (r != VK_SUCCESS)
    {
        fprintf(stderr, "[host_vulkan] present: VkResult %d\n", (int)r);
        return -1;
    }
    h->present_frame = 0;
    return 0;
}

int vk_host_c_capture(VkHostC* h, int w, int hgt, uint8_t* pixels)
{
    if (!pixels || w <= 0 || hgt <= 0 || (size_t)w > SIZE_MAX / 4 / (size_t)hgt ||
        w != h->output_w || hgt != h->output_h)
    {
        fprintf(stderr, "[host_vulkan] capture size mismatch\n");
        return -1;
    }
    if (submit(h) != 0)
        return -1;
    if (vk_host_c_wait_ready(h) != 0)
        return -1;

    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    int submitted = 0;
    int result = -1;
#define CAPTURE_CHECK(op, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        VkResult capture_result = (op);                                                            \
        if (capture_result != VK_SUCCESS)                                                          \
        {                                                                                          \
            fprintf(stderr, "[host_vulkan] %s: VkResult %d\n", msg, (int)capture_result);          \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
    size_t size = (size_t)w * (size_t)hgt * 4;
    VkBufferCreateInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CAPTURE_CHECK(vkCreateBuffer(h->device, &bi, NULL, &buffer), "create capture buffer");
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(h->device, buffer, &req);
    uint32_t mt = find_memory_type(h->physical_device, req.memoryTypeBits,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == 0xFFFFFFFF)
        goto cleanup;
    VkMemoryAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = mt;
    CAPTURE_CHECK(vkAllocateMemory(h->device, &ai, NULL, &mem), "alloc capture memory");
    CAPTURE_CHECK(vkBindBufferMemory(h->device, buffer, mem, 0), "bind capture buffer");

    VkCommandBufferAllocateInfo ca = {0};
    ca.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ca.commandPool = h->command_pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    CAPTURE_CHECK(vkAllocateCommandBuffers(h->device, &ca, &cmd), "alloc capture commands");
    VkCommandBufferBeginInfo cb = {0};
    cb.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cb.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CAPTURE_CHECK(vkBeginCommandBuffer(cmd, &cb), "begin capture commands");
    record_barrier(cmd, h->output_image, h->output_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy copy = {0};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = (uint32_t)w;
    copy.imageExtent.height = (uint32_t)hgt;
    copy.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, h->output_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                           &copy);
    record_barrier(cmd, h->output_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    CAPTURE_CHECK(vkEndCommandBuffer(cmd), "end capture commands");
    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VkFenceCreateInfo fi = {0};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    CAPTURE_CHECK(vkCreateFence(h->device, &fi, NULL, &fence), "create capture fence");
    CAPTURE_CHECK(vkQueueSubmit(h->graphics_queue, 1, &si, fence), "submit capture");
    submitted = 1;
    CAPTURE_CHECK(vkWaitForFences(h->device, 1, &fence, VK_TRUE, GPU_WAIT_NS), "wait capture");
    submitted = 0;

    void* mapped = NULL;
    CAPTURE_CHECK(vkMapMemory(h->device, mem, 0, size, 0, &mapped), "map capture");
    size_t stride = (size_t)w * 4;
    for (int y = 0; y < hgt; ++y)
        memcpy(pixels + (size_t)y * stride, (const uint8_t*)mapped + (size_t)(hgt - 1 - y) * stride,
               stride);
    vkUnmapMemory(h->device, mem);
    h->output_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    /* Unpremultiply alpha to match GL's PNG contract */
    for (size_t i = 0; i < size; i += 4)
    {
        uint8_t a = pixels[i + 3];
        if (a)
            for (size_t c = 0; c < 3; ++c)
            {
                unsigned v = (unsigned)pixels[i + c] * 255U + a / 2U;
                v /= a;
                pixels[i + c] = (uint8_t)(v > 255U ? 255U : v);
            }
    }

    result = 0;
cleanup:
    // A timed out copy may still reference these resources. Stop their device
    // work before freeing them; do not treat timeout as completed submission.
    if (submitted)
        vkDeviceWaitIdle(h->device);
    if (fence)
        vkDestroyFence(h->device, fence, NULL);
    if (cmd)
        vkFreeCommandBuffers(h->device, h->command_pool, 1, &cmd);
    if (buffer)
        vkDestroyBuffer(h->device, buffer, NULL);
    if (mem)
        vkFreeMemory(h->device, mem, NULL);
#undef CAPTURE_CHECK
    return result;
}

void vk_host_c_destroy(VkHostC* h)
{
    if (h->device)
        vkDeviceWaitIdle(h->device);
    h->pending = 0;
    if (h->framebuffer)
        vkDestroyFramebuffer(h->device, h->framebuffer, NULL);
    if (h->output_view)
        vkDestroyImageView(h->device, h->output_view, NULL);
    if (h->output_image)
        vkDestroyImage(h->device, h->output_image, NULL);
    if (h->output_memory)
        vkFreeMemory(h->device, h->output_memory, NULL);
    for (uint32_t i = 0; i < h->swap_image_count; ++i)
        if (h->swap_finished[i])
            vkDestroySemaphore(h->device, h->swap_finished[i], NULL);
    free(h->swap_finished);
    free(h->swap_images);
    if (h->swapchain)
        vkDestroySwapchainKHR(h->device, h->swapchain, NULL);
    if (h->render_pass)
        vkDestroyRenderPass(h->device, h->render_pass, NULL);
    if (h->acquired)
        vkDestroySemaphore(h->device, h->acquired, NULL);
    if (h->fence)
        vkDestroyFence(h->device, h->fence, NULL);
    if (h->command_pool)
        vkDestroyCommandPool(h->device, h->command_pool, NULL);
    if (h->device)
        vkDestroyDevice(h->device, NULL);
    if (h->surface)
        vkDestroySurfaceKHR(h->instance, h->surface, NULL);
    if (h->debug_callback)
    {
        PFN_vkDestroyDebugReportCallbackEXT destroy_callback =
            (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(
                h->instance, "vkDestroyDebugReportCallbackEXT");
        destroy_callback(h->instance, h->debug_callback, NULL);
        fprintf(stderr, "[host_vulkan] validation errors=%u warnings=%u\n", h->validation_errors,
                h->validation_warnings);
    }
    if (h->instance)
        vkDestroyInstance(h->instance, NULL);
    memset(h, 0, sizeof(*h));
}
