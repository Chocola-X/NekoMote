#include <nekomote/render_vk/renderer.hpp>

#include "vk_util.hpp"
#include "spirv_shaders.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

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
};
static_assert(sizeof(PushConstants) == 60, "push constant layout mismatch");

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

VkRenderPass create_render_pass(VkDevice device, VkFormat format)
{
    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
    VkRenderPass pass;
    if (vkCreateRenderPass(device, &info, nullptr, &pass) != VK_SUCCESS)
        throw std::runtime_error("failed to create render pass");
    return pass;
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

VkPipeline create_pipeline(VkDevice device, VkRenderPass render_pass,
                           VkPipelineLayout layout, VkShaderModule vert, VkShaderModule frag,
                           VkBlendFactor src_factor, VkBlendFactor dst_factor,
                           VkBlendFactor src_alpha, VkBlendFactor dst_alpha, bool no_vertex_input)
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
    assembly.topology = no_vertex_input ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
                                        : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

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
        static_cast<float>(target.height) - std::ceil(bounds.bottom * view.scale + view.origin_y) -
            kMaskBorderPixels,
        target.height);
    const int right = integer_bound(
        std::ceil(bounds.right * view.scale + view.origin_x) + kMaskBorderPixels, target.width);
    const int top = integer_bound(
        static_cast<float>(target.height) - std::floor(bounds.top * view.scale + view.origin_y) +
            kMaskBorderPixels,
        target.height);
    int clip_x = x, clip_right = right;
    if (view.flip_x)
    {
        clip_x = target.width - right;
        clip_right = target.width - x;
    }
    return {clip_x, y, std::max(0, clip_right - clip_x), std::max(0, top - y)};
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
    VkCommandPool command_pool{VK_NULL_HANDLE};
    VkCommandBuffer command_buffer{VK_NULL_HANDLE};
    VkFence fence{VK_NULL_HANDLE};
    VkRenderPass render_pass{VK_NULL_HANDLE};
    VkDescriptorSetLayout set_layout{VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
    VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
    VkPipelineLayout blit_pipeline_layout{VK_NULL_HANDLE};
    VkPipeline pipelines[kPipelineCount]{};
    VkShaderModule model_vert{VK_NULL_HANDLE}, model_frag{VK_NULL_HANDLE};
    VkShaderModule blit_vert{VK_NULL_HANDLE}, blit_frag{VK_NULL_HANDLE};
    VkSampler sampler{VK_NULL_HANDLE};

    Buffer vbo, ebo;
    std::size_t vertex_capacity{0}, index_capacity{0}, command_capacity{0}, texture_capacity{0};
    std::size_t mask_command_capacity{0}, mask_capacity{0};

    struct Texture
    {
        TextureId id;
        Image image;
        VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
    };
    std::vector<Texture> textures;

    std::vector<DrawCommand> commands, mask_commands;
    std::vector<MaskDefinition> masks;
    std::vector<MaskBounds> mask_bounds;
    ColorComposition composition{ColorComposition::PremultipliedOver};

    Image color_target;
    VkFramebuffer color_framebuffer{VK_NULL_HANDLE};
    Image mask_target;
    VkFramebuffer mask_framebuffer{VK_NULL_HANDLE};
    VkDescriptorSet mask_descriptor_set{VK_NULL_HANDLE};
    int output_width{0}, output_height{0};

    std::string device_name;

    ~Impl()
    {
        const auto dev = ctx.device;
        if (fence)
            vkDestroyFence(dev, fence, nullptr);
        if (color_framebuffer)
            vkDestroyFramebuffer(dev, color_framebuffer, nullptr);
        if (mask_framebuffer)
            vkDestroyFramebuffer(dev, mask_framebuffer, nullptr);
        destroy_image(dev, color_target);
        destroy_image(dev, mask_target);
        for (auto& t : textures)
        {
            destroy_image(dev, t.image);
        }
        destroy_buffer(dev, vbo);
        destroy_buffer(dev, ebo);
        for (int i = 0; i < kPipelineCount; ++i)
            if (pipelines[i])
                vkDestroyPipeline(dev, pipelines[i], nullptr);
        if (blit_pipeline_layout)
            vkDestroyPipelineLayout(dev, blit_pipeline_layout, nullptr);
        if (pipeline_layout)
            vkDestroyPipelineLayout(dev, pipeline_layout, nullptr);
        if (descriptor_pool)
            vkDestroyDescriptorPool(dev, descriptor_pool, nullptr);
        if (set_layout)
            vkDestroyDescriptorSetLayout(dev, set_layout, nullptr);
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
        if (render_pass)
            vkDestroyRenderPass(dev, render_pass, nullptr);
        if (command_pool)
            vkDestroyCommandPool(dev, command_pool, nullptr);
    }

    VkDescriptorSet allocate_descriptor_set()
    {
        VkDescriptorSetAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        info.descriptorPool = descriptor_pool;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &set_layout;
        VkDescriptorSet set;
        if (vkAllocateDescriptorSets(ctx.device, &info, &set) != VK_SUCCESS)
            throw std::runtime_error("failed to allocate descriptor set");
        return set;
    }

    void write_descriptor_set(VkDescriptorSet set, VkImageView view0, VkImageView view1)
    {
        std::array<VkWriteDescriptorSet, 2> writes{};
        std::array<VkDescriptorImageInfo, 2> image_infos{};
        image_infos[0].sampler = sampler;
        image_infos[0].imageView = view0;
        image_infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_infos[1].sampler = sampler;
        image_infos[1].imageView = view1;
        image_infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = set;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &image_infos[0];
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = set;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &image_infos[1];
        vkUpdateDescriptorSets(ctx.device, static_cast<std::uint32_t>(writes.size()), writes.data(),
                               0, nullptr);
    }

    const Texture* find_texture(TextureId id) const
    {
        for (const auto& t : textures)
            if (t.id == id)
                return &t;
        return nullptr;
    }
};

