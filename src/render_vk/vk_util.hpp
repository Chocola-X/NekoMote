#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace nekomote::render_vk
{

inline std::uint32_t find_memory_type(VkPhysicalDevice physical_device, std::uint32_t type_filter,
                                      VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &props);
    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((type_filter & (1 << i)) &&
            (props.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    throw std::runtime_error("failed to find suitable memory type");
}

struct Buffer
{
    VkBuffer handle{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkDeviceSize size{0};
};

inline Buffer create_buffer(VkDevice device, VkPhysicalDevice physical_device, VkDeviceSize size,
                            VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
{
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    Buffer buf;
    buf.size = size;
    if (vkCreateBuffer(device, &info, nullptr, &buf.handle) != VK_SUCCESS)
        throw std::runtime_error("failed to create buffer");
    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(device, buf.handle, &reqs);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = reqs.size;
    alloc.memoryTypeIndex =
        find_memory_type(physical_device, reqs.memoryTypeBits, properties);
    if (vkAllocateMemory(device, &alloc, nullptr, &buf.memory) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate buffer memory");
    vkBindBufferMemory(device, buf.handle, buf.memory, 0);
    return buf;
}

inline void destroy_buffer(VkDevice device, Buffer& buf)
{
    if (buf.handle)
        vkDestroyBuffer(device, buf.handle, nullptr);
    if (buf.memory)
        vkFreeMemory(device, buf.memory, nullptr);
    buf = {};
}

struct Image
{
    VkImage handle{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    int width{0}, height{0};
    VkFormat format{VK_FORMAT_UNDEFINED};
};

inline Image create_image(VkDevice device, VkPhysicalDevice physical_device, int width, int height,
                          VkFormat format, VkImageUsageFlags usage,
                          VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT)
{
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent.width = static_cast<std::uint32_t>(width);
    info.extent.height = static_cast<std::uint32_t>(height);
    info.extent.depth = 1;
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    Image img;
    img.width = width;
    img.height = height;
    img.format = format;
    if (vkCreateImage(device, &info, nullptr, &img.handle) != VK_SUCCESS)
        throw std::runtime_error("failed to create image");
    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(device, img.handle, &reqs);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = reqs.size;
    alloc.memoryTypeIndex = find_memory_type(physical_device, reqs.memoryTypeBits,
                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &alloc, nullptr, &img.memory) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate image memory");
    vkBindImageMemory(device, img.handle, img.memory, 0);
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = img.handle;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = aspect;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &view_info, nullptr, &img.view) != VK_SUCCESS)
        throw std::runtime_error("failed to create image view");
    return img;
}

inline void destroy_image(VkDevice device, Image& img)
{
    if (img.view)
        vkDestroyImageView(device, img.view, nullptr);
    if (img.handle)
        vkDestroyImage(device, img.handle, nullptr);
    if (img.memory)
        vkFreeMemory(device, img.memory, nullptr);
    img = {};
}

inline VkCommandBuffer begin_one_time_commands(VkDevice device, VkCommandPool pool)
{
    VkCommandBufferAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool = pool;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &info, &cmd);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    return cmd;
}

inline void end_one_time_commands(VkDevice device, VkQueue queue, VkCommandPool pool,
                                  VkCommandBuffer cmd)
{
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
}

inline void transition_image_layout(VkDevice device, VkQueue queue, VkCommandPool pool,
                                    VkImage image, VkImageLayout old_layout,
                                    VkImageLayout new_layout)
{
    VkCommandBuffer cmd = begin_one_time_commands(device, pool);
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
        new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
             new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    else
        throw std::runtime_error("unsupported layout transition");
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
    end_one_time_commands(device, queue, pool, cmd);
}

inline void copy_buffer(VkDevice device, VkQueue queue, VkCommandPool pool, VkBuffer src,
                        VkBuffer dst, VkDeviceSize size)
{
    VkCommandBuffer cmd = begin_one_time_commands(device, pool);
    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    end_one_time_commands(device, queue, pool, cmd);
}

inline void copy_buffer_to_image(VkDevice device, VkQueue queue, VkCommandPool pool, VkBuffer src,
                                 VkImage dst, int width, int height)
{
    VkCommandBuffer cmd = begin_one_time_commands(device, pool);
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = 0;
    region.imageOffset.y = 0;
    region.imageOffset.z = 0;
    region.imageExtent.width = static_cast<std::uint32_t>(width);
    region.imageExtent.height = static_cast<std::uint32_t>(height);
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(cmd, src, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    end_one_time_commands(device, queue, pool, cmd);
}

inline VkShaderModule create_shader_module(VkDevice device, const unsigned char* code,
                                           std::size_t size)
{
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = reinterpret_cast<const std::uint32_t*>(code);
    VkShaderModule module;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
        throw std::runtime_error("failed to create shader module");
    return module;
}

inline VkSampler create_sampler(VkDevice device)
{
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    info.unnormalizedCoordinates = VK_FALSE;
    info.compareEnable = VK_FALSE;
    info.compareOp = VK_COMPARE_OP_ALWAYS;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VkSampler sampler;
    if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS)
        throw std::runtime_error("failed to create sampler");
    return sampler;
}

} // namespace nekomote::render_vk
