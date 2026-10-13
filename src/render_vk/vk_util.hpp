#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace nekomote::render_vk
{
inline constexpr std::uint64_t kGpuWaitNanoseconds = 5'000'000'000;

inline void check_vk(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": VkResult " + std::to_string(result));
}

inline VkShaderModule create_shader_module(VkDevice device, const unsigned char* bytes,
                                           std::size_t size)
{
    if (!bytes || !size || size % sizeof(std::uint32_t))
        throw std::runtime_error("invalid SPIR-V size");
    std::vector<std::uint32_t> words(size / sizeof(std::uint32_t));
    std::memcpy(words.data(), bytes, size);
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = words.data();
    VkShaderModule shader = VK_NULL_HANDLE;
    check_vk(vkCreateShaderModule(device, &info, nullptr, &shader), "create shader module");
    return shader;
}
enum class SamplerCoordinates
{
    Normalized,
    Texels
};
inline VkSampler create_sampler(VkDevice device,
                                SamplerCoordinates coordinates = SamplerCoordinates::Normalized)
{
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = info.addressModeV = info.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = 0;
    info.unnormalizedCoordinates = coordinates == SamplerCoordinates::Texels ? VK_TRUE : VK_FALSE;
    VkSampler sampler = VK_NULL_HANDLE;
    check_vk(vkCreateSampler(device, &info, nullptr, &sampler), "create sampler");
    return sampler;
}

inline std::uint32_t find_memory_type(VkPhysicalDevice physical_device, std::uint32_t filter,
                                      VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &props);
    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (props.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    throw std::runtime_error("Vulkan memory type unavailable");
}

struct Buffer
{
    VkDevice device{VK_NULL_HANDLE};
    VkBuffer handle{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkDeviceSize size{0};
    // Host-visible allocations stay mapped until reset. The host must complete
    // preceding GPU use before writing, replacing or destroying this buffer.
    void* mapped_data{nullptr};
    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept
    {
        swap(other);
    }
    Buffer& operator=(Buffer&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            swap(other);
        }
        return *this;
    }
    ~Buffer()
    {
        reset();
    }
    void reset() noexcept
    {
        if (mapped_data)
            vkUnmapMemory(device, memory);
        if (handle)
            vkDestroyBuffer(device, handle, nullptr);
        if (memory)
            vkFreeMemory(device, memory, nullptr);
        handle = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        size = 0;
        mapped_data = nullptr;
    }
    void swap(Buffer& other) noexcept
    {
        std::swap(device, other.device);
        std::swap(handle, other.handle);
        std::swap(memory, other.memory);
        std::swap(size, other.size);
        std::swap(mapped_data, other.mapped_data);
    }
};

