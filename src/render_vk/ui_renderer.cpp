#include <nekomote/render_vk/ui_renderer.hpp>
#include <nekomote/render_vk/renderer.hpp>

#include "vk_util.hpp"
#include "spirv_shaders.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace nekomote::render_vk
{
namespace
{

struct RectPC
{
    float params0[4]{};
    float params1[4]{};
    float params2[4]{};
    float params3[4]{};
};

struct SimplePC
{
    float params0[4]{};
    float params1[4]{};
};

VkRenderPass create_ui_render_pass(VkDevice device, VkFormat format)
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
        throw std::runtime_error("failed to create UI render pass");
    return pass;
}

VkDescriptorSetLayout create_ui_set_layout(VkDevice device)
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings = &binding;
    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("failed to create UI descriptor set layout");
    return layout;
}

VkPipelineLayout create_ui_pipeline_layout(VkDevice device, VkDescriptorSetLayout set_layout,
                                            std::uint32_t pc_size)
{
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc_range.offset = 0;
    pc_range.size = pc_size;
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 1;
    info.pSetLayouts = &set_layout;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &pc_range;
    VkPipelineLayout layout;
    if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("failed to create UI pipeline layout");
    return layout;
}

VkPipeline create_ui_pipeline(VkDevice device, VkRenderPass render_pass, VkPipelineLayout layout,
                              VkShaderModule vert, VkShaderModule frag, bool has_uv,
                              VkPrimitiveTopology topology, VkBlendFactor src_factor,
                              VkBlendFactor dst_factor)
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
    binding.stride = has_uv ? 16u : 8u;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::array<VkVertexInputAttributeDescription, 2> attrs{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset = 0;
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset = 8;

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = has_uv ? 2u : 1u;
    vertex_input.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = topology;

    VkViewport viewport{};
    viewport.width = 1;
    viewport.height = 1;
    viewport.maxDepth = 1;
    VkRect2D scissor{};
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
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;

    std::array<VkDynamicState, 2> dyn{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dyn.size());
    dynamic.pDynamicStates = dyn.data();

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
        throw std::runtime_error("failed to create UI pipeline");
    return pipeline;
}

} // namespace

FontAtlas::~FontAtlas()
{
    reset();
}

void FontAtlas::reset()
{
    if (view_)
        vkDestroyImageView(device_, view_, nullptr);
    if (image_)
        vkDestroyImage(device_, image_, nullptr);
    if (memory_)
        vkFreeMemory(device_, memory_, nullptr);
    image_ = VK_NULL_HANDLE;
    view_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
}

