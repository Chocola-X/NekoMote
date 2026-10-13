#include <nekomote/render_vk/renderer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "spirv_shaders.hpp"
#include "vk_util.hpp"

namespace nekomote::render_vk
{
namespace
{

struct PushConstants
{
    float output_size[2]{};
    float origin_x{0}, origin_y{0}, scale{1};
    std::int32_t flip_x{0};
    float projection[2]{};
    std::int32_t premultiply{0};
    std::int32_t protection_pass{0};
    std::int32_t mask_write{0};
    std::int32_t use_mask{0};
    float mask_output_size[2]{};
    float global_alpha{1};
    float padding{0};
    std::array<float, 4> atlas_texels{1, 1, 0, 0};
};
static_assert(offsetof(PushConstants, atlas_texels) == 64, "atlas UV layout mismatch");
static_assert(sizeof(PushConstants) == 80, "push constant layout mismatch");

enum PipelineIndex
{
    kPremultipliedOver = 0,
    kStraightOver,
    kProtectionPass,
    kMultiply,
    kMaskWrite,
    kBlit,
    kPipelineCount
};

VkPipelineLayout create_pipeline_layout(VkDevice device, VkDescriptorSetLayout set_layout)
{
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc_range.offset = 0;
    pc_range.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 1;
    info.pSetLayouts = &set_layout;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &pc_range;
    VkPipelineLayout layout;
    if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("failed to create pipeline layout");
    return layout;
}

VkPipelineLayout create_blit_pipeline_layout(VkDevice device, VkDescriptorSetLayout set_layout)
{
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc_range.offset = 0;
    pc_range.size = sizeof(float);
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 1;
    info.pSetLayouts = &set_layout;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &pc_range;
    VkPipelineLayout layout;
    if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("failed to create blit pipeline layout");
    return layout;
}

VkDescriptorSetLayout create_descriptor_set_layout(VkDevice device)
{
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<std::uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("failed to create descriptor set layout");
    return layout;
}

VkPipeline create_pipeline(VkDevice device, VkRenderPass render_pass, VkPipelineLayout layout,
                           VkShaderModule vert, VkShaderModule frag, VkBlendFactor src_factor,
                           VkBlendFactor dst_factor, VkBlendFactor src_alpha,
                           VkBlendFactor dst_alpha, bool no_vertex_input)
{
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(RenderVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::array<VkVertexInputAttributeDescription, 3> attrs{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset = offsetof(RenderVertex, x);
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset = offsetof(RenderVertex, u);
    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[2].offset = offsetof(RenderVertex, color);

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (!no_vertex_input)
    {
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrs.size());
        vertex_input.pVertexAttributeDescriptions = attrs.data();
    }

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = 1;
    viewport.height = 1;
    viewport.minDepth = 0;
    viewport.maxDepth = 1;
    VkRect2D scissor{};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = 1;
    scissor.extent.height = 1;
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = VK_TRUE;
    attachment.srcColorBlendFactor = src_factor;
    attachment.dstColorBlendFactor = dst_factor;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = src_alpha;
    attachment.dstAlphaBlendFactor = dst_alpha;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;

    std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT,
                                                 VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size());
    dynamic.pDynamicStates = dynamic_states.data();

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport_state;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    info.renderPass = render_pass;
    info.subpass = 0;
    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) !=
        VK_SUCCESS)
        throw std::runtime_error("failed to create pipeline");
    return pipeline;
}

struct Bounds
{
    float left{std::numeric_limits<float>::infinity()};
    float top{std::numeric_limits<float>::infinity()};
    float right{-std::numeric_limits<float>::infinity()};
    float bottom{-std::numeric_limits<float>::infinity()};
    void include(float x, float y) noexcept
    {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
    }
};

struct Clip
{
    int x{0}, y{0}, width{0}, height{0};
};

struct MaskBounds
{
    Bounds source, content;
};

