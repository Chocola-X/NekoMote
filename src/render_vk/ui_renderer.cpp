#include <nekomote/render_vk/renderer.hpp>
#include <nekomote/render_vk/ui_renderer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#include "spirv_shaders.hpp"
#include "vk_util.hpp"

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
    binding.stride = 16u;
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
    vertex_input.vertexAttributeDescriptionCount = 2u;
    (void)has_uv;
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

struct UiRenderer::Impl
{
    struct Vertex
    {
        float x, y, u, v;
    };
    enum class Kind
    {
        Rect,
        Text,
        Icon,
        Solid
    };
    struct Draw
    {
        Kind kind;
        std::uint32_t first, count;
        RectPC constants;
        VkRect2D clip;
        VkDescriptorSet descriptor{VK_NULL_HANDLE};
    };
    struct Icon
    {
        Image image;
        VkDescriptorSet descriptor{VK_NULL_HANDLE};
    };

    DeviceContext ctx{};
    Target target{};
    VkCommandPool upload_pool{VK_NULL_HANDLE};
    VkRenderPass render_pass{VK_NULL_HANDLE};
    VkDescriptorSetLayout set_layout{VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
    VkSampler sampler{VK_NULL_HANDLE};
    VkPipelineLayout layout{VK_NULL_HANDLE};
    std::array<VkPipeline, 4> pipelines{};
    std::array<VkShaderModule, 5> shaders{};
    Buffer vbo;
    std::vector<Vertex> vertices;
    std::vector<Draw> draws;
    std::vector<VkRect2D> clips;
    std::vector<float> text_vertices, text_uvs;
    nekomote::ui::FontMetrics font_metrics;
    Image font_image;
    VkDescriptorSet font_set{VK_NULL_HANDLE};
    std::vector<unsigned char> atlas_pixels;
    int atlas_width{0}, atlas_height{0};
    bool atlas_dirty{false}, font_ready{false}, initialized{false};
    std::vector<Icon> icons;
    int width{0}, height{0};
    float alpha{1};

    ~Impl()
    {
        const auto device = ctx.device;
        if (!device)
            return;
        vbo.reset();
        font_image.reset();
        for (auto& icon : icons)
            icon.image.reset();
        for (auto pipeline : pipelines)
            if (pipeline)
                vkDestroyPipeline(device, pipeline, nullptr);
        for (auto shader : shaders)
            if (shader)
                vkDestroyShaderModule(device, shader, nullptr);
        if (layout)
            vkDestroyPipelineLayout(device, layout, nullptr);
        if (sampler)
            vkDestroySampler(device, sampler, nullptr);
        if (descriptor_pool)
            vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        if (set_layout)
            vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        if (render_pass)
            vkDestroyRenderPass(device, render_pass, nullptr);
        if (upload_pool)
            vkDestroyCommandPool(device, upload_pool, nullptr);
    }

    VkDescriptorSet allocate_set()
    {
        VkDescriptorSetAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        info.descriptorPool = descriptor_pool;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &set_layout;
        VkDescriptorSet result{};
        check_vk(vkAllocateDescriptorSets(ctx.device, &info, &result), "allocate UI descriptor");
        return result;
    }

    void write_set(VkDescriptorSet descriptor, VkImageView view)
    {
        const VkDescriptorImageInfo image_info{sampler, view,
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptor;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &image_info;
        vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);
    }

    Image upload_pixels(int image_width, int image_height, VkFormat format,
                        const unsigned char* pixels, std::size_t bytes)
    {
        auto image =
            create_image(ctx.device, ctx.physical_device, image_width, image_height, format,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        auto staging = create_mapped_buffer(ctx.device, ctx.physical_device, bytes,
                                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        std::memcpy(staging.mapped_data, pixels, bytes);
        const auto commands = begin_one_time_commands(ctx.device, upload_pool);
        record_image_barrier(commands, image.handle, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {static_cast<std::uint32_t>(image_width),
                            static_cast<std::uint32_t>(image_height), 1};
        vkCmdCopyBufferToImage(commands, staging.handle, image.handle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        record_image_barrier(commands, image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        end_one_time_commands(ctx.device, ctx.graphics_queue, upload_pool, commands);
        return image;
    }

    void resize_atlas(int new_width, int new_height)
    {
        // All prior text draws retain their locations when the atlas grows.
        // Normalize their UVs to the new texture size without reordering draws.
        for (const auto& draw : draws)
        {
            if (draw.kind != Kind::Text)
                continue;
            for (std::uint32_t i = draw.first; i < draw.first + draw.count; ++i)
            {
                vertices[i].u *= static_cast<float>(atlas_width) / static_cast<float>(new_width);
                vertices[i].v *= static_cast<float>(atlas_height) / static_cast<float>(new_height);
            }
        }
        atlas_width = new_width;
        atlas_height = new_height;
        atlas_pixels.assign(
            static_cast<std::size_t>(new_width) * static_cast<std::size_t>(new_height), 0);
        atlas_dirty = true;
    }

    void upload_glyph(const nekomote::ui::Glyph& glyph, const unsigned char* bitmap)
    {
        if (!bitmap || glyph.atlas_x < 0 || glyph.atlas_y < 0 || glyph.atlas_w < 0 ||
            glyph.atlas_h < 0 || glyph.atlas_x + glyph.atlas_w > atlas_width ||
            glyph.atlas_y + glyph.atlas_h > atlas_height)
            throw std::runtime_error("UI glyph outside atlas");
        for (int row = 0; row < glyph.atlas_h; ++row)
        {
            const auto offset = static_cast<std::size_t>(glyph.atlas_y + row) *
                                    static_cast<std::size_t>(atlas_width) +
                                static_cast<std::size_t>(glyph.atlas_x);
            std::memcpy(atlas_pixels.data() + offset,
                        bitmap +
                            static_cast<std::size_t>(row) * static_cast<std::size_t>(glyph.atlas_w),
                        static_cast<std::size_t>(glyph.atlas_w));
        }
        atlas_dirty = true;
    }

    VkRect2D current_clip() const
    {
        if (!clips.empty())
            return clips.back();
        return {{0, 0}, {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)}};
    }

    RectPC constants(nekomote::ui::Color fill) const
    {
        RectPC pc{};
        pc.params0[0] = static_cast<float>(width);
        pc.params0[1] = static_cast<float>(height);
        pc.params1[0] = fill.r;
        pc.params1[1] = fill.g;
        pc.params1[2] = fill.b;
        pc.params1[3] = fill.a * alpha;
        return pc;
    }

    void add_quad(float x, float y, float w, float h)
    {
        vertices.insert(vertices.end(), {{x, y, 0, 0},
                                         {x + w, y, 1, 0},
                                         {x + w, y + h, 1, 1},
                                         {x, y, 0, 0},
                                         {x + w, y + h, 1, 1},
                                         {x, y + h, 0, 1}});
    }

    void record(Kind kind, std::uint32_t first, RectPC pc,
                VkDescriptorSet descriptor = VK_NULL_HANDLE)
    {
        const auto count = static_cast<std::uint32_t>(vertices.size()) - first;
        if (count)
            draws.push_back({kind, first, count, pc, current_clip(), descriptor});
    }

    void ensure_vertex_buffer(std::size_t required)
    {
        if (required <= vbo.size)
            return;
        vbo = create_mapped_buffer(ctx.device, ctx.physical_device,
                                   std::max(required, std::size_t{1 << 20}),
                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
};

UiRenderer::UiRenderer() = default;
UiRenderer::~UiRenderer() = default;

void UiRenderer::reset()
{
    impl_.reset();
}

bool UiRenderer::init(const DeviceContext& ctx, int viewport_width, int viewport_height)
{
    reset();
    if (!ctx.device || !ctx.physical_device || !ctx.graphics_queue || viewport_width <= 0 ||
        viewport_height <= 0)
        return false;
    auto candidate = std::make_unique<Impl>();
    auto& p = *candidate;
    p.ctx = ctx;
    p.width = viewport_width;
    p.height = viewport_height;
    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool.queueFamilyIndex = ctx.graphics_queue_family;
    check_vk(vkCreateCommandPool(ctx.device, &pool, nullptr, &p.upload_pool),
             "create UI upload pool");
    p.render_pass = create_color_render_pass(ctx.device, ctx.color_format);
    p.set_layout = create_ui_set_layout(ctx.device);
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256};
    VkDescriptorPoolCreateInfo descriptor_pool{};
    descriptor_pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptor_pool.maxSets = 256;
    descriptor_pool.poolSizeCount = 1;
    descriptor_pool.pPoolSizes = &pool_size;
    check_vk(vkCreateDescriptorPool(ctx.device, &descriptor_pool, nullptr, &p.descriptor_pool),
             "create UI descriptor pool");
    p.sampler = create_sampler(ctx.device);
    p.layout = create_ui_pipeline_layout(ctx.device, p.set_layout, sizeof(RectPC));
    p.shaders[0] = create_shader_module(ctx.device, ui_vert_spv, ui_vert_size);
    p.shaders[1] = create_shader_module(ctx.device, rect_frag_spv, rect_frag_size);
    p.shaders[2] = create_shader_module(ctx.device, text_frag_spv, text_frag_size);
    p.shaders[3] = create_shader_module(ctx.device, icon_frag_spv, icon_frag_size);
    p.shaders[4] = create_shader_module(ctx.device, tri_frag_spv, tri_frag_size);
    for (std::size_t i = 0; i < p.pipelines.size(); ++i)
        p.pipelines[i] =
            create_ui_pipeline(ctx.device, p.render_pass, p.layout, p.shaders[0], p.shaders[i + 1],
                               true, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_BLEND_FACTOR_SRC_ALPHA,
                               VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    p.vertices.reserve(65536);
    p.draws.reserve(8192);
    p.clips.reserve(16);
    p.text_vertices.reserve(8192);
    p.text_uvs.reserve(8192);
    p.ensure_vertex_buffer(65536 * sizeof(Impl::Vertex));
    p.initialized = true;
    impl_ = std::move(candidate);
    return true;
}

void UiRenderer::resize(int viewport_width, int viewport_height)
{
    if (impl_ && viewport_width > 0 && viewport_height > 0)
    {
        impl_->width = viewport_width;
        impl_->height = viewport_height;
    }
}

void UiRenderer::set_render_target(Target target)
{
    if (!impl_)
        return;
    if (!target.commands || !target.framebuffer || target.width <= 0 || target.height <= 0)
        throw std::runtime_error("invalid Vulkan UI target");
    impl_->target = target;
    resize(target.width, target.height);
}

bool UiRenderer::init_font(const std::string& font_path, float pixel_height)
{
    if (!impl_ || !impl_->initialized || !std::isfinite(pixel_height) || pixel_height <= 0)
        return false;
    auto& p = *impl_;
    p.font_ready = false;
    if (!p.font_metrics.init(font_path, pixel_height))
        return false;
    p.resize_atlas(p.font_metrics.texture_width(), p.font_metrics.texture_height());
    p.font_metrics.on_atlas_resized = [&p](int w, int h) { p.resize_atlas(w, h); };
    p.font_metrics.on_glyph_added =
        [&p](const nekomote::ui::Glyph& glyph, const unsigned char* bitmap)
    { p.upload_glyph(glyph, bitmap); };
    if (!p.font_set)
        p.font_set = p.allocate_set();
    p.font_ready = true;
    return true;
}

std::uint32_t UiRenderer::upload_icon(ImageViewRgba8 image)
{
    if (!impl_ || !impl_->initialized || image.width <= 0 || image.height <= 0)
        return 0;
    const auto expected =
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4;
    if (image.pixels.size() != expected)
        throw std::runtime_error("invalid UI icon pixels");
    auto& p = *impl_;
    Impl::Icon icon{};
    icon.image = p.upload_pixels(image.width, image.height, VK_FORMAT_R8G8B8A8_UNORM,
                                 image.pixels.data(), expected);
    try
    {
        icon.descriptor = p.allocate_set();
        p.write_set(icon.descriptor, icon.image.view);
        p.icons.push_back(std::move(icon));
    }
    catch (...)
    {
        if (icon.descriptor)
            vkFreeDescriptorSets(p.ctx.device, p.descriptor_pool, 1, &icon.descriptor);
        throw;
    }
    return static_cast<std::uint32_t>(p.icons.size());
}

void UiRenderer::destroy_icon(std::uint32_t id)
{
    if (!impl_ || !id || id > impl_->icons.size())
        return;
    auto& p = *impl_;
    auto& icon = p.icons[id - 1];
    if (icon.descriptor)
        check_vk(vkFreeDescriptorSets(p.ctx.device, p.descriptor_pool, 1, &icon.descriptor),
                 "free UI icon descriptor");
    icon.descriptor = VK_NULL_HANDLE;
    icon.image.reset();
}

void UiRenderer::begin_frame()
{
    if (!impl_)
        return;
    impl_->alpha = 1;
    impl_->vertices.clear();
    impl_->draws.clear();
    impl_->clips.clear();
}

void UiRenderer::end_frame()
{
    if (!impl_ || !impl_->initialized)
        return;
    auto& p = *impl_;
    if (p.draws.empty())
    {
        p.alpha = 1;
        return;
    }
    if (!p.target.commands || !p.target.framebuffer)
        throw std::runtime_error("Vulkan UI target not set");
    // Glyph preparation is an explicit content-change boundary. No uploads,
    // descriptor allocation or heap growth occur for a warmed unchanged frame.
    if (p.atlas_dirty && p.font_ready)
    {
        auto image = p.upload_pixels(p.atlas_width, p.atlas_height, VK_FORMAT_R8_UNORM,
                                     p.atlas_pixels.data(), p.atlas_pixels.size());
        p.font_image = std::move(image);
        p.write_set(p.font_set, p.font_image.view);
        p.atlas_dirty = false;
    }
    p.ensure_vertex_buffer(p.vertices.size() * sizeof(Impl::Vertex));
    std::memcpy(p.vbo.mapped_data, p.vertices.data(), p.vertices.size() * sizeof(Impl::Vertex));
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = p.render_pass;
    begin.framebuffer = p.target.framebuffer;
    begin.renderArea.extent = {static_cast<std::uint32_t>(p.width),
                               static_cast<std::uint32_t>(p.height)};
    const auto commands = p.target.commands;
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0, 0, static_cast<float>(p.width), static_cast<float>(p.height),
                              0, 1};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commands, 0, 1, &p.vbo.handle, &offset);
    // Transparency requires exact painter order. Never group by primitive type.
    for (const auto& draw : p.draws)
    {
        if (!draw.clip.extent.width || !draw.clip.extent.height)
            continue;
        auto clip = draw.clip;
        clip.offset.y = p.height - clip.offset.y - static_cast<std::int32_t>(clip.extent.height);
        vkCmdSetScissor(commands, 0, 1, &clip);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          p.pipelines[static_cast<std::size_t>(draw.kind)]);
        vkCmdPushConstants(commands, p.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(RectPC), &draw.constants);
        if (draw.descriptor)
            vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 1,
                                    &draw.descriptor, 0, nullptr);
        vkCmdDraw(commands, draw.count, 1, draw.first, 0);
    }
    vkCmdEndRenderPass(commands);
    p.alpha = 1;
}

void UiRenderer::draw_rounded_rect(float x, float y, float w, float h, float radius,
                                   nekomote::ui::Color fill, nekomote::ui::Color border,
                                   float border_width)
{
    if (!impl_ || !impl_->initialized || w <= 0 || h <= 0)
        return;
    auto& p = *impl_;
    const auto first = static_cast<std::uint32_t>(p.vertices.size());
    auto pc = p.constants(fill);
    pc.params0[2] = w;
    pc.params0[3] = h;
    pc.params2[0] = border.r;
    pc.params2[1] = border.g;
    pc.params2[2] = border.b;
    pc.params2[3] = border.a * p.alpha;
    pc.params3[0] = std::max(0.0f, border_width);
    pc.params3[1] = std::clamp(radius, 0.0f, std::min(w, h) * 0.5f);
    pc.params3[2] = 1;
    p.add_quad(x, y, w, h);
    p.record(Impl::Kind::Rect, first, pc);
}

void UiRenderer::draw_text(float x, float y, const char* text, float size,
                           nekomote::ui::Color color)
{
    if (!impl_ || !impl_->initialized || !impl_->font_ready || !text || size <= 0)
        return;
    auto& p = *impl_;
    p.text_vertices.clear();
    p.text_uvs.clear();
    p.font_metrics.draw_text(p.text_vertices, p.text_uvs, x, y, text, size);
    const auto first = static_cast<std::uint32_t>(p.vertices.size());
    for (std::size_t i = 0; i < p.text_vertices.size(); i += 2)
        p.vertices.push_back(
            {p.text_vertices[i], p.text_vertices[i + 1], p.text_uvs[i], p.text_uvs[i + 1]});
    p.record(Impl::Kind::Text, first, p.constants(color), p.font_set);
}

void UiRenderer::draw_icon(float x, float y, float w, float h, std::uint32_t texture_id,
                           nekomote::ui::Color tint)
{
    if (!impl_ || !impl_->initialized || !texture_id || texture_id > impl_->icons.size() ||
        w <= 0 || h <= 0)
        return;
    auto& p = *impl_;
    const auto& icon = p.icons[texture_id - 1];
    if (!icon.descriptor)
        return;
    const auto first = static_cast<std::uint32_t>(p.vertices.size());
    p.add_quad(x, y, w, h);
    p.record(Impl::Kind::Icon, first, p.constants(tint), icon.descriptor);
}

void UiRenderer::draw_circle(float cx, float cy, float radius, nekomote::ui::Color fill,
                             nekomote::ui::Color border, float border_width)
{
    draw_rounded_rect(cx - radius, cy - radius, radius * 2, radius * 2, radius, fill, border,
                      border_width);
}

void UiRenderer::draw_solid_rect(float x, float y, float w, float h, nekomote::ui::Color color)
{
    if (!impl_ || !impl_->initialized || w <= 0 || h <= 0)
        return;
    auto& p = *impl_;
    const auto first = static_cast<std::uint32_t>(p.vertices.size());
    p.add_quad(x, y, w, h);
    p.record(Impl::Kind::Solid, first, p.constants(color));
}

void UiRenderer::draw_arrow(float cx, float cy, float size, bool points_up,
                            nekomote::ui::Color fill)
{
    if (!impl_ || !impl_->initialized || size <= 0)
        return;
    auto& p = *impl_;
    const auto first = static_cast<std::uint32_t>(p.vertices.size());
    const float edge = points_up ? size * 0.5f : -size * 0.5f;
    p.vertices.insert(
        p.vertices.end(),
        {{cx - size, cy + edge, 0, 0}, {cx + size, cy + edge, 0, 0}, {cx, cy - edge, 0, 0}});
    p.record(Impl::Kind::Solid, first, p.constants(fill));
}

void UiRenderer::push_clip_rect(float x, float y, float w, float h)
{
    if (!impl_ || !impl_->initialized)
        return;
    auto& p = *impl_;
    const auto previous = p.current_clip();
    const float left = std::clamp(x, static_cast<float>(previous.offset.x),
                                  static_cast<float>(previous.offset.x) +
                                      static_cast<float>(previous.extent.width));
    const float top = std::clamp(y, static_cast<float>(previous.offset.y),
                                 static_cast<float>(previous.offset.y) +
                                     static_cast<float>(previous.extent.height));
    const float right = std::clamp(x + std::max(0.0f, w), left,
                                   static_cast<float>(previous.offset.x) +
                                       static_cast<float>(previous.extent.width));
    const float bottom = std::clamp(y + std::max(0.0f, h), top,
                                    static_cast<float>(previous.offset.y) +
                                        static_cast<float>(previous.extent.height));
    const auto ix = static_cast<std::int32_t>(left);
    const auto iy = static_cast<std::int32_t>(top);
    p.clips.push_back({{ix, iy},
                       {static_cast<std::uint32_t>(right) - static_cast<std::uint32_t>(ix),
                        static_cast<std::uint32_t>(bottom) - static_cast<std::uint32_t>(iy)}});
}

void UiRenderer::pop_clip_rect()
{
    if (impl_ && !impl_->clips.empty())
        impl_->clips.pop_back();
}

void UiRenderer::set_global_alpha(float alpha)
{
    if (impl_)
        impl_->alpha = std::isfinite(alpha) ? std::clamp(alpha, 0.0f, 1.0f) : 0;
}

float UiRenderer::global_alpha() const
{
    return impl_ ? impl_->alpha : 1;
}

int UiRenderer::viewport_width() const
{
    return impl_ ? impl_->width : 0;
}

int UiRenderer::viewport_height() const
{
    return impl_ ? impl_->height : 0;
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