Renderer::Renderer(const DeviceContext& ctx) : impl_(std::make_unique<Impl>())
{
    auto& p = *impl_;
    p.ctx = ctx;
    if (!p.ctx.device || !p.ctx.physical_device || !p.ctx.graphics_queue)
        throw std::runtime_error("invalid Vulkan device context");

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(p.ctx.physical_device, &props);
    p.device_name = std::string(props.deviceName);

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = p.ctx.graphics_queue_family;
    if (vkCreateCommandPool(p.ctx.device, &pool_info, nullptr, &p.command_pool) != VK_SUCCESS)
        throw std::runtime_error("failed to create command pool");

    VkCommandBufferAllocateInfo cmd_info{};
    cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_info.commandPool = p.command_pool;
    cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_info.commandBufferCount = 1;
    vkAllocateCommandBuffers(p.ctx.device, &cmd_info, &p.command_buffer);

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(p.ctx.device, &fence_info, nullptr, &p.fence) != VK_SUCCESS)
        throw std::runtime_error("failed to create fence");

    p.render_pass = create_render_pass(p.ctx.device, p.ctx.color_format);
    p.set_layout = create_descriptor_set_layout(p.ctx.device);

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 256;
    VkDescriptorPoolCreateInfo pool_create{};
    pool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create.maxSets = 256;
    pool_create.poolSizeCount = 1;
    pool_create.pPoolSizes = &pool_size;
    if (vkCreateDescriptorPool(p.ctx.device, &pool_create, nullptr, &p.descriptor_pool) !=
        VK_SUCCESS)
        throw std::runtime_error("failed to create descriptor pool");

    p.sampler = create_sampler(p.ctx.device);
    p.model_vert = create_shader_module(p.ctx.device, model_vert_spv, model_vert_size);
    p.model_frag = create_shader_module(p.ctx.device, model_frag_spv, model_frag_size);
    p.blit_vert = create_shader_module(p.ctx.device, blit_vert_spv, blit_vert_size);
    p.blit_frag = create_shader_module(p.ctx.device, blit_frag_spv, blit_frag_size);

    p.pipeline_layout = create_pipeline_layout(p.ctx.device, p.set_layout);
    p.blit_pipeline_layout = create_blit_pipeline_layout(p.ctx.device, p.set_layout);

    p.pipelines[kPremultipliedOver] = create_pipeline(
        p.ctx.device, p.render_pass, p.pipeline_layout, p.model_vert, p.model_frag,
        VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, false);
    p.pipelines[kStraightOver] = create_pipeline(
        p.ctx.device, p.render_pass, p.pipeline_layout, p.model_vert, p.model_frag,
        VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, false);
    p.pipelines[kProtectionPass] = create_pipeline(
        p.ctx.device, p.render_pass, p.pipeline_layout, p.model_vert, p.model_frag,
        VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ZERO,
        VK_BLEND_FACTOR_ONE, false);
    p.pipelines[kMultiply] = create_pipeline(
        p.ctx.device, p.render_pass, p.pipeline_layout, p.model_vert, p.model_frag,
        VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ZERO,
        VK_BLEND_FACTOR_ONE, false);
    p.pipelines[kMaskWrite] = create_pipeline(
        p.ctx.device, p.render_pass, p.pipeline_layout, p.model_vert, p.model_frag,
        VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, VK_BLEND_FACTOR_ZERO,
        VK_BLEND_FACTOR_ONE, false);
    p.pipelines[kBlit] = create_pipeline(
        p.ctx.device, p.render_pass, p.blit_pipeline_layout, p.blit_vert, p.blit_frag,
        VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, true);
}