Clip clip(const Bounds& bounds, const Target& target, const View& view) noexcept
{
    if (!std::isfinite(bounds.left))
        return {};
    constexpr float kMaskBorderPixels = 2;
    const auto integer_bound = [](float value, int maximum)
    { return static_cast<int>(std::clamp(value, 0.0f, static_cast<float>(maximum))); };
    const int x = integer_bound(
        std::floor(bounds.left * view.scale + view.origin_x) - kMaskBorderPixels, target.width);
    const int y = integer_bound(
        std::floor(bounds.top * view.scale + view.origin_y) - kMaskBorderPixels, target.height);
    const int right = integer_bound(
        std::ceil(bounds.right * view.scale + view.origin_x) + kMaskBorderPixels, target.width);
    const int bottom = integer_bound(
        std::ceil(bounds.bottom * view.scale + view.origin_y) + kMaskBorderPixels, target.height);
    int clip_x = x, clip_right = right;
    if (view.flip_x)
    {
        clip_x = target.width - right;
        clip_right = target.width - x;
    }
    return {clip_x, target.height - bottom, std::max(0, clip_right - clip_x),
            std::max(0, bottom - y)};
}

Clip intersect(Clip a, Clip b) noexcept
{
    const int x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    return {x, y, std::max(0, std::min(a.x + a.width, b.x + b.width) - x),
            std::max(0, std::min(a.y + a.height, b.y + b.height) - y)};
}

int floor_power_two(int value) noexcept
{
    int result = 1;
    while (result <= value / 2)
        result *= 2;
    return result;
}

} // namespace

