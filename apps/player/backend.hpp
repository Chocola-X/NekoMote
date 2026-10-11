#pragma once

#include <nekomote/image.hpp>
#include <nekomote/render_frame.hpp>
#include <nekomote/render_gl/renderer.hpp>
#include <nekomote/render_gl/ui_renderer.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#if defined(NEKOMOTE_HAS_VULKAN)
#include <nekomote/render_vk/renderer.hpp>
#include <nekomote/render_vk/ui_renderer.hpp>
#include "vk_host.hpp"
#include <vulkan/vulkan.h>
#endif

namespace nekomote::player
{

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

class BackendRenderer
{
  public:
    bool vulkan_{false};
    std::unique_ptr<nekomote::render_gl::Renderer> gl_;
#if defined(NEKOMOTE_HAS_VULKAN)
    std::unique_ptr<nekomote::render_vk::Renderer> vk_;
    nekomote::render_vk::DeviceContext vk_ctx_;
    VkHost* vk_host_{nullptr};
    std::uint32_t vk_image_index_{0};
#endif

    void reserve(BackendCapacity c)
    {
        if (gl_)
            gl_->reserve({c.vertices, c.indices, c.commands, c.textures, c.mask_commands,
                          c.masks});
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            vk_->reserve({c.vertices, c.indices, c.commands, c.textures, c.mask_commands,
                          c.masks});
#endif
    }

    void upload_texture(nekomote::TextureId id, const nekomote::ImageRgba8& img)
    {
        if (gl_)
            gl_->upload_texture(id, img);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            vk_->upload_texture(id, img);
#endif
    }

    void prepare(nekomote::RenderFrame frame)
    {
        if (gl_)
            gl_->prepare(frame);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            vk_->prepare(frame);
#endif
    }

    void resize_output(int w, int h)
    {
        if (gl_)
            gl_->resize_output(w, h);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            vk_->resize_output(w, h);
#endif
    }

    void render(const BackendView& view, BackendContents contents)
    {
        const auto gl_view = nekomote::render_gl::View{
            view.origin_x, view.origin_y, view.scale, view.clear, view.alpha, view.flip_x};
        const auto gl_contents = contents == BackendContents::Clear
                                     ? nekomote::render_gl::TargetContents::Clear
                                     : nekomote::render_gl::TargetContents::Preserve;
        if (gl_)
            gl_->render({0, 0, 0}, gl_view, gl_contents);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_ && vk_host_)
        {
            const auto& frame = vk_host_->frames[vk_image_index_];
            const auto vk_view = nekomote::render_vk::View{
                view.origin_x, view.origin_y, view.scale, view.clear, view.alpha, view.flip_x};
            const auto vk_contents = contents == BackendContents::Clear
                                         ? nekomote::render_vk::TargetContents::Clear
                                         : nekomote::render_vk::TargetContents::Preserve;
            vk_->render({frame.framebuffer,
                         static_cast<int>(vk_host_->extent.width),
                         static_cast<int>(vk_host_->extent.height)},
                        vk_view, vk_contents);
        }
#endif
    }

    nekomote::ImageRgba8 capture()
    {
        if (gl_)
            return gl_->capture({0, 0, 0});
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_ && vk_host_)
        {
            const auto& frame = vk_host_->frames[vk_image_index_];
            return vk_->capture({frame.framebuffer,
                                 static_cast<int>(vk_host_->extent.width),
                                 static_cast<int>(vk_host_->extent.height)});
        }
#endif
        return {};
    }

    std::string diagnostics() const
    {
        if (gl_)
            return gl_->diagnostics();
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            return vk_->diagnostics();
#endif
        return {};
    }

    void reset()
    {
        gl_.reset();
#if defined(NEKOMOTE_HAS_VULKAN)
        vk_.reset();
#endif
    }
};

class BackendUiRenderer
{
  public:
    bool vulkan_{false};
    std::unique_ptr<nekomote::render_gl::UiRenderer> gl_;
#if defined(NEKOMOTE_HAS_VULKAN)
    std::unique_ptr<nekomote::render_vk::UiRenderer> vk_;
#endif

    bool init(bool vulkan, int w, int h
#if defined(NEKOMOTE_HAS_VULKAN)
              ,
              const nekomote::render_vk::DeviceContext* vk_ctx = nullptr
#endif
    )
    {
        vulkan_ = vulkan;
        if (!vulkan)
        {
            gl_ = std::make_unique<nekomote::render_gl::UiRenderer>();
            return gl_->init(w, h);
        }
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_ctx)
        {
            vk_ = std::make_unique<nekomote::render_vk::UiRenderer>();
            return vk_->init(*vk_ctx, w, h);
        }
#endif
        return false;
    }

    void reset()
    {
        if (gl_)
            gl_->reset();
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            vk_->reset();
#endif
    }

    void resize(int w, int h)
    {
        if (gl_)
            gl_->resize(w, h);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            vk_->resize(w, h);
#endif
    }

    bool init_font(const std::string& path, float px)
    {
        if (gl_)
            return gl_->init_font(path, px);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            return vk_->init_font(path, px);
#endif
        return false;
    }

    std::uint32_t upload_icon(const nekomote::ImageRgba8& img)
    {
        if (gl_)
            return gl_->upload_icon(img);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            return vk_->upload_icon(img);
#endif
        return 0;
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

    void set_render_target_gl(std::uint32_t fb)
    {
        if (gl_)
            gl_->set_render_target(fb);
    }

#if defined(NEKOMOTE_HAS_VULKAN)
    void set_render_target_vk(VkFramebuffer fb)
    {
        if (vk_)
            vk_->set_render_target(fb);
    }
#endif

    nekomote::ui::Painter* painter()
    {
        if (gl_)
            return gl_.get();
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            return vk_.get();
#endif
        return nullptr;
    }

    void end_frame()
    {
        if (gl_)
            gl_->end_frame();
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            vk_->end_frame();
#endif
    }

    void set_global_alpha(float a)
    {
        if (gl_)
            gl_->set_global_alpha(a);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            vk_->set_global_alpha(a);
#endif
    }

    void push_clip_rect(float x, float y, float w, float h)
    {
        if (gl_)
            gl_->push_clip_rect(x, y, w, h);
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            vk_->push_clip_rect(x, y, w, h);
#endif
    }

    void pop_clip_rect()
    {
        if (gl_)
            gl_->pop_clip_rect();
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            vk_->pop_clip_rect();
#endif
    }

    nekomote::ui::FontMetrics* font()
    {
        if (gl_)
            return gl_->font();
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_)
            return vk_->font();
#endif
        return nullptr;
    }
};

} // namespace nekomote::player