bool FontAtlas::init(VkDevice device, VkPhysicalDevice physical_device, VkQueue queue,
                     std::uint32_t queue_family, VkCommandPool pool, int texture_width,
                     int texture_height)
{
    reset();
    device_ = device;
    queue_ = queue;
    queue_family_ = queue_family;
    pool_ = pool;
    texture_width_ = texture_width;
    texture_height_ = texture_height;
    auto img = create_image(device, physical_device, texture_width, texture_height,
                            VK_FORMAT_R8_UNORM,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    image_ = img.handle;
    memory_ = img.memory;
    view_ = img.view;
    transition_image_layout(device, queue, pool, image_, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}

void FontAtlas::resize_atlas(int new_width, int new_height)
{
    texture_width_ = new_width;
    texture_height_ = new_height;
    reset();
    auto img = create_image(device_, VK_NULL_HANDLE, texture_width_, texture_height_,
                            VK_FORMAT_R8_UNORM,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    (void)img;
}

void FontAtlas::upload_glyph(const nekomote::ui::Glyph& glyph, const unsigned char* bitmap)
{
    (void)glyph;
    (void)bitmap;
}

struct UiRenderer::Impl
{
    DeviceContext ctx;
    VkCommandPool command_pool{VK_NULL_HANDLE};
    VkCommandBuffer command_buffer{VK_NULL_HANDLE};
    VkFence fence{VK_NULL_HANDLE};
    VkRenderPass render_pass{VK_NULL_HANDLE};
    VkDescriptorSetLayout set_layout{VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
    VkSampler sampler{VK_NULL_HANDLE};

    VkPipelineLayout rect_layout{VK_NULL_HANDLE};
    VkPipelineLayout simple_layout{VK_NULL_HANDLE};
    VkPipeline rect_pipeline{VK_NULL_HANDLE};
    VkPipeline text_pipeline{VK_NULL_HANDLE};
    VkPipeline icon_pipeline{VK_NULL_HANDLE};
    VkPipeline tri_pipeline{VK_NULL_HANDLE};
    VkShaderModule ui_vert{VK_NULL_HANDLE};
    VkShaderModule rect_frag{VK_NULL_HANDLE};
    VkShaderModule text_frag{VK_NULL_HANDLE};
    VkShaderModule icon_frag{VK_NULL_HANDLE};
    VkShaderModule tri_frag{VK_NULL_HANDLE};

    Buffer vbo;
    Buffer uvbo;

    FontAtlas font_atlas;
    nekomote::ui::FontMetrics font_metrics;
    bool font_ready{false};
    VkDescriptorSet font_set{VK_NULL_HANDLE};

    struct Icon
    {
        Image image;
        VkDescriptorSet set;
    };
    std::vector<Icon> icons;

    bool initialized{false};
    int viewport_w{0}, viewport_h{0};
    float global_alpha{1.0f};
    VkFramebuffer framebuffer{VK_NULL_HANDLE};

    std::vector<float> rect_verts, rect_uvs;
    std::vector<float> text_verts, text_uvs;
    std::vector<float> icon_verts, icon_uvs;
    std::vector<std::uint32_t> icon_ids;
    std::vector<float> tri_verts;

    ~Impl()
    {
        const auto dev = ctx.device;
        for (auto& icon : icons)
            destroy_image(dev, icon.image);
        if (vbo.handle)
            destroy_buffer(dev, vbo);
        if (uvbo.handle)
            destroy_buffer(dev, uvbo);
        if (rect_pipeline)
            vkDestroyPipeline(dev, rect_pipeline, nullptr);
        if (text_pipeline)
            vkDestroyPipeline(dev, text_pipeline, nullptr);
        if (icon_pipeline)
            vkDestroyPipeline(dev, icon_pipeline, nullptr);
        if (tri_pipeline)
            vkDestroyPipeline(dev, tri_pipeline, nullptr);
        if (rect_layout)
            vkDestroyPipelineLayout(dev, rect_layout, nullptr);
        if (simple_layout)
            vkDestroyPipelineLayout(dev, simple_layout, nullptr);
        if (sampler)
            vkDestroySampler(dev, sampler, nullptr);
        if (descriptor_pool)
            vkDestroyDescriptorPool(dev, descriptor_pool, nullptr);
        if (set_layout)
            vkDestroyDescriptorSetLayout(dev, set_layout, nullptr);
        if (render_pass)
            vkDestroyRenderPass(dev, render_pass, nullptr);
        if (ui_vert)
            vkDestroyShaderModule(dev, ui_vert, nullptr);
        if (rect_frag)
            vkDestroyShaderModule(dev, rect_frag, nullptr);
        if (text_frag)
            vkDestroyShaderModule(dev, text_frag, nullptr);
        if (icon_frag)
            vkDestroyShaderModule(dev, icon_frag, nullptr);
        if (tri_frag)
            vkDestroyShaderModule(dev, tri_frag, nullptr);
        if (fence)
            vkDestroyFence(dev, fence, nullptr);
        if (command_pool)
            vkDestroyCommandPool(dev, command_pool, nullptr);
    }

    VkDescriptorSet alloc_set()
    {
        VkDescriptorSetAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        info.descriptorPool = descriptor_pool;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &set_layout;
        VkDescriptorSet set;
        if (vkAllocateDescriptorSets(ctx.device, &info, &set) != VK_SUCCESS)
            throw std::runtime_error("failed to allocate UI descriptor set");
        return set;
    }

    void write_set(VkDescriptorSet set, VkImageView view)
    {
        VkDescriptorImageInfo image_info{};
        image_info.sampler = sampler;
        image_info.imageView = view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &image_info;
        vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);
    }
};

UiRenderer::UiRenderer() = default;

UiRenderer::~UiRenderer()
{
    reset();
}

void UiRenderer::reset()
{
    if (impl_)
        impl_.reset();
    impl_ = std::make_unique<Impl>();
}

bool UiRenderer::init(const DeviceContext& ctx, int viewport_width, int viewport_height)
{
    reset();
    auto& p = *impl_;
    p.ctx = ctx;
    p.viewport_w = viewport_width;
    p.viewport_h = viewport_height;

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = p.ctx.graphics_queue_family;
    if (vkCreateCommandPool(p.ctx.device, &pool_info, nullptr, &p.command_pool) != VK_SUCCESS)
        return false;

    VkCommandBufferAllocateInfo cmd_info{};
    cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_info.commandPool = p.command_pool;
    cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_info.commandBufferCount = 1;
    vkAllocateCommandBuffers(p.ctx.device, &cmd_info, &p.command_buffer);

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(p.ctx.device, &fence_info, nullptr, &p.fence);

    p.render_pass = create_ui_render_pass(p.ctx.device, p.ctx.color_format);
    p.set_layout = create_ui_set_layout(p.ctx.device);

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 256;
    VkDescriptorPoolCreateInfo pool_create{};
    pool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create.maxSets = 256;
    pool_create.poolSizeCount = 1;
    pool_create.pPoolSizes = &pool_size;
    vkCreateDescriptorPool(p.ctx.device, &pool_create, nullptr, &p.descriptor_pool);

    p.sampler = create_sampler(p.ctx.device);
    p.ui_vert = create_shader_module(p.ctx.device, ui_vert_spv, ui_vert_size);
    p.rect_frag = create_shader_module(p.ctx.device, rect_frag_spv, rect_frag_size);
    p.text_frag = create_shader_module(p.ctx.device, text_frag_spv, text_frag_size);
    p.icon_frag = create_shader_module(p.ctx.device, icon_frag_spv, icon_frag_size);
    p.tri_frag = create_shader_module(p.ctx.device, tri_frag_spv, tri_frag_size);

    p.rect_layout = create_ui_pipeline_layout(p.ctx.device, p.set_layout, sizeof(RectPC));
    p.simple_layout = create_ui_pipeline_layout(p.ctx.device, p.set_layout, sizeof(SimplePC));

    p.rect_pipeline = create_ui_pipeline(
        p.ctx.device, p.render_pass, p.rect_layout, p.ui_vert, p.rect_frag, true,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    p.text_pipeline = create_ui_pipeline(
        p.ctx.device, p.render_pass, p.simple_layout, p.ui_vert, p.text_frag, true,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    p.icon_pipeline = create_ui_pipeline(
        p.ctx.device, p.render_pass, p.simple_layout, p.ui_vert, p.icon_frag, true,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    p.tri_pipeline = create_ui_pipeline(
        p.ctx.device, p.render_pass, p.simple_layout, p.ui_vert, p.tri_frag, false,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);

    const auto vb_size = static_cast<VkDeviceSize>(65536 * sizeof(float));
    p.vbo = create_buffer(p.ctx.device, p.ctx.physical_device, vb_size,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    p.uvbo = create_buffer(p.ctx.device, p.ctx.physical_device, vb_size,
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    p.initialized = true;
    return true;
}

void UiRenderer::resize(int viewport_width, int viewport_height)
{
    if (impl_)
    {
        impl_->viewport_w = viewport_width;
        impl_->viewport_h = viewport_height;
    }
}

void UiRenderer::set_render_target(VkFramebuffer framebuffer)
{
    if (impl_)
        impl_->framebuffer = framebuffer;
}

bool UiRenderer::init_font(const std::string& font_path, float pixel_height)
{
    if (!impl_ || !impl_->initialized)
        return false;
    auto& p = *impl_;
    if (!p.font_metrics.init(font_path, pixel_height))
        return false;
    if (!p.font_atlas.init(p.ctx.device, p.ctx.physical_device, p.ctx.graphics_queue,
                           p.ctx.graphics_queue_family, p.command_pool,
                           p.font_metrics.texture_width(), p.font_metrics.texture_height()))
        return false;
    p.font_metrics.on_glyph_added = [this](const nekomote::ui::Glyph& g, const unsigned char* bitmap)
    { impl_->font_atlas.upload_glyph(g, bitmap); };
    p.font_metrics.on_atlas_resized = [this](int w, int h)
    { impl_->font_atlas.resize_atlas(w, h); };
    p.font_set = p.alloc_set();
    p.write_set(p.font_set, p.font_atlas.view());
    p.font_ready = true;
    return true;
}

std::uint32_t UiRenderer::upload_icon(const ImageRgba8& image)
{
    if (!impl_ || !impl_->initialized)
        return 0;
    auto& p = *impl_;
    auto img = create_image(p.ctx.device, p.ctx.physical_device, image.width, image.height,
                            VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    auto staging = create_buffer(
        p.ctx.device, p.ctx.physical_device, image.pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped = nullptr;
    vkMapMemory(p.ctx.device, staging.memory, 0, image.pixels.size(), 0, &mapped);
    std::memcpy(mapped, image.pixels.data(), image.pixels.size());
    vkUnmapMemory(p.ctx.device, staging.memory);
    transition_image_layout(p.ctx.device, p.ctx.graphics_queue, p.command_pool, img.handle,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(p.ctx.device, p.ctx.graphics_queue, p.command_pool, staging.handle,
                         img.handle, image.width, image.height);
    transition_image_layout(p.ctx.device, p.ctx.graphics_queue, p.command_pool, img.handle,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    destroy_buffer(p.ctx.device, staging);
    auto set = p.alloc_set();
    p.write_set(set, img.view);
    p.icons.push_back({img, set});
    return static_cast<std::uint32_t>(p.icons.size());
}

void UiRenderer::destroy_icon(std::uint32_t id)
{
    if (!impl_ || id == 0 || id > impl_->icons.size())
        return;
    auto& icon = impl_->icons[id - 1];
    destroy_image(impl_->ctx.device, icon.image);
    icon = {};
}

void UiRenderer::begin_frame()
{
    if (!impl_ || !impl_->initialized)
        return;
    impl_->global_alpha = 1.0f;
    impl_->rect_verts.clear();
    impl_->rect_uvs.clear();
    impl_->text_verts.clear();
    impl_->text_uvs.clear();
    impl_->icon_verts.clear();
    impl_->icon_uvs.clear();
    impl_->icon_ids.clear();
    impl_->tri_verts.clear();
}

void UiRenderer::end_frame()
{
    if (!impl_ || !impl_->initialized)
        return;
    auto& p = *impl_;
    if (!p.framebuffer)
        return;

    vkWaitForFences(p.ctx.device, 1, &p.fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    vkResetFences(p.ctx.device, 1, &p.fence);
    vkResetCommandBuffer(p.command_buffer, 0);
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(p.command_buffer, &begin_info);

    VkRenderPassBeginInfo rp_begin{};
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass = p.render_pass;
    rp_begin.framebuffer = p.framebuffer;
    rp_begin.renderArea.extent.width = static_cast<std::uint32_t>(p.viewport_w);
    rp_begin.renderArea.extent.height = static_cast<std::uint32_t>(p.viewport_h);
    rp_begin.clearValueCount = 0;
    vkCmdBeginRenderPass(p.command_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(p.viewport_w);
    viewport.height = static_cast<float>(p.viewport_h);
    viewport.maxDepth = 1;
    vkCmdSetViewport(p.command_buffer, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent.width = static_cast<std::uint32_t>(p.viewport_w);
    scissor.extent.height = static_cast<std::uint32_t>(p.viewport_h);
    vkCmdSetScissor(p.command_buffer, 0, 1, &scissor);

    if (!p.rect_verts.empty())
    {
        vkCmdBindPipeline(p.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, p.rect_pipeline);
        RectPC pc{};
        pc.params0[0] = static_cast<float>(p.viewport_w);
        pc.params0[1] = static_cast<float>(p.viewport_h);
        vkCmdPushConstants(p.command_buffer, p.rect_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(RectPC), &pc);
        vkCmdDraw(p.command_buffer, static_cast<std::uint32_t>(p.rect_verts.size() / 2), 1, 0, 0);
    }

    if (!p.tri_verts.empty())
    {
        vkCmdBindPipeline(p.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, p.tri_pipeline);
        SimplePC pc{};
        pc.params0[0] = static_cast<float>(p.viewport_w);
        pc.params0[1] = static_cast<float>(p.viewport_h);
        vkCmdPushConstants(p.command_buffer, p.simple_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(SimplePC), &pc);
        vkCmdDraw(p.command_buffer, static_cast<std::uint32_t>(p.tri_verts.size() / 2), 1, 0, 0);
    }

    vkCmdEndRenderPass(p.command_buffer);
    vkEndCommandBuffer(p.command_buffer);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &p.command_buffer;
    vkQueueSubmit(p.ctx.graphics_queue, 1, &submit, p.fence);

    p.global_alpha = 1.0f;
}

void UiRenderer::draw_rounded_rect(float x, float y, float w, float h, float radius,
                                   nekomote::ui::Color fill, nekomote::ui::Color border,
                                   float border_width)
{
    if (!impl_ || !impl_->initialized)
        return;
    fill.a *= impl_->global_alpha;
    border.a *= impl_->global_alpha;
    const float r = std::min(radius, std::min(w, h) * 0.5f);
    impl_->rect_verts.insert(impl_->rect_verts.end(),
                             {x, y, x + w, y, x + w, y + h, x, y, x + w, y + h, x, y + h});
    impl_->rect_uvs.insert(impl_->rect_uvs.end(), {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1});
    (void)r;
    (void)border;
    (void)border_width;
}

void UiRenderer::draw_text(float x, float y, const char* text, float size,
                           nekomote::ui::Color color)
{
    if (!impl_ || !impl_->initialized || !impl_->font_ready)
        return;
    (void)x;
    (void)y;
    (void)text;
    (void)size;
    (void)color;
}

void UiRenderer::draw_icon(float x, float y, float w, float h, std::uint32_t texture_id,
                           nekomote::ui::Color tint)
{
    if (!impl_ || !impl_->initialized)
        return;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)texture_id;
    (void)tint;
}

void UiRenderer::draw_circle(float cx, float cy, float radius, nekomote::ui::Color fill,
                             nekomote::ui::Color border, float border_width)
{
    draw_rounded_rect(cx - radius, cy - radius, radius * 2, radius * 2, radius, fill, border,
                      border_width);
}

void UiRenderer::draw_solid_rect(float x, float y, float w, float h, nekomote::ui::Color color)
{
    if (!impl_ || !impl_->initialized)
        return;
    color.a *= impl_->global_alpha;
    impl_->tri_verts.insert(impl_->tri_verts.end(),
                            {x, y, x + w, y, x + w, y + h, x, y, x + w, y + h, x, y + h});
}

void UiRenderer::draw_arrow(float cx, float cy, float size, bool points_up,
                            nekomote::ui::Color fill)
{
    if (!impl_ || !impl_->initialized)
        return;
    const float h = size * 0.5f;
    if (points_up)
        impl_->tri_verts.insert(impl_->tri_verts.end(),
                                {cx, cy - h, cx - h, cy + h, cx + h, cy + h});
    else
        impl_->tri_verts.insert(impl_->tri_verts.end(),
                                {cx, cy + h, cx - h, cy - h, cx + h, cy - h});
    (void)fill;
}

void UiRenderer::push_clip_rect(float x, float y, float w, float h)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void UiRenderer::pop_clip_rect()
{
}

void UiRenderer::set_global_alpha(float alpha)
{
    if (impl_)
        impl_->global_alpha = alpha;
}

float UiRenderer::global_alpha() const
{
    return impl_ ? impl_->global_alpha : 1.0f;
}

int UiRenderer::viewport_width() const
{
    return impl_ ? impl_->viewport_w : 0;
}

int UiRenderer::viewport_height() const
{
    return impl_ ? impl_->viewport_h : 0;
}

nekomote::ui::FontMetrics* UiRenderer::font()
{
    return impl_ ? &impl_->font_metrics : nullptr;
}

bool UiRenderer::is_initialized() const
{
    return impl_ && impl_->initialized;
}

} // namespace nekomote::render_vk
