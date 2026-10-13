#pragma once

#if defined(NEKOMOTE_HAS_VULKAN)
#include <vulkan/vulkan.h>
#endif

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <nekomote/image.hpp>
#include <nekomote/render_frame.hpp>
#include <nekomote/render_gl/renderer.hpp>
#include <nekomote/render_gl/ui_renderer.hpp>

#include <GLFW/glfw3.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#if defined(NEKOMOTE_HAS_VULKAN)
#include <nekomote/render_vk/renderer.hpp>
#include <nekomote/render_vk/ui_renderer.hpp>

#include "vk_host.hpp"
#endif

namespace nekomote::player
{
enum class BackendKind
{
    OpenGL,
    Vulkan
};
struct BackendView
{
    float origin_x{0}, origin_y{0}, scale{1};
    std::array<float, 4> clear{0, 0, 0, 0};
    float alpha{1};
    bool flip_x{false};
};
enum class BackendContents
{
    Clear,
    Preserve
};
struct BackendCapacity
{
    std::size_t vertices{0}, indices{0}, commands{0}, textures{0};
    std::size_t mask_commands{0}, masks{0};
};

// Player GPU boundary. Application sessions only produce RenderFrame; this
// execution object never decides model switching, input or wallpaper settings.
class BackendRenderer
{
  public:
    explicit BackendRenderer(BackendKind kind
#if defined(NEKOMOTE_HAS_VULKAN)
                             ,
                             const render_vk::DeviceContext* context = nullptr
#endif
    )
    {
        if (kind == BackendKind::OpenGL)
            gl_ = std::make_unique<render_gl::Renderer>(glfwGetProcAddress);
        else
        {
#if defined(NEKOMOTE_HAS_VULKAN)
            if (!context)
                throw std::runtime_error("Vulkan renderer needs a device");
            vk_ = std::make_unique<render_vk::Renderer>(*context);
#else
            throw std::runtime_error("Vulkan backend not built");
#endif
        }
    }
    void reserve(BackendCapacity c)
    {
        if (gl_)
            gl_->reserve({c.vertices, c.indices, c.commands, c.textures, c.mask_commands, c.masks});
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            vk_->reserve({c.vertices, c.indices, c.commands, c.textures, c.mask_commands, c.masks});
#endif
    }
    void upload_texture(TextureId id, ImageViewRgba8 image, TextureRegion region = {})
    {
        if (gl_)
            gl_->upload_texture(id, image, region);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            vk_->upload_texture(id, image, region);
#endif
    }
    void prepare(RenderFrame frame)
    {
        if (gl_)
            gl_->prepare(frame);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            vk_->prepare(frame);
#endif
    }
    void resize_output(int width, int height)
    {
        if (gl_)
            gl_->resize_output(width, height);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            vk_->resize_output(width, height);
#endif
    }
#if defined(NEKOMOTE_HAS_VULKAN)
    void set_target(render_vk::Target target, VkHost& host)
    {
        target_ = target;
        host_ = &host;
    }
#endif
    void render(const BackendView& v, BackendContents contents, int width, int height)
    {
        if (gl_)
            gl_->render({0, width, height},
                        {v.origin_x, v.origin_y, v.scale, v.clear, v.alpha, v.flip_x},
                        contents == BackendContents::Clear ? render_gl::TargetContents::Clear
                                                           : render_gl::TargetContents::Preserve);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
        {
            if (!host_ || target_.width != width || target_.height != height)
                throw std::runtime_error("Vulkan renderer target not bound");
            vk_->render(target_, {v.origin_x, v.origin_y, v.scale, v.clear, v.alpha, v.flip_x},
                        contents == BackendContents::Clear ? render_vk::TargetContents::Clear
                                                           : render_vk::TargetContents::Preserve);
        }
#endif
    }
    ImageRgba8 capture(int width, int height)
    {
        if (gl_)
            return gl_->capture({0, width, height});
#if defined(NEKOMOTE_HAS_VULKAN)
        if (host_)
            return host_->capture(width, height);
#endif
        throw std::runtime_error("renderer capture has no target");
    }
    std::string diagnostics() const
    {
        if (gl_)
            return gl_->diagnostics();
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            return vk_->diagnostics();
#endif
        throw std::runtime_error("renderer is unavailable");
    }

  private:
    std::unique_ptr<render_gl::Renderer> gl_;
#if defined(NEKOMOTE_HAS_VULKAN)
    std::unique_ptr<render_vk::Renderer> vk_;
    render_vk::Target target_{};
    VkHost* host_{nullptr};
#endif
};

class BackendUiRenderer
{
  public:
    bool init(BackendKind kind, int width, int height
#if defined(NEKOMOTE_HAS_VULKAN)
              ,
              const render_vk::DeviceContext* context = nullptr
#endif
    )
    {
        if (kind == BackendKind::OpenGL)
        {
            gl_ = std::make_unique<render_gl::UiRenderer>();
            return gl_->init(width, height);
        }
#if defined(NEKOMOTE_HAS_VULKAN)
        if (context)
        {
            vk_ = std::make_unique<render_vk::UiRenderer>();
            return vk_->init(*context, width, height);
        }
#endif
        throw std::runtime_error("Vulkan UI requires a built backend and device");
    }
    ui::Painter* painter()
    {
        if (gl_)
            return gl_.get();
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            return vk_.get();
#endif
        return nullptr;
    }
    void resize(int width, int height)
    {
        if (gl_)
            gl_->resize(width, height);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            vk_->resize(width, height);
#endif
    }
    bool init_font(const std::string& path, float size)
    {
        if (gl_)
            return gl_->init_font(path, size);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            return vk_->init_font(path, size);
#endif
        return false;
    }
    std::uint32_t upload_icon(ImageViewRgba8 image)
    {
        if (gl_)
            return gl_->upload_icon(image);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            return vk_->upload_icon(image);
#endif
        throw std::runtime_error("UI is unavailable");
    }
    void destroy_icon(std::uint32_t id)
    {
        if (gl_)
            gl_->destroy_icon(id);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            vk_->destroy_icon(id);
#endif
    }
#if defined(NEKOMOTE_HAS_VULKAN)
    void set_target(render_vk::Target target)
    {
        if (vk_)
            vk_->set_render_target(target);
    }
#endif
    void end_frame()
    {
        painter()->end_frame();
    }
    void set_global_alpha(float alpha)
    {
        painter()->set_global_alpha(alpha);
    }
    void push_clip_rect(float x, float y, float w, float h)
    {
        painter()->push_clip_rect(x, y, w, h);
    }
    void pop_clip_rect()
    {
        painter()->pop_clip_rect();
    }
    ui::FontMetrics* font()
    {
        return painter() ? painter()->font() : nullptr;
    }

  private:
    std::unique_ptr<render_gl::UiRenderer> gl_;
#if defined(NEKOMOTE_HAS_VULKAN)
    std::unique_ptr<render_vk::UiRenderer> vk_;
#endif
};
} // namespace nekomote::player