struct Renderer::Impl
{
    DeviceContext ctx;
    VkCommandPool upload_pool{VK_NULL_HANDLE};
    VkRenderPass target_pass{VK_NULL_HANDLE}, color_pass{VK_NULL_HANDLE}, mask_pass{VK_NULL_HANDLE};
    VkFormat mask_format{VK_FORMAT_UNDEFINED};
    VkDescriptorSetLayout set_layout{VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
    VkPipelineLayout pipeline_layout{VK_NULL_HANDLE}, blit_pipeline_layout{VK_NULL_HANDLE};
    VkPipeline pipelines[kPipelineCount]{};
    VkShaderModule model_vert{VK_NULL_HANDLE}, model_frag{VK_NULL_HANDLE};
    VkShaderModule blit_vert{VK_NULL_HANDLE}, blit_frag{VK_NULL_HANDLE};
    VkSampler sampler{VK_NULL_HANDLE}, atlas_sampler{VK_NULL_HANDLE};
    Buffer vbo, ebo;
    Image dummy;
    struct Texture
    {
        TextureId id;
        Image image;
        std::array<float, 4> texel_transform;
        VkDescriptorSet unmasked{VK_NULL_HANDLE}, masked{VK_NULL_HANDLE};
    };
    std::vector<Texture> textures;
    std::vector<DrawCommand> commands, mask_commands;
    std::vector<MaskDefinition> masks;
    std::vector<MaskBounds> mask_bounds;
    std::size_t vertex_capacity{0}, index_capacity{0}, command_capacity{0}, texture_capacity{0};
    std::size_t mask_command_capacity{0}, mask_capacity{0}, descriptor_texture_capacity{0};
    ColorComposition composition{ColorComposition::PremultipliedOver};
    Image color_target, mask_target;
    VkFramebuffer color_framebuffer{VK_NULL_HANDLE}, mask_framebuffer{VK_NULL_HANDLE};
    VkDescriptorSet blit_set{VK_NULL_HANDLE};
    VkImageLayout color_layout{VK_IMAGE_LAYOUT_UNDEFINED}, mask_layout{VK_IMAGE_LAYOUT_UNDEFINED};
    int output_width{0}, output_height{0};
    std::uint32_t max_texture_size{0};
    std::string device_name;

    // The host has completed the preceding submission before destruction or
    // resource mutation. The backend neither owns nor waits on host fences.
    ~Impl()
    {
        const auto dev = ctx.device;
        if (color_framebuffer)
            vkDestroyFramebuffer(dev, color_framebuffer, nullptr);
        if (mask_framebuffer)
            vkDestroyFramebuffer(dev, mask_framebuffer, nullptr);
        for (auto pipeline : pipelines)
            if (pipeline)
                vkDestroyPipeline(dev, pipeline, nullptr);
        if (blit_pipeline_layout)
            vkDestroyPipelineLayout(dev, blit_pipeline_layout, nullptr);
        if (pipeline_layout)
            vkDestroyPipelineLayout(dev, pipeline_layout, nullptr);
        if (descriptor_pool)
            vkDestroyDescriptorPool(dev, descriptor_pool, nullptr);
        if (set_layout)
            vkDestroyDescriptorSetLayout(dev, set_layout, nullptr);
        if (atlas_sampler)
            vkDestroySampler(dev, atlas_sampler, nullptr);
        if (sampler)
            vkDestroySampler(dev, sampler, nullptr);
        if (model_vert)
            vkDestroyShaderModule(dev, model_vert, nullptr);
        if (model_frag)
            vkDestroyShaderModule(dev, model_frag, nullptr);
        if (blit_vert)
            vkDestroyShaderModule(dev, blit_vert, nullptr);
        if (blit_frag)
            vkDestroyShaderModule(dev, blit_frag, nullptr);
        if (target_pass)
            vkDestroyRenderPass(dev, target_pass, nullptr);
        if (color_pass)
            vkDestroyRenderPass(dev, color_pass, nullptr);
        if (mask_pass)
            vkDestroyRenderPass(dev, mask_pass, nullptr);
        if (upload_pool)
            vkDestroyCommandPool(dev, upload_pool, nullptr);
    }

    VkDescriptorSet allocate_descriptor_set()
    {
        VkDescriptorSetAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        info.descriptorPool = descriptor_pool;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &set_layout;
        VkDescriptorSet result{VK_NULL_HANDLE};
        check_vk(vkAllocateDescriptorSets(ctx.device, &info, &result), "allocate model descriptor");
        return result;
    }

    void write_descriptor_set(VkDescriptorSet set, VkImageView atlas, VkImageView mask,
                              VkSampler image_sampler)
    {
        std::array<VkWriteDescriptorSet, 2> writes{};
        std::array<VkDescriptorImageInfo, 2> images{};
        for (std::size_t i = 0; i < images.size(); ++i)
        {
            images[i].sampler = i == 0 ? image_sampler : sampler;
            images[i].imageView = i == 0 ? atlas : mask;
            images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = static_cast<std::uint32_t>(i);
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo = &images[i];
        }
        vkUpdateDescriptorSets(ctx.device, static_cast<std::uint32_t>(writes.size()), writes.data(),
                               0, nullptr);
    }

    const Texture* find_texture(TextureId id) const
    {
        const auto it = std::find_if(textures.begin(), textures.end(),
                                     [id](const Texture& t) { return t.id == id; });
        return it == textures.end() ? nullptr : &*it;
    }

    Image upload_image(ImageViewRgba8 image)
    {
        if (image.width <= 0 || image.height <= 0 ||
            static_cast<std::uint32_t>(image.width) > max_texture_size ||
            static_cast<std::uint32_t>(image.height) > max_texture_size ||
            static_cast<std::size_t>(image.width) > std::numeric_limits<std::size_t>::max() / 4 /
                                                        static_cast<std::size_t>(image.height) ||
            image.pixels.size() !=
                static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4)
            throw std::runtime_error("invalid or unsupported texture dimensions");
        auto staging = create_mapped_buffer(ctx.device, ctx.physical_device, image.pixels.size(),
                                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        std::memcpy(staging.mapped_data, image.pixels.data(), image.pixels.size());
        auto result = create_image(ctx.device, ctx.physical_device, image.width, image.height,
                                   VK_FORMAT_R8G8B8A8_UNORM,
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        const auto upload_commands = begin_one_time_commands(ctx.device, upload_pool);
        record_image_barrier(upload_commands, result.handle, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {static_cast<std::uint32_t>(image.width),
                            static_cast<std::uint32_t>(image.height), 1};
        vkCmdCopyBufferToImage(upload_commands, staging.handle, result.handle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        record_image_barrier(upload_commands, result.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        end_one_time_commands(ctx.device, ctx.graphics_queue, upload_pool, upload_commands);
        return result;
    }
};

Renderer::Renderer(const DeviceContext& ctx) : impl_(std::make_unique<Impl>())
{
    auto& p = *impl_;
    p.ctx = ctx;
    if (!ctx.device || !ctx.physical_device || !ctx.graphics_queue)
        throw std::runtime_error("invalid Vulkan device context");
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(ctx.physical_device, &props);
    p.device_name = props.deviceName;
    p.max_texture_size = props.limits.maxImageDimension2D;
    const VkFormatFeatureFlags mask_features =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    // The reference stores coverage in a five-bit green channel. An eight-bit
    // attachment is a semantic change, not an acceptable unsupported-format fallback.
    for (auto format : {VK_FORMAT_R5G5B5A1_UNORM_PACK16, VK_FORMAT_B5G5R5A1_UNORM_PACK16,
                        VK_FORMAT_A1R5G5B5_UNORM_PACK16})
    {
        VkFormatProperties support{};
        vkGetPhysicalDeviceFormatProperties(ctx.physical_device, format, &support);
        if ((support.optimalTilingFeatures & mask_features) == mask_features)
        {
            p.mask_format = format;
            break;
        }
    }
    if (p.mask_format == VK_FORMAT_UNDEFINED)
        throw std::runtime_error("five-bit linearly filtered mask coverage unsupported");
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = ctx.graphics_queue_family;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    check_vk(vkCreateCommandPool(ctx.device, &pool_info, nullptr, &p.upload_pool),
             "create upload pool");
    p.target_pass = create_color_render_pass(ctx.device, ctx.color_format);
    p.color_pass = create_color_render_pass(ctx.device, VK_FORMAT_R8G8B8A8_UNORM);
    p.mask_pass = create_color_render_pass(ctx.device, p.mask_format);
    p.set_layout = create_descriptor_set_layout(ctx.device);
    p.sampler = create_sampler(ctx.device);
    p.atlas_sampler = create_sampler(ctx.device, SamplerCoordinates::Texels);
    p.model_vert = create_shader_module(ctx.device, model_vert_spv, model_vert_size);
    p.model_frag = create_shader_module(ctx.device, model_frag_spv, model_frag_size);
    p.blit_vert = create_shader_module(ctx.device, blit_vert_spv, blit_vert_size);
    p.blit_frag = create_shader_module(ctx.device, blit_frag_spv, blit_frag_size);
    p.pipeline_layout = create_pipeline_layout(ctx.device, p.set_layout);
    p.blit_pipeline_layout = create_blit_pipeline_layout(ctx.device, p.set_layout);
    p.pipelines[kPremultipliedOver] =
        create_pipeline(ctx.device, p.target_pass, p.pipeline_layout, p.model_vert, p.model_frag,
                        VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                        VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, false);
    p.pipelines[kStraightOver] =
        create_pipeline(ctx.device, p.color_pass, p.pipeline_layout, p.model_vert, p.model_frag,
                        VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                        VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, false);
    p.pipelines[kProtectionPass] =
        create_pipeline(ctx.device, p.color_pass, p.pipeline_layout, p.model_vert, p.model_frag,
                        VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_DST_ALPHA,
                        VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, false);
    p.pipelines[kMultiply] =
        create_pipeline(ctx.device, p.color_pass, p.pipeline_layout, p.model_vert, p.model_frag,
                        VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                        VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, false);
    p.pipelines[kMaskWrite] = create_pipeline(
        ctx.device, p.mask_pass, p.pipeline_layout, p.model_vert, p.model_frag, VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, false);
    p.pipelines[kBlit] =
        create_pipeline(ctx.device, p.target_pass, p.blit_pipeline_layout, p.blit_vert, p.blit_frag,
                        VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                        VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, true);
    ImageRgba8 white;
    white.width = white.height = 1;
    white.pixels = {255, 255, 255, 255};
    p.dummy = p.upload_image(white);
}

Renderer::~Renderer() = default;

void Renderer::reserve(Capacity capacity)
{
    auto& p = *impl_;
    if (capacity.vertices > std::numeric_limits<std::size_t>::max() / sizeof(RenderVertex) ||
        capacity.indices > std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t) ||
        capacity.textures > (std::numeric_limits<std::uint32_t>::max() - 2U) / 4U)
        throw std::runtime_error("GPU capacity exceeds representable range");
    if (!p.descriptor_pool)
    {
        const auto set_count = static_cast<std::uint32_t>(capacity.textures * 2 + 1);
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, set_count * 2};
        VkDescriptorPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.maxSets = set_count;
        info.poolSizeCount = 1;
        info.pPoolSizes = &size;
        check_vk(vkCreateDescriptorPool(p.ctx.device, &info, nullptr, &p.descriptor_pool),
                 "create model descriptor pool");
        p.descriptor_texture_capacity = capacity.textures;
        p.blit_set = p.allocate_descriptor_set();
        p.write_descriptor_set(p.blit_set, p.dummy.view, p.dummy.view, p.sampler);
    }
    else if (capacity.textures > p.descriptor_texture_capacity)
        throw std::runtime_error("texture reserve exceeds established descriptor capacity");
    auto vbo = capacity.vertices ? create_mapped_buffer(p.ctx.device, p.ctx.physical_device,
                                                        capacity.vertices * sizeof(RenderVertex),
                                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
                                 : Buffer{};
    auto ebo = capacity.indices ? create_mapped_buffer(p.ctx.device, p.ctx.physical_device,
                                                       capacity.indices * sizeof(std::uint32_t),
                                                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
                                : Buffer{};
    p.mask_bounds.reserve(capacity.masks);
    p.mask_commands.reserve(capacity.mask_commands);
    p.masks.reserve(capacity.masks);
    p.commands.reserve(capacity.commands);
    p.textures.reserve(capacity.textures);
    p.vbo = std::move(vbo);
    p.ebo = std::move(ebo);
    p.vertex_capacity = capacity.vertices;
    p.index_capacity = capacity.indices;
    p.command_capacity = capacity.commands;
    p.texture_capacity = capacity.textures;
    p.mask_command_capacity = capacity.mask_commands;
    p.mask_capacity = capacity.masks;
}

void Renderer::upload_texture(TextureId id, ImageViewRgba8 image, TextureRegion region)
{
    auto& p = *impl_;
    if (!id.valid() || p.find_texture(id) || p.textures.size() >= p.texture_capacity)
        throw std::runtime_error("invalid, duplicate or excessive texture ID");
    if (!region.valid_for(image.width, image.height))
        throw std::runtime_error("invalid texture crop region");
    Impl::Texture texture;
    texture.id = id;
    texture.texel_transform = region.texel_transform(image.width, image.height);
    texture.image = p.upload_image(image);
    texture.unmasked = p.allocate_descriptor_set();
    texture.masked = p.allocate_descriptor_set();
    p.write_descriptor_set(texture.unmasked, texture.image.view, p.dummy.view, p.atlas_sampler);
    p.write_descriptor_set(texture.masked, texture.image.view,
                           p.mask_target.view ? p.mask_target.view : p.dummy.view, p.atlas_sampler);
    p.textures.push_back(std::move(texture));
}

void Renderer::prepare(RenderFrame frame)
{
    auto& p = *impl_;
    if (frame.composition != ColorComposition::PremultipliedOver &&
        frame.composition != ColorComposition::ProtectedStraight)
        throw std::runtime_error("unsupported color composition");
    if (frame.vertices.size() > p.vertex_capacity || frame.indices.size() > p.index_capacity ||
        frame.commands.size() > p.command_capacity ||
        frame.mask_commands.size() > p.mask_command_capacity ||
        frame.masks.size() > p.mask_capacity)
        throw std::runtime_error("frame exceeds reserved capacity");
    for (const auto& vertex : frame.vertices)
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.u) ||
            !std::isfinite(vertex.v) ||
            std::any_of(vertex.color.begin(), vertex.color.end(),
                        [](float v) { return !std::isfinite(v); }))
            throw std::runtime_error("nonfinite vertex");
    for (auto index : frame.indices)
        if (index >= frame.vertices.size())
            throw std::runtime_error("frame vertex index out of range");
    const auto validate = [&p, &frame](const DrawCommand& command)
    {
        if (!p.find_texture(command.texture) || command.first_index > frame.indices.size() ||
            command.index_count > frame.indices.size() - command.first_index)
            throw std::runtime_error("invalid draw command");
        if (command.protection != ColorProtection::None &&
            (command.protection != ColorProtection::PreserveTranslucent ||
             frame.composition != ColorComposition::ProtectedStraight))
            throw std::runtime_error("color protection requires straight composition");
        if (command.blend != BlendMode::Over &&
            (command.blend != BlendMode::Multiply ||
             frame.composition != ColorComposition::ProtectedStraight ||
             command.protection != ColorProtection::None))
            throw std::runtime_error("unsupported blend mode");
        if (command.mask.valid() && (command.mask.value > frame.masks.size() ||
                                     frame.composition != ColorComposition::ProtectedStraight))
            throw std::runtime_error("invalid mask reference");
    };
    for (const auto& command : frame.commands)
        validate(command);
    for (const auto& command : frame.mask_commands)
    {
        validate(command);
        if (command.mask.valid() || command.protection != ColorProtection::None ||
            command.blend != BlendMode::Over)
            throw std::runtime_error("nested mask commands are unverified");
    }
    for (const auto& mask : frame.masks)
        if (mask.first_command > frame.mask_commands.size() ||
            mask.command_count > frame.mask_commands.size() - mask.first_command)
            throw std::runtime_error("invalid mask command range");

    const auto bounds = [&frame](const DrawCommand& command)
    {
        Bounds result;
        for (std::uint32_t i = 0; i < command.index_count; ++i)
        {
            const auto& vertex = frame.vertices[frame.indices[command.first_index + i]];
            result.include(vertex.x, vertex.y);
        }
        return result;
    };
    p.mask_bounds.clear();
    for (const auto& mask : frame.masks)
    {
        Bounds result;
        for (std::uint32_t i = 0; i < mask.command_count; ++i)
        {
            const auto bound = bounds(frame.mask_commands[mask.first_command + i]);
            if (std::isfinite(bound.left))
            {
                result.include(bound.left, bound.top);
                result.include(bound.right, bound.bottom);
            }
        }
        p.mask_bounds.push_back({result, {}});
    }
    for (const auto& command : frame.commands)
        if (command.mask.valid())
        {
            const auto b = bounds(command);
            if (std::isfinite(b.left))
            {
                auto& content = p.mask_bounds[command.mask.value - 1].content;
                content.include(b.left, b.top);
                content.include(b.right, b.bottom);
            }
        }

    const auto upload = [](Buffer& buffer, const void* bytes, std::size_t size)
    {
        if (!size)
            return;
        std::memcpy(buffer.mapped_data, bytes, size);
    };
    upload(p.vbo, frame.vertices.data(), frame.vertices.size() * sizeof(RenderVertex));
    upload(p.ebo, frame.indices.data(), frame.indices.size() * sizeof(std::uint32_t));
    p.commands.assign(frame.commands.begin(), frame.commands.end());
    p.mask_commands.assign(frame.mask_commands.begin(), frame.mask_commands.end());
    p.masks.assign(frame.masks.begin(), frame.masks.end());
    p.composition = frame.composition;
}

void Renderer::resize_output(int width, int height)
{
    auto& p = *impl_;
    if (p.composition == ColorComposition::PremultipliedOver || width <= 0 || height <= 0)
        return;
    if (width == p.output_width && height == p.output_height && p.color_target.handle)
        return;
    if (!p.blit_set)
        throw std::runtime_error("reserve resources before protected output resize");
    if (static_cast<std::uint32_t>(width) > p.max_texture_size ||
        static_cast<std::uint32_t>(height) > p.max_texture_size)
        throw std::runtime_error("output exceeds GPU texture dimensions");
    auto color =
        create_image(p.ctx.device, p.ctx.physical_device, width, height, VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    auto mask = create_image(p.ctx.device, p.ctx.physical_device, floor_power_two(width),
                             floor_power_two(height), p.mask_format,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    const auto make_fb = [&p](const Image& image, VkRenderPass pass, VkFramebuffer* result)
    {
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = pass;
        info.attachmentCount = 1;
        info.pAttachments = &image.view;
        info.width = static_cast<std::uint32_t>(image.width);
        info.height = static_cast<std::uint32_t>(image.height);
        info.layers = 1;
        check_vk(vkCreateFramebuffer(p.ctx.device, &info, nullptr, result),
                 "create intermediate framebuffer");
    };
    VkFramebuffer new_color{VK_NULL_HANDLE}, new_mask{VK_NULL_HANDLE};
    make_fb(color, p.color_pass, &new_color);
    try
    {
        make_fb(mask, p.mask_pass, &new_mask);
    }
    catch (...)
    {
        vkDestroyFramebuffer(p.ctx.device, new_color, nullptr);
        throw;
    }
    if (p.color_framebuffer)
        vkDestroyFramebuffer(p.ctx.device, p.color_framebuffer, nullptr);
    if (p.mask_framebuffer)
        vkDestroyFramebuffer(p.ctx.device, p.mask_framebuffer, nullptr);
    p.color_framebuffer = new_color;
    p.mask_framebuffer = new_mask;
    p.color_target = std::move(color);
    p.mask_target = std::move(mask);
    p.color_layout = p.mask_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    p.output_width = width;
    p.output_height = height;
    p.write_descriptor_set(p.blit_set, p.color_target.view, p.dummy.view, p.sampler);
    for (const auto& texture : p.textures)
        p.write_descriptor_set(texture.masked, texture.image.view, p.mask_target.view,
                               p.atlas_sampler);
}

void Renderer::render(Target target, const View& view, TargetContents contents)
{
    if (contents != TargetContents::Clear && contents != TargetContents::Preserve)
        throw std::runtime_error("invalid target contents policy");
    if (target.width <= 0 || target.height <= 0)
        return;
    if (!target.commands || !target.framebuffer)
        throw std::runtime_error("invalid Vulkan output target");
    if (!std::isfinite(view.origin_x) || !std::isfinite(view.origin_y) ||
        !std::isfinite(view.scale) || view.scale <= 0 || !std::isfinite(view.alpha) ||
        view.alpha < 0 || view.alpha > 1 ||
        std::any_of(view.clear.begin(), view.clear.end(),
                    [](float v) { return !std::isfinite(v); }))
        throw std::runtime_error("invalid output view");
    auto& p = *impl_;
    const bool straight = p.composition == ColorComposition::ProtectedStraight;
    if (straight && (!p.color_target.handle || p.output_width != target.width ||
                     p.output_height != target.height))
        throw std::runtime_error("protected composition requires explicit output resize");
    const auto cmd = target.commands;
    const Clip full{0, 0, target.width, target.height};
    const auto set_scissor = [cmd](Clip clip)
    {
        VkRect2D rect{
            {clip.x, clip.y},
            {static_cast<std::uint32_t>(clip.width), static_cast<std::uint32_t>(clip.height)}};
        vkCmdSetScissor(cmd, 0, 1, &rect);
    };
    const auto begin = [&](VkRenderPass pass, VkFramebuffer fb, int width, int height, bool clear,
                           std::array<float, 4> rgba)
    {
        VkRenderPassBeginInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = pass;
        info.framebuffer = fb;
        info.renderArea.extent = {static_cast<std::uint32_t>(width),
                                  static_cast<std::uint32_t>(height)};
        vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{};
        viewport.width = static_cast<float>(width);
        viewport.height = static_cast<float>(height);
        viewport.maxDepth = 1;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        set_scissor({0, 0, width, height});
        // Clear/Preserve is an explicit operation; changing clearValueCount does
        // not alter the attachment's LOAD operation.
        if (clear)
        {
            VkClearAttachment attachment{};
            attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            std::copy(rgba.begin(), rgba.end(), attachment.clearValue.color.float32);
            VkClearRect rect{};
            rect.rect.extent = info.renderArea.extent;
            rect.layerCount = 1;
            vkCmdClearAttachments(cmd, 1, &attachment, 1, &rect);
        }
    };
    if (straight)
    {
        record_image_barrier(cmd, p.color_target.handle, p.color_layout,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        p.color_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        begin(p.color_pass, p.color_framebuffer, target.width, target.height, true, {});
    }
    else
        begin(p.target_pass, target.framebuffer, target.width, target.height,
              contents == TargetContents::Clear, view.clear);

    if (p.vbo.handle && p.ebo.handle)
    {
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &p.vbo.handle, &offset);
        vkCmdBindIndexBuffer(cmd, p.ebo.handle, 0, VK_INDEX_TYPE_UINT32);
    }
    PushConstants pc{};
    pc.output_size[0] = static_cast<float>(target.width);
    pc.output_size[1] = static_cast<float>(target.height);
    pc.origin_x = view.origin_x;
    pc.origin_y = view.origin_y;
    pc.scale = view.scale;
    pc.flip_x = view.flip_x ? 1 : 0;
    pc.projection[0] = 2 / static_cast<float>(target.width);
    pc.projection[1] = -2 / static_cast<float>(target.height);
    pc.mask_output_size[0] = static_cast<float>(target.width);
    pc.mask_output_size[1] = static_cast<float>(target.height);
    pc.global_alpha = straight ? 1 : view.alpha;
    const auto push = [&]()
    {
        vkCmdPushConstants(cmd, p.pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc),
                           &pc);
    };
    for (const auto& command : p.commands)
    {
        if (!command.index_count)
            continue;
        if (command.mask.valid())
        {
            vkCmdEndRenderPass(cmd);
            record_image_barrier(cmd, p.mask_target.handle, p.mask_layout,
                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            p.mask_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            begin(p.mask_pass, p.mask_framebuffer, p.mask_target.width, p.mask_target.height, true,
                  {});
            const auto& bounds = p.mask_bounds[command.mask.value - 1];
            const auto output_clip = intersect(
                intersect(clip(bounds.source, target, view), clip(bounds.content, target, view)),
                full);
            // Internal framebuffer rows follow GL's lower-left arithmetic.
            const auto scaled = [](int value, int size, int original)
            { return static_cast<int>(static_cast<std::int64_t>(value) * size / original); };
            set_scissor({scaled(output_clip.x, p.mask_target.width, target.width),
                         scaled(output_clip.y, p.mask_target.height, target.height),
                         scaled(output_clip.width, p.mask_target.width, target.width),
                         scaled(output_clip.height, p.mask_target.height, target.height)});
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipelines[kMaskWrite]);
            pc.mask_write = 1;
            pc.use_mask = 0;
            const auto& definition = p.masks[command.mask.value - 1];
            for (std::uint32_t i = 0; i < definition.command_count; ++i)
            {
                const auto& draw = p.mask_commands[definition.first_command + i];
                if (!draw.index_count)
                    continue;
                const auto* texture = p.find_texture(draw.texture);
                pc.atlas_texels = texture->texel_transform;
                push();
                // Dummy mask prevents sampling an attachment currently being written.
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline_layout, 0,
                                        1, &texture->unmasked, 0, nullptr);
                vkCmdDrawIndexed(cmd, draw.index_count, 1, draw.first_index, 0, 0);
            }
            vkCmdEndRenderPass(cmd);
            record_image_barrier(cmd, p.mask_target.handle, p.mask_layout,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            p.mask_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            begin(p.color_pass, p.color_framebuffer, target.width, target.height, false, {});
            set_scissor(output_clip);
            pc.mask_write = 0;
            pc.use_mask = 1;
        }
        else
        {
            set_scissor(full);
            pc.use_mask = 0;
        }
        const auto* texture = p.find_texture(command.texture);
        pc.atlas_texels = texture->texel_transform;
        const auto set = command.mask.valid() ? texture->masked : texture->unmasked;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline_layout, 0, 1, &set,
                                0, nullptr);
        if (straight && command.protection == ColorProtection::PreserveTranslucent)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipelines[kProtectionPass]);
            pc.protection_pass = 1;
            pc.premultiply = 0;
            push();
            vkCmdDrawIndexed(cmd, command.index_count, 1, command.first_index, 0, 0);
            pc.protection_pass = 0;
        }
        const auto pipe = command.blend == BlendMode::Multiply
                              ? kMultiply
                              : (straight ? kStraightOver : kPremultipliedOver);
        pc.premultiply = command.blend == BlendMode::Multiply || !straight ? 1 : 0;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipelines[pipe]);
        push();
        vkCmdDrawIndexed(cmd, command.index_count, 1, command.first_index, 0, 0);
    }
    vkCmdEndRenderPass(cmd);
    if (straight)
    {
        record_image_barrier(cmd, p.color_target.handle, p.color_layout,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        p.color_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        begin(p.target_pass, target.framebuffer, target.width, target.height,
              contents == TargetContents::Clear, view.clear);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipelines[kBlit]);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.blit_pipeline_layout, 0, 1,
                                &p.blit_set, 0, nullptr);
        vkCmdPushConstants(cmd, p.blit_pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(float), &view.alpha);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }
}

std::string Renderer::diagnostics() const
{
    return "Vulkan 1.0 | " + impl_->device_name;
}

} // namespace nekomote::render_vk