inline Buffer create_buffer(VkDevice device, VkPhysicalDevice physical_device, VkDeviceSize size,
                            VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
{
    if (!size)
        throw std::runtime_error("zero Vulkan buffer size");
    Buffer b;
    b.device = device;
    b.size = size;
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check_vk(vkCreateBuffer(device, &info, nullptr, &b.handle), "create buffer");
    VkMemoryRequirements reqs{};
    vkGetBufferMemoryRequirements(device, b.handle, &reqs);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = reqs.size;
    alloc.memoryTypeIndex = find_memory_type(physical_device, reqs.memoryTypeBits, properties);
    check_vk(vkAllocateMemory(device, &alloc, nullptr, &b.memory), "allocate buffer");
    check_vk(vkBindBufferMemory(device, b.handle, b.memory, 0), "bind buffer");
    return b;
}

// Upload and frame buffers opt into one persistent, coherent host mapping.
// Capture/device buffers keep create_buffer's allocation-only ownership.
inline Buffer create_mapped_buffer(VkDevice device, VkPhysicalDevice physical_device,
                                   VkDeviceSize size, VkBufferUsageFlags usage)
{
    auto buffer =
        create_buffer(device, physical_device, size, usage,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped = nullptr;
    check_vk(vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "map buffer");
    buffer.mapped_data = mapped;
    return buffer;
}

struct Image
{
    VkDevice device{VK_NULL_HANDLE};
    VkImage handle{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    int width{0}, height{0};
    VkFormat format{VK_FORMAT_UNDEFINED};
    Image() = default;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&& other) noexcept
    {
        swap(other);
    }
    Image& operator=(Image&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            swap(other);
        }
        return *this;
    }
    ~Image()
    {
        reset();
    }
    void reset() noexcept
    {
        if (view)
            vkDestroyImageView(device, view, nullptr);
        if (handle)
            vkDestroyImage(device, handle, nullptr);
        if (memory)
            vkFreeMemory(device, memory, nullptr);
        view = VK_NULL_HANDLE;
        handle = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        width = height = 0;
        format = VK_FORMAT_UNDEFINED;
    }
    void swap(Image& other) noexcept
    {
        std::swap(device, other.device);
        std::swap(handle, other.handle);
        std::swap(memory, other.memory);
        std::swap(view, other.view);
        std::swap(width, other.width);
        std::swap(height, other.height);
        std::swap(format, other.format);
    }
};
inline Image create_image(VkDevice device, VkPhysicalDevice physical_device, int width, int height,
                          VkFormat format, VkImageUsageFlags usage,
                          VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT)
{
    if (width <= 0 || height <= 0)
        throw std::runtime_error("invalid Vulkan image size");
    Image image;
    image.device = device;
    image.width = width;
    image.height = height;
    image.format = format;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check_vk(vkCreateImage(device, &info, nullptr, &image.handle), "create image");
    VkMemoryRequirements reqs{};
    vkGetImageMemoryRequirements(device, image.handle, &reqs);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = reqs.size;
    alloc.memoryTypeIndex =
        find_memory_type(physical_device, reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check_vk(vkAllocateMemory(device, &alloc, nullptr, &image.memory), "allocate image");
    check_vk(vkBindImageMemory(device, image.handle, image.memory, 0), "bind image");
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = image.handle;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {aspect, 0, 1, 0, 1};
    check_vk(vkCreateImageView(device, &view, nullptr, &image.view), "create image view");
    return image;
}

struct LayoutAccess
{
    VkPipelineStageFlags stage;
    VkAccessFlags access;
};
inline LayoutAccess layout_access(VkImageLayout layout)
{
    switch (layout)
    {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0};
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return {VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT};
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT};
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT};
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0};
        default:
            throw std::runtime_error("unsupported Vulkan image layout");
    }
}
inline void record_image_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout,
                                 VkImageLayout new_layout)
{
    const auto src = layout_access(old_layout);
    const auto dst = layout_access(new_layout);
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = src.access;
    barrier.dstAccessMask = dst.access;
    vkCmdPipelineBarrier(cmd, src.stage, dst.stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
inline VkCommandBuffer begin_one_time_commands(VkDevice device, VkCommandPool pool)
{
    VkCommandBufferAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool = pool;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    check_vk(vkAllocateCommandBuffers(device, &info, &cmd), "allocate upload commands");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    const auto result = vkBeginCommandBuffer(cmd, &begin);
    if (result != VK_SUCCESS)
    {
        vkFreeCommandBuffers(device, pool, 1, &cmd);
        check_vk(result, "begin upload");
    }
    return cmd;
}
inline void end_one_time_commands(VkDevice device, VkQueue queue, VkCommandPool pool,
                                  VkCommandBuffer cmd)
{
    VkFence fence = VK_NULL_HANDLE;
    try
    {
        check_vk(vkEndCommandBuffer(cmd), "end upload");
        VkFenceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        check_vk(vkCreateFence(device, &info, nullptr, &fence), "create upload fence");
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        check_vk(vkQueueSubmit(queue, 1, &submit, fence), "submit upload");
        check_vk(vkWaitForFences(device, 1, &fence, VK_TRUE, kGpuWaitNanoseconds), "wait upload");
    }
    catch (...)
    {
        // Exceptional device/timeout path only; normal uploads use the fence.
        vkDeviceWaitIdle(device);
        if (fence)
            vkDestroyFence(device, fence, nullptr);
        vkFreeCommandBuffers(device, pool, 1, &cmd);
        throw;
    }
    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
}
inline VkRenderPass create_color_render_pass(VkDevice device, VkFormat format)
{
    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkSubpassDependency deps[2]{};
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
    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &attachment;
    info.subpassCount = 1;
    info.pSubpasses = &sub;
    info.dependencyCount = 2;
    info.pDependencies = deps;
    VkRenderPass pass = VK_NULL_HANDLE;
    check_vk(vkCreateRenderPass(device, &info, nullptr, &pass), "create color pass");
    return pass;
}
} // namespace nekomote::render_vk