Renderer::~Renderer() = default;

void Renderer::reserve(Capacity capacity)
{
    auto& p = *impl_;
    p.mask_bounds.reserve(capacity.masks);
    p.mask_commands.reserve(capacity.mask_commands);
    p.masks.reserve(capacity.masks);
    p.commands.reserve(capacity.commands);
    p.textures.reserve(capacity.textures);
    const auto vbo_size = capacity.vertices * sizeof(RenderVertex);
    const auto ebo_size = capacity.indices * sizeof(std::uint32_t);
    if (p.vbo.handle)
        destroy_buffer(p.ctx.device, p.vbo);
    if (p.ebo.handle)
        destroy_buffer(p.ctx.device, p.ebo);
    if (vbo_size > 0)
        p.vbo = create_buffer(p.ctx.device, p.ctx.physical_device, vbo_size,
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ebo_size > 0)
        p.ebo = create_buffer(p.ctx.device, p.ctx.physical_device, ebo_size,
                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    p.vertex_capacity = capacity.vertices;
    p.index_capacity = capacity.indices;
    p.command_capacity = capacity.commands;
    p.texture_capacity = capacity.textures;
    p.mask_command_capacity = capacity.mask_commands;
    p.mask_capacity = capacity.masks;
}

void Renderer::upload_texture(TextureId id, const ImageRgba8& image)
{
    auto& p = *impl_;
    if (!id.valid() || p.find_texture(id) || p.textures.size() >= p.texture_capacity)
        throw std::runtime_error("invalid, duplicate or excessive texture ID");
    if (image.width <= 0 || image.height <= 0 ||
        image.pixels.size() !=
            static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4)
        throw std::runtime_error("invalid texture dimensions");
    const auto data_size = image.pixels.size();
    auto staging = create_buffer(
        p.ctx.device, p.ctx.physical_device, data_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped = nullptr;
    vkMapMemory(p.ctx.device, staging.memory, 0, data_size, 0, &mapped);
    std::memcpy(mapped, image.pixels.data(), data_size);
    vkUnmapMemory(p.ctx.device, staging.memory);
    auto img = create_image(p.ctx.device, p.ctx.physical_device, image.width, image.height,
                            VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    transition_image_layout(p.ctx.device, p.ctx.graphics_queue, p.command_pool, img.handle,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(p.ctx.device, p.ctx.graphics_queue, p.command_pool, staging.handle,
                         img.handle, image.width, image.height);
    transition_image_layout(p.ctx.device, p.ctx.graphics_queue, p.command_pool, img.handle,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    destroy_buffer(p.ctx.device, staging);
    auto set = p.allocate_descriptor_set();
    p.write_descriptor_set(set, img.view, VK_NULL_HANDLE);
    p.textures.push_back({id, img, set});
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

    if (!frame.vertices.empty() && p.vbo.handle)
    {
        auto staging = create_buffer(
            p.ctx.device, p.ctx.physical_device,
            frame.vertices.size() * sizeof(RenderVertex),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* mapped = nullptr;
        vkMapMemory(p.ctx.device, staging.memory, 0, staging.size, 0, &mapped);
        std::memcpy(mapped, frame.vertices.data(), frame.vertices.size() * sizeof(RenderVertex));
        vkUnmapMemory(p.ctx.device, staging.memory);
        copy_buffer(p.ctx.device, p.ctx.graphics_queue, p.command_pool, staging.handle, p.vbo.handle,
                    frame.vertices.size() * sizeof(RenderVertex));
        destroy_buffer(p.ctx.device, staging);
    }
    if (!frame.indices.empty() && p.ebo.handle)
    {
        auto staging = create_buffer(
            p.ctx.device, p.ctx.physical_device,
            frame.indices.size() * sizeof(std::uint32_t),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* mapped = nullptr;
        vkMapMemory(p.ctx.device, staging.memory, 0, staging.size, 0, &mapped);
        std::memcpy(mapped, frame.indices.data(), frame.indices.size() * sizeof(std::uint32_t));
        vkUnmapMemory(p.ctx.device, staging.memory);
        copy_buffer(p.ctx.device, p.ctx.graphics_queue, p.command_pool, staging.handle, p.ebo.handle,
                    frame.indices.size() * sizeof(std::uint32_t));
        destroy_buffer(p.ctx.device, staging);
    }
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
    if (p.output_width == width && p.output_height == height && p.color_target.handle)
        return;
    const auto dev = p.ctx.device;
    if (p.color_framebuffer)
        vkDestroyFramebuffer(dev, p.color_framebuffer, nullptr);
    if (p.mask_framebuffer)
        vkDestroyFramebuffer(dev, p.mask_framebuffer, nullptr);
    destroy_image(dev, p.color_target);
    destroy_image(dev, p.mask_target);
    p.color_target = create_image(dev, p.ctx.physical_device, width, height, p.ctx.color_format,
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                       VK_IMAGE_USAGE_SAMPLED_BIT);
    transition_image_layout(dev, p.ctx.graphics_queue, p.command_pool, p.color_target.handle,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    p.mask_target = create_image(dev, p.ctx.physical_device, floor_power_two(width),
                                  floor_power_two(height), p.ctx.color_format,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                      VK_IMAGE_USAGE_SAMPLED_BIT);
    transition_image_layout(dev, p.ctx.graphics_queue, p.command_pool, p.mask_target.handle,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    std::array<VkImageView, 1> fb_attachments{p.color_target.view};
    VkFramebufferCreateInfo fb_info{};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = p.render_pass;
    fb_info.attachmentCount = 1;
    fb_info.pAttachments = fb_attachments.data();
    fb_info.width = static_cast<std::uint32_t>(width);
    fb_info.height = static_cast<std::uint32_t>(height);
    fb_info.layers = 1;
    if (vkCreateFramebuffer(dev, &fb_info, nullptr, &p.color_framebuffer) != VK_SUCCESS)
        throw std::runtime_error("failed to create color framebuffer");
    fb_attachments[0] = p.mask_target.view;
    fb_info.width = static_cast<std::uint32_t>(p.mask_target.width);
    fb_info.height = static_cast<std::uint32_t>(p.mask_target.height);
    if (vkCreateFramebuffer(dev, &fb_info, nullptr, &p.mask_framebuffer) != VK_SUCCESS)
        throw std::runtime_error("failed to create mask framebuffer");
    p.mask_descriptor_set = p.allocate_descriptor_set();
    p.write_descriptor_set(p.mask_descriptor_set, p.mask_target.view, VK_NULL_HANDLE);
    p.output_width = width;
    p.output_height = height;
}

void Renderer::render(Target target, const View& view, TargetContents contents)
{
    if (contents != TargetContents::Clear && contents != TargetContents::Preserve)
        throw std::runtime_error("invalid target contents policy");
    if (target.width <= 0 || target.height <= 0)
        return;
    if (!std::isfinite(view.origin_x) || !std::isfinite(view.origin_y) ||
        !std::isfinite(view.scale) || view.scale <= 0 ||
        std::any_of(view.clear.begin(), view.clear.end(),
                    [](float v) { return !std::isfinite(v); }))
        throw std::runtime_error("invalid output view");
    auto& p = *impl_;
    const auto dev = p.ctx.device;
    const bool straight = p.composition == ColorComposition::ProtectedStraight;
    if (straight && (!p.color_target.handle || p.output_width != target.width ||
                     p.output_height != target.height))
        throw std::runtime_error("protected composition requires explicit output resize");

    vkWaitForFences(dev, 1, &p.fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    vkResetFences(dev, 1, &p.fence);
    vkResetCommandBuffer(p.command_buffer, 0);
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(p.command_buffer, &begin_info);

    const VkFramebuffer fb = straight ? p.color_framebuffer : target.framebuffer;
    const int fb_w = target.width;
    const int fb_h = target.height;
    VkRenderPassBeginInfo rp_begin{};
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass = p.render_pass;
    rp_begin.framebuffer = fb;
    rp_begin.renderArea.offset.x = 0;
    rp_begin.renderArea.offset.y = 0;
    rp_begin.renderArea.extent.width = static_cast<std::uint32_t>(fb_w);
    rp_begin.renderArea.extent.height = static_cast<std::uint32_t>(fb_h);
    std::array<VkClearValue, 1> clear_values{};
    if (straight)
    {
        clear_values[0].color.float32[0] = 0;
        clear_values[0].color.float32[1] = 0;
        clear_values[0].color.float32[2] = 0;
        clear_values[0].color.float32[3] = 0;
    }
    else
    {
        for (int i = 0; i < 4; ++i)
            clear_values[0].color.float32[i] = view.clear[i];
    }
    rp_begin.pClearValues = clear_values.data();
    rp_begin.clearValueCount = 1;
    if (straight || contents == TargetContents::Clear)
        rp_begin.clearValueCount = 1;
    else
        rp_begin.clearValueCount = 0;
    vkCmdBeginRenderPass(p.command_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(fb_w);
    viewport.height = static_cast<float>(fb_h);
    viewport.minDepth = 0;
    viewport.maxDepth = 1;
    vkCmdSetViewport(p.command_buffer, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent.width = static_cast<std::uint32_t>(fb_w);
    scissor.extent.height = static_cast<std::uint32_t>(fb_h);
    vkCmdSetScissor(p.command_buffer, 0, 1, &scissor);

    vkCmdBindPipeline(p.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      straight ? p.pipelines[kStraightOver] : p.pipelines[kPremultipliedOver]);
    VkBuffer vbo_buffers[] = {p.vbo.handle};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(p.command_buffer, 0, 1, vbo_buffers, offsets);
    vkCmdBindIndexBuffer(p.command_buffer, p.ebo.handle, 0, VK_INDEX_TYPE_UINT32);

    PushConstants pc{};
    pc.output_size[0] = static_cast<float>(target.width);
    pc.output_size[1] = static_cast<float>(target.height);
    pc.origin_x = view.origin_x;
    pc.origin_y = view.origin_y;
    pc.scale = view.scale;
    pc.flip_x = view.flip_x ? 1 : 0;
    pc.projection[0] = 2.0f / static_cast<float>(target.width);
    pc.projection[1] = -2.0f / static_cast<float>(target.height);
    pc.mask_output_size[0] = static_cast<float>(target.width);
    pc.mask_output_size[1] = static_cast<float>(target.height);
    pc.global_alpha = straight ? 1.0f : view.alpha;
    pc.premultiply = straight ? 0 : 1;
    vkCmdPushConstants(p.command_buffer, p.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(PushConstants), &pc);

    for (const auto& command : p.commands)
    {
        if (command.mask.valid())
        {
            vkCmdEndRenderPass(p.command_buffer);
            rp_begin.framebuffer = p.mask_framebuffer;
            rp_begin.renderArea.extent.width = static_cast<std::uint32_t>(p.mask_target.width);
            rp_begin.renderArea.extent.height = static_cast<std::uint32_t>(p.mask_target.height);
            rp_begin.clearValueCount = 1;
            clear_values[0].color.float32[0] = 0;
            clear_values[0].color.float32[1] = 0;
            clear_values[0].color.float32[2] = 0;
            clear_values[0].color.float32[3] = 0;
            vkCmdBeginRenderPass(p.command_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
            viewport.width = static_cast<float>(p.mask_target.width);
            viewport.height = static_cast<float>(p.mask_target.height);
            vkCmdSetViewport(p.command_buffer, 0, 1, &viewport);
            scissor.extent.width = static_cast<std::uint32_t>(p.mask_target.width);
            scissor.extent.height = static_cast<std::uint32_t>(p.mask_target.height);
            vkCmdSetScissor(p.command_buffer, 0, 1, &scissor);
            vkCmdBindPipeline(p.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              p.pipelines[kMaskWrite]);
            pc.mask_write = 1;
            pc.use_mask = 0;
            vkCmdPushConstants(p.command_buffer, p.pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(PushConstants), &pc);
            const auto& def = p.masks[command.mask.value - 1];
            for (std::uint32_t i = 0; i < def.command_count; ++i)
            {
                const auto& draw = p.mask_commands[def.first_command + i];
                const auto* tex = p.find_texture(draw.texture);
                if (!tex)
                    continue;
                vkCmdBindDescriptorSets(p.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        p.pipeline_layout, 0, 1, &tex->descriptor_set, 0, nullptr);
                vkCmdDrawIndexed(p.command_buffer, draw.index_count, 1, draw.first_index, 0, 0);
            }
            vkCmdEndRenderPass(p.command_buffer);
            rp_begin.framebuffer = p.color_framebuffer;
            rp_begin.renderArea.extent.width = static_cast<std::uint32_t>(target.width);
            rp_begin.renderArea.extent.height = static_cast<std::uint32_t>(target.height);
            rp_begin.clearValueCount = 0;
            vkCmdBeginRenderPass(p.command_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
            viewport.width = static_cast<float>(target.width);
            viewport.height = static_cast<float>(target.height);
            vkCmdSetViewport(p.command_buffer, 0, 1, &viewport);
            const auto& mb = p.mask_bounds[command.mask.value - 1];
            const auto output_clip = intersect(
                intersect(clip(mb.source, target, view), clip(mb.content, target, view)),
                {0, 0, target.width, target.height});
            scissor.offset.x = static_cast<std::int32_t>(output_clip.x);
            scissor.offset.y = static_cast<std::int32_t>(output_clip.y);
            scissor.extent.width = static_cast<std::uint32_t>(output_clip.width);
            scissor.extent.height = static_cast<std::uint32_t>(output_clip.height);
            vkCmdSetScissor(p.command_buffer, 0, 1, &scissor);
            vkCmdBindPipeline(p.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              p.pipelines[kStraightOver]);
            pc.mask_write = 0;
            pc.use_mask = 1;
            pc.mask_output_size[0] = static_cast<float>(p.mask_target.width);
            pc.mask_output_size[1] = static_cast<float>(p.mask_target.height);
            vkCmdPushConstants(p.command_buffer, p.pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(PushConstants), &pc);
        }
        else
        {
            scissor.offset.x = 0;
            scissor.offset.y = 0;
            scissor.extent.width = static_cast<std::uint32_t>(target.width);
            scissor.extent.height = static_cast<std::uint32_t>(target.height);
            vkCmdSetScissor(p.command_buffer, 0, 1, &scissor);
            pc.use_mask = 0;
        }

        const auto* tex = p.find_texture(command.texture);
        if (!tex)
            continue;
        VkDescriptorSet sets[] = {tex->descriptor_set, p.mask_descriptor_set};
        vkCmdBindDescriptorSets(p.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline_layout,
                                0, 1, &tex->descriptor_set, 0, nullptr);
        if (command.mask.valid())
        {
            vkCmdBindDescriptorSets(p.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    p.pipeline_layout, 0, 1, sets, 0, nullptr);
        }

        if (straight && command.protection == ColorProtection::PreserveTranslucent)
        {
            vkCmdBindPipeline(p.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              p.pipelines[kProtectionPass]);
            pc.protection_pass = 1;
            vkCmdPushConstants(p.command_buffer, p.pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(PushConstants), &pc);
            vkCmdDrawIndexed(p.command_buffer, command.index_count, 1, command.first_index, 0, 0);
            pc.protection_pass = 0;
        }

        PipelineIndex pipe = straight ? kStraightOver : kPremultipliedOver;
        if (command.blend == BlendMode::Multiply)
            pipe = kMultiply;
        vkCmdBindPipeline(p.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipelines[pipe]);
        pc.premultiply = (command.blend == BlendMode::Multiply || !straight) ? 1 : 0;
        vkCmdPushConstants(p.command_buffer, p.pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(PushConstants), &pc);
        vkCmdDrawIndexed(p.command_buffer, command.index_count, 1, command.first_index, 0, 0);
    }

    if (straight)
    {
        vkCmdEndRenderPass(p.command_buffer);
        rp_begin.framebuffer = target.framebuffer;
        rp_begin.renderArea.extent.width = static_cast<std::uint32_t>(target.width);
        rp_begin.renderArea.extent.height = static_cast<std::uint32_t>(target.height);
        rp_begin.clearValueCount = (contents == TargetContents::Clear) ? 1 : 0;
        if (contents == TargetContents::Clear)
            for (int i = 0; i < 4; ++i)
                clear_values[0].color.float32[i] = view.clear[i];
        vkCmdBeginRenderPass(p.command_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
        viewport.width = static_cast<float>(target.width);
        viewport.height = static_cast<float>(target.height);
        vkCmdSetViewport(p.command_buffer, 0, 1, &viewport);
        scissor.offset.x = 0;
        scissor.offset.y = 0;
        scissor.extent.width = static_cast<std::uint32_t>(target.width);
        scissor.extent.height = static_cast<std::uint32_t>(target.height);
        vkCmdSetScissor(p.command_buffer, 0, 1, &scissor);
        vkCmdBindPipeline(p.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipelines[kBlit]);
        VkDescriptorSet blit_set = p.allocate_descriptor_set();
        p.write_descriptor_set(blit_set, p.color_target.view, VK_NULL_HANDLE);
        vkCmdBindDescriptorSets(p.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                p.blit_pipeline_layout, 0, 1, &blit_set, 0, nullptr);
        float blit_alpha = view.alpha;
        vkCmdPushConstants(p.command_buffer, p.blit_pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(float), &blit_alpha);
        vkCmdDraw(p.command_buffer, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(p.command_buffer);
    vkEndCommandBuffer(p.command_buffer);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &p.command_buffer;
    vkQueueSubmit(p.ctx.graphics_queue, 1, &submit, p.fence);
}

ImageRgba8 Renderer::capture(Target target)
{
    auto& p = *impl_;
    if (target.width <= 0 || target.height <= 0)
        throw std::runtime_error("cannot capture empty target");
    vkWaitForFences(p.ctx.device, 1, &p.fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    ImageRgba8 image;
    image.width = target.width;
    image.height = target.height;
    image.pixels.resize(static_cast<std::size_t>(target.width) *
                        static_cast<std::size_t>(target.height) * 4);
    const auto data_size = image.pixels.size();
    auto staging = create_buffer(
        p.ctx.device, p.ctx.physical_device, data_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    auto img = create_image(p.ctx.device, p.ctx.physical_device, target.width, target.height,
                            p.ctx.color_format,
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    (void)img;
    (void)staging;
    destroy_buffer(p.ctx.device, staging);
    destroy_image(p.ctx.device, img);
    return image;
}

std::string Renderer::diagnostics() const
{
    return "Vulkan | " + impl_->device_name;
}

} // namespace nekomote::render_vk
