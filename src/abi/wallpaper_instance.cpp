#include "wallpaper_instance.hpp"

#include <nekomote/asset_io/image.hpp>
#include <nekomote/ui/menu.hpp>

#include <fcntl.h>
#include <nekomote/abi.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <thread>

#include "abi_internal.hpp"

namespace nekomote::abi
{
namespace
{

nekomote_error ok_or_set(const application::SessionStatus& status)
{
    if (status)
        return NEKOMOTE_OK;
    switch (status.error)
    {
        case application::SessionError::InvalidInput:
            return NEKOMOTE_ERROR_INVALID_INPUT;
        case application::SessionError::InvalidReaction:
            return NEKOMOTE_ERROR_INVALID_REACTION;
        case application::SessionError::Busy:
            return NEKOMOTE_ERROR_BUSY;
        case application::SessionError::InputUnavailable:
            return NEKOMOTE_ERROR_INPUT_UNAVAILABLE;
        case application::SessionError::EventQueueFull:
            return NEKOMOTE_ERROR_EVENT_QUEUE_FULL;
        case application::SessionError::Playback:
            return NEKOMOTE_ERROR_PLAYBACK;
        case application::SessionError::Faulted:
            return NEKOMOTE_ERROR_FAULTED;
        case application::SessionError::None:
            return NEKOMOTE_OK;
    }
    return NEKOMOTE_ERROR_FAULTED;
}

} // namespace

WallpaperInstance::WallpaperInstance(render_gl::GlLoader loader) : loader_(loader)
{
    controller_cfg_.pointer = application::PointerScope::LocalSurface;
    controller_cfg_.audio = application::AudioMode::Disabled;
}

#if defined(NEKOMOTE_HAS_VULKAN)
WallpaperInstance::WallpaperInstance(const render_vk::DeviceContext& ctx)
    : backend_(WallpaperBackend::Vulkan), vk_ctx_(ctx)
{
    controller_cfg_.pointer = application::PointerScope::LocalSurface;
    controller_cfg_.audio = application::AudioMode::Disabled;
}

nekomote_error WallpaperInstance::set_vk_target(render_vk::Target target)
{
    vk_target_set_ = false;
    if (backend_ != WallpaperBackend::Vulkan)
    {
        set_error(NEKOMOTE_ERROR_UNSUPPORTED, "Vulkan target on OpenGL wallpaper");
        return NEKOMOTE_ERROR_UNSUPPORTED;
    }
    if (!target.commands || !target.framebuffer || target.width <= 0 || target.height <= 0 ||
        target.width != output_width_ || target.height != output_height_)
    {
        set_error(NEKOMOTE_ERROR_INVALID_INPUT, "invalid Vulkan target handles or output size");
        return NEKOMOTE_ERROR_INVALID_INPUT;
    }
    vk_target_ = target;
    vk_target_set_ = true;
    return NEKOMOTE_OK;
}
#endif

WallpaperInstance::~WallpaperInstance()
{
    reset_gpu_resources();
}

nekomote_error WallpaperInstance::set_assets_dir(const std::filesystem::path& dir)
{
    assets_dir_ = dir;
    if (!controller_.scan_assets(dir))
    {
        error_string_ = "no characters found in " + dir.string();
        set_error(NEKOMOTE_ERROR_NOT_FOUND, error_string_.c_str());
        return NEKOMOTE_ERROR_NOT_FOUND;
    }
    assets_scanned_ = true;
    auto config_path = dir / "config.json";
    {
        // Effective credentials include group/ACL permissions. Do not create a
        // config just to probe it; a read-only existing file also needs fallback.
        std::error_code ec;
        const bool exists = std::filesystem::exists(config_path, ec);
        const auto& probe_path = exists ? config_path : dir;
        const int permissions = exists ? W_OK : W_OK | X_OK;
        const bool dir_writable =
            !ec && (!exists || std::filesystem::is_regular_file(config_path, ec)) && !ec &&
            ::faccessat(AT_FDCWD, probe_path.c_str(), permissions, AT_EACCESS) == 0;
        if (!dir_writable)
        {
            const char* xdg = std::getenv("XDG_DATA_HOME");
            std::filesystem::path user_dir;
            if (xdg && *xdg)
                user_dir = std::filesystem::path(xdg) / "nekomote";
            else
            {
                const char* home = std::getenv("HOME");
                if (!home || !*home)
                {
                    error_string_ = "cannot determine writable config directory: HOME not set";
                    set_error(NEKOMOTE_ERROR_NOT_FOUND, error_string_.c_str());
                    return NEKOMOTE_ERROR_NOT_FOUND;
                }
                user_dir = std::filesystem::path(home) / ".local" / "share" / "nekomote";
            }
            std::filesystem::create_directories(user_dir);
            config_path = user_dir / "config.json";
        }
    }
    controller_.set_config_path(config_path);
    controller_.load_config(config_path);
    auto& config = controller_.config();
    if (!mute_forced_)
    {
        panel_.muted = config.mute;
        controller_cfg_.mute = config.mute;
        controller_cfg_.audio =
            config.mute ? application::AudioMode::Disabled : application::AudioMode::Device;
    }
    panel_.fps_limit = config.fps;
    panel_.icon = {};
    panel_.sidebar = {};
    if (config.icon_position_saved)
    {
        panel_.icon.x = config.icon_x;
        panel_.icon.y = config.icon_y;
        panel_.icon.initialized = true;
    }
    if (config.sidebar_position_saved && config.sidebar_free)
    {
        panel_.sidebar.x = config.sidebar_x;
        panel_.sidebar.y = config.sidebar_y;
        panel_.sidebar.is_free = true;
    }
    if (config.character.empty())
        config.character = controller_.catalog().characters.front().name;
    config.fit = true;
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::set_no_save(bool value)
{
    controller_.set_no_save(value);
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::set_muted(bool value)
{
    mute_forced_ = true;
    panel_.muted = value;
    controller_cfg_.mute = value;
    controller_cfg_.audio =
        value ? application::AudioMode::Disabled : application::AudioMode::Device;
    if (value)
        audio_device_.reset();
    else
    {
        audio_failed_ = false;
        if (!audio_device_ && !audio_clips_.empty())
        {
            try
            {
                auto device = std::make_unique<audio::Device>(audio_clips_);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
                while (device->snapshot().state == audio::DeviceState::Connecting &&
                       std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                const auto snapshot = device->snapshot();
                if (snapshot.state == audio::DeviceState::Failed)
                    audio_failed_ = true;
                else
                    audio_device_ = std::move(device);
            }
            catch (const std::exception&)
            {
                audio_failed_ = true;
            }
        }
    }
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::set_seed(std::uint32_t seed)
{
    controller_cfg_.seed = seed;
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::set_output_size(int width, int height)
{
    if (width < 0 || height < 0)
    {
        set_error(NEKOMOTE_ERROR_INVALID_INPUT, "negative output size");
        return NEKOMOTE_ERROR_INVALID_INPUT;
    }
#if defined(NEKOMOTE_HAS_VULKAN)
    if (width != output_width_ || height != output_height_)
        vk_target_set_ = false;
#endif
    output_width_ = width;
    output_height_ = height;
    controller_.set_output_size(width, height);
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::set_paused(bool paused)
{
    paused_ = paused;
    controller_.set_paused(paused);
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::set_ui_open(bool open)
{
    panel_.sidebar.open = open;
    return NEKOMOTE_OK;
}

void WallpaperInstance::set_render_target_fbo(std::uint32_t fbo)
{
    render_target_fbo_ = fbo;
}

void WallpaperInstance::reset_gpu_resources()
{
#if defined(NEKOMOTE_HAS_VULKAN)
    vk_target_set_ = false;
#endif
    if (backend_ == WallpaperBackend::OpenGL)
    {
        renderer_.reset();
        previous_renderer_.reset();
        background_renderer_.reset();
        ui_renderer_.destroy_icon(panel_.icon_texture);
        ui_renderer_.reset();
    }
#if defined(NEKOMOTE_HAS_VULKAN)
    else
    {
        vk_renderer_.reset();
        vk_previous_renderer_.reset();
        vk_background_renderer_.reset();
        vk_ui_renderer_.destroy_icon(panel_.icon_texture);
        vk_ui_renderer_.reset();
    }
#endif
    panel_.ui_ready = false;
    panel_.icon_texture = 0;
    last_render_width_ = 0;
    last_render_height_ = 0;
}

nekomote_error WallpaperInstance::load_initial()
{
    if (!assets_scanned_)
    {
        error_string_ = "assets not scanned";
        set_error(NEKOMOTE_ERROR_INVALID_INPUT, error_string_.c_str());
        return NEKOMOTE_ERROR_INVALID_INPUT;
    }
    const auto& catalog = controller_.catalog();
    const auto* initial = catalog.find_character(controller_.config().character);
    if (!initial)
        initial = &catalog.characters.front();
    int initial_index = static_cast<int>(initial - catalog.characters.data());
    auto candidate = controller_.load_initial(initial_index, controller_cfg_);
    if (!candidate.ok)
    {
        error_string_ = candidate.error;
        set_error(NEKOMOTE_ERROR_FAULTED, error_string_.c_str());
        return NEKOMOTE_ERROR_FAULTED;
    }
    finish_candidate_gpu(candidate);
    create_audio_device(candidate);
    controller_.install_initial(std::move(candidate));
    if (output_width_ > 0 && output_height_ > 0)
        controller_.set_output_size(output_width_, output_height_);
    if (!controller_.config().background.empty())
    {
        const auto* bg = catalog.find_background(controller_.config().background);
        if (bg)
        {
            int bg_index = static_cast<int>(bg - catalog.backgrounds.data());
            apply_background(controller_.background_path(bg_index), 0, bg_index);
        }
    }
    initialized_ = true;
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::load_model(const std::filesystem::path& model_path,
                                             const std::filesystem::path& session_path)
{
    application::CharacterEntry temp_entry{model_path.stem().string(), model_path, session_path,
                                           model_path.parent_path()};
    auto candidate = controller_.load_initial(temp_entry, controller_cfg_);
    if (!candidate.ok)
    {
        error_string_ = candidate.error;
        set_error(NEKOMOTE_ERROR_FAULTED, error_string_.c_str());
        return NEKOMOTE_ERROR_FAULTED;
    }
    finish_candidate_gpu(candidate);
    create_audio_device(candidate);
    controller_.install_initial(std::move(candidate));
    if (output_width_ > 0 && output_height_ > 0)
    {
        controller_.set_output_size(output_width_, output_height_);
        controller_.prepare_frame();
    }
    initialized_ = true;
    return NEKOMOTE_OK;
}

void WallpaperInstance::finish_candidate_gpu(const application::CandidateCpu& candidate)
{
    const auto& scene = candidate.program->playback().scene();
    const auto capacity = scene.frame_capacity();
    // Complete all potentially failing GPU work while the active model remains
    // intact. The host has waited for its previous submission before this call.
    if (output_width_ > 0 && output_height_ > 0)
    {
        if (!candidate.instance->set_output_size(output_width_, output_height_) ||
            !candidate.instance->prepare_frame())
            throw std::runtime_error("candidate frame preparation failed");
    }
    const bool switching = controller_.switch_state() == application::SwitchState::Pending;
    if (backend_ == WallpaperBackend::OpenGL)
    {
        auto next = std::make_unique<render_gl::Renderer>(loader_);
        next->reserve({capacity.vertices, capacity.indices, capacity.commands,
                       scene.textures().size(), capacity.mask_commands, capacity.masks});
        for (const auto& [id, image, region] : candidate.textures)
            next->upload_texture(id, image, region);
        if (output_width_ > 0 && output_height_ > 0)
        {
            next->prepare(candidate.instance->frame());
            next->resize_output(output_width_, output_height_);
        }
        std::unique_ptr<render_gl::Renderer> empty_background;
        if (!background_renderer_)
        {
            empty_background = std::make_unique<render_gl::Renderer>(loader_);
            empty_background->reserve({8, 12, 2, 2});
        }
        if (switching)
            previous_renderer_ = std::move(renderer_);
        renderer_ = std::move(next);
        if (empty_background)
            background_renderer_ = std::move(empty_background);
    }
#if defined(NEKOMOTE_HAS_VULKAN)
    else
    {
        auto next = std::make_unique<render_vk::Renderer>(vk_ctx_);
        next->reserve({capacity.vertices, capacity.indices, capacity.commands,
                       scene.textures().size(), capacity.mask_commands, capacity.masks});
        for (const auto& [id, image, region] : candidate.textures)
            next->upload_texture(id, image, region);
        if (output_width_ > 0 && output_height_ > 0)
        {
            next->prepare(candidate.instance->frame());
            next->resize_output(output_width_, output_height_);
        }
        std::unique_ptr<render_vk::Renderer> empty_background;
        if (!vk_background_renderer_)
        {
            empty_background = std::make_unique<render_vk::Renderer>(vk_ctx_);
            empty_background->reserve({8, 12, 2, 2});
        }
        if (switching)
            vk_previous_renderer_ = std::move(vk_renderer_);
        vk_renderer_ = std::move(next);
        if (empty_background)
            vk_background_renderer_ = std::move(empty_background);
    }
#endif
    last_render_width_ = 0;
    last_render_height_ = 0;
}

void WallpaperInstance::create_audio_device(application::CandidateCpu& candidate)
{
    if (controller_cfg_.audio != application::AudioMode::Device)
        return;
    if (candidate.audio_clips.empty())
        return;
    audio_clips_ = candidate.audio_clips;
    if (audio_failed_)
        return;
    try
    {
        auto device = std::make_unique<audio::Device>(std::move(candidate.audio_clips));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        while (device->snapshot().state == audio::DeviceState::Connecting &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto snapshot = device->snapshot();
        if (snapshot.state == audio::DeviceState::Failed)
        {
            audio_failed_ = true;
            error_string_ = "audio device unavailable (error " + std::to_string(snapshot.error) +
                            "); continuing muted";
            return;
        }
        audio_device_ = std::move(device);
    }
    catch (const std::exception& error)
    {
        audio_failed_ = true;
        error_string_ =
            std::string("audio device creation failed: ") + error.what() + "; continuing muted";
    }
}

void WallpaperInstance::apply_background(const std::filesystem::path& path, double fade_duration,
                                         int bg_index)
{
    auto new_image = asset_io::load_png(path);
    if (!new_image.ok)
    {
        error_string_ = new_image.error;
        return;
    }
    if (!controller_.has_background() || fade_duration <= 0)
    {
        const TextureId id{1};
        if (backend_ == WallpaperBackend::OpenGL)
        {
            auto next = std::make_unique<render_gl::Renderer>(loader_);
            next->reserve({8, 12, 2, 2});
            next->upload_texture(id, new_image.image);
            background_renderer_ = std::move(next);
        }
#if defined(NEKOMOTE_HAS_VULKAN)
        else
        {
            auto next = std::make_unique<render_vk::Renderer>(vk_ctx_);
            next->reserve({8, 12, 2, 2});
            next->upload_texture(id, new_image.image);
            vk_background_renderer_ = std::move(next);
        }
#endif
        controller_.confirm_background(bg_index, id, id, new_image.image.width,
                                       new_image.image.height, 0);
        current_background_id_ = id;
        current_background_width_ = new_image.image.width;
        current_background_height_ = new_image.image.height;
        current_background_path_ = path;
        return;
    }
    auto old_image = asset_io::load_png(current_background_path_);
    if (!old_image.ok)
    {
        error_string_ = old_image.error;
        return;
    }
    const TextureId old_id{1}, new_id{2};
    if (backend_ == WallpaperBackend::OpenGL)
    {
        auto next = std::make_unique<render_gl::Renderer>(loader_);
        next->reserve({8, 12, 2, 2});
        next->upload_texture(old_id, old_image.image);
        next->upload_texture(new_id, new_image.image);
        background_renderer_ = std::move(next);
    }
#if defined(NEKOMOTE_HAS_VULKAN)
    else
    {
        auto next = std::make_unique<render_vk::Renderer>(vk_ctx_);
        next->reserve({8, 12, 2, 2});
        next->upload_texture(old_id, old_image.image);
        next->upload_texture(new_id, new_image.image);
        vk_background_renderer_ = std::move(next);
    }
#endif
    controller_.confirm_background(bg_index, old_id, new_id, new_image.image.width,
                                   new_image.image.height, fade_duration);
    current_background_id_ = new_id;
    current_background_width_ = new_image.image.width;
    current_background_height_ = new_image.image.height;
    current_background_path_ = path;
}

void WallpaperInstance::handle_switch_poll()
{
    if (controller_.switch_state() != application::SwitchState::Pending)
        return;
    auto candidate_opt = controller_.poll_candidate();
    if (!candidate_opt)
        return;
    if (!candidate_opt->ok)
    {
        controller_.reject_candidate(candidate_opt->error);
        return;
    }
    try
    {
        const int bg_index = controller_.current_background_index();
        const bool keep_background = controller_.has_background();
        finish_candidate_gpu(*candidate_opt);
        create_audio_device(*candidate_opt);
        controller_.confirm_candidate(std::move(*candidate_opt));
        if (keep_background)
            controller_.confirm_background(bg_index, current_background_id_, current_background_id_,
                                           current_background_width_, current_background_height_,
                                           0);
        if (controller_.config().fit && output_width_ > 0 && output_height_ > 0)
        {
            controller_.prepare_frame();
            controller_.fit_to_output(output_width_, output_height_);
        }
        if (output_width_ > 0 && output_height_ > 0)
        {
            controller_.set_output_size(output_width_, output_height_);
            controller_.prepare_frame();
            if (backend_ == WallpaperBackend::OpenGL)
            {
                renderer_->prepare(controller_.current_instance()->frame());
                renderer_->resize_output(output_width_, output_height_);
                if (controller_.has_background())
                {
                    background_renderer_->prepare(
                        controller_.current_instance()->background_frame());
                    background_renderer_->resize_output(output_width_, output_height_);
                }
            }
#if defined(NEKOMOTE_HAS_VULKAN)
            else
            {
                vk_renderer_->prepare(controller_.current_instance()->frame());
                vk_renderer_->resize_output(output_width_, output_height_);
                if (controller_.has_background())
                {
                    vk_background_renderer_->prepare(
                        controller_.current_instance()->background_frame());
                    vk_background_renderer_->resize_output(output_width_, output_height_);
                }
            }
#endif
        }
        controller_.set_paused(paused_);
        controller_.set_minimized(minimized_);
        controller_.set_output_unavailable(output_unavailable_);
    }
    catch (const std::exception& error)
    {
        controller_.reject_candidate(error.what());
    }
}

void WallpaperInstance::handle_background_poll()
{
    auto bg_req = controller_.poll_background();
    if (!bg_req)
        return;
    if (bg_req->action == application::BackgroundAction::Switch)
        apply_background(bg_req->path, bg_req->fade_duration, bg_req->background_index);
    else if (bg_req->action == application::BackgroundAction::Clear)
    {
        controller_.confirm_background_clear();
        background_renderer_.reset();
#if defined(NEKOMOTE_HAS_VULKAN)
        vk_background_renderer_.reset();
#endif
        current_background_id_ = {};
        current_background_width_ = current_background_height_ = 0;
        current_background_path_.clear();
    }
}

nekomote_error WallpaperInstance::advance(double dt)
{
#if defined(NEKOMOTE_HAS_VULKAN)
    vk_target_set_ = false;
#endif
    if (!std::isfinite(dt) || dt < 0)
    {
        set_error(NEKOMOTE_ERROR_INVALID_INPUT, "invalid wallpaper delta");
        return NEKOMOTE_ERROR_INVALID_INPUT;
    }
    if (!initialized_)
        return NEKOMOTE_OK;
    handle_switch_poll();
    controller_.update_fade();
    if (controller_.switch_state() == application::SwitchState::Idle && previous_renderer_)
        previous_renderer_.reset();
#if defined(NEKOMOTE_HAS_VULKAN)
    if (controller_.switch_state() == application::SwitchState::Idle && vk_previous_renderer_)
        vk_previous_renderer_.reset();
#endif
    handle_background_poll();
    if (output_width_ <= 0 || output_height_ <= 0 || minimized_)
        return NEKOMOTE_OK;
    if (audio_device_)
    {
        while (const auto audio_command = controller_.pop_audio_command())
        {
            if (!audio_device_->submit(*audio_command))
            {
                error_string_ = "audio command queue full or device failed";
                set_error(NEKOMOTE_ERROR_PLAYBACK, error_string_.c_str());
                return NEKOMOTE_ERROR_PLAYBACK;
            }
        }
        const auto snapshot = audio_device_->snapshot();
        if (snapshot.state == audio::DeviceState::Failed)
        {
            error_string_ = "audio device failed " + std::to_string(snapshot.error);
            set_error(NEKOMOTE_ERROR_PLAYBACK, error_string_.c_str());
            return NEKOMOTE_ERROR_PLAYBACK;
        }
        controller_.set_audio_level(snapshot.rms);
    }
    controller_.advance(dt);
    return check_session();
}

nekomote_error WallpaperInstance::check_session()
{
    const auto status = controller_.last_error();
    if (status)
        return NEKOMOTE_OK;
    error_string_ = "session failure";
    return ok_or_set(status);
}

nekomote_error WallpaperInstance::render_frame()
{
    if (!initialized_)
        return NEKOMOTE_OK;
    if (output_width_ <= 0 || output_height_ <= 0 || minimized_)
        return NEKOMOTE_OK;
#if defined(NEKOMOTE_HAS_VULKAN)
    if (backend_ == WallpaperBackend::Vulkan && (!vk_target_set_ || !vk_renderer_))
    {
        set_error(NEKOMOTE_ERROR_RENDERER, "Vulkan target or model GPU resources unavailable");
        return NEKOMOTE_ERROR_RENDERER;
    }
#endif
    if (backend_ == WallpaperBackend::OpenGL && !renderer_)
    {
        set_error(NEKOMOTE_ERROR_RENDERER, "model GPU resources unavailable after reset");
        return NEKOMOTE_ERROR_RENDERER;
    }
    controller_.prepare_frame();
    auto status = controller_.last_error();
    if (!status)
    {
        error_string_ = "prepare_frame failed";
        return ok_or_set(status);
    }
    const bool in_crossfade = controller_.switch_state() == application::SwitchState::CrossFade &&
                              controller_.previous_instance();
    const int w = output_width_;
    const int h = output_height_;
    const std::array<float, 4> clear = {0.06f, 0.07f, 0.10f, 1.0f};
    RenderFrame bg_frame{};
    if (controller_.has_background())
        bg_frame = controller_.current_instance()->background_frame();
    const float fade_alpha = controller_.fade_alpha();
    const float cx = static_cast<float>(w) * .5f;
    const float cy = static_cast<float>(h) * .5f;

    if (backend_ == WallpaperBackend::OpenGL)
    {
        renderer_->prepare(controller_.current_instance()->frame());
        if (in_crossfade)
        {
            controller_.previous_instance()->prepare_frame();
            previous_renderer_->prepare(controller_.previous_instance()->frame());
        }
        if (w != last_render_width_ || h != last_render_height_)
        {
            renderer_->resize_output(w, h);
            if (in_crossfade && previous_renderer_)
                previous_renderer_->resize_output(w, h);
            if (background_renderer_)
                background_renderer_->resize_output(w, h);
            last_render_width_ = w;
            last_render_height_ = h;
        }
        const render_gl::Target target{render_target_fbo_, w, h};
        if (!bg_frame.commands.empty())
        {
            background_renderer_->prepare(bg_frame);
            background_renderer_->render(target, {0, 0, 1, clear});
        }
        const auto tc = !bg_frame.commands.empty() ? render_gl::TargetContents::Preserve
                                                   : render_gl::TargetContents::Clear;
        if (in_crossfade)
        {
            previous_renderer_->render(
                target,
                {cx, cy, 1, clear, 1.0f - fade_alpha, controller_.previous_program()->mirror()},
                tc);
            renderer_->render(target, {cx, cy, 1, clear, fade_alpha, controller_.mirror()},
                              render_gl::TargetContents::Preserve);
        }
        else
        {
            renderer_->render(
                target,
                {cx, cy, 1, clear,
                 controller_.switch_state() == application::SwitchState::Idle ? 1.0f : fade_alpha,
                 controller_.mirror()},
                tc);
        }
    }
#if defined(NEKOMOTE_HAS_VULKAN)
    else
    {
        if (!vk_target_set_)
        {
            set_error(NEKOMOTE_ERROR_RENDERER, "Vulkan target not set before render_frame");
            return NEKOMOTE_ERROR_RENDERER;
        }
        vk_renderer_->prepare(controller_.current_instance()->frame());
        if (in_crossfade)
        {
            controller_.previous_instance()->prepare_frame();
            vk_previous_renderer_->prepare(controller_.previous_instance()->frame());
        }
        if (w != last_render_width_ || h != last_render_height_)
        {
            vk_renderer_->resize_output(w, h);
            if (in_crossfade && vk_previous_renderer_)
                vk_previous_renderer_->resize_output(w, h);
            if (vk_background_renderer_)
                vk_background_renderer_->resize_output(w, h);
            last_render_width_ = w;
            last_render_height_ = h;
        }
        const render_vk::Target target = vk_target_;
        if (!bg_frame.commands.empty())
        {
            vk_background_renderer_->prepare(bg_frame);
            vk_background_renderer_->render(target, {0, 0, 1, clear});
        }
        const auto tc = !bg_frame.commands.empty() ? render_vk::TargetContents::Preserve
                                                   : render_vk::TargetContents::Clear;
        if (in_crossfade)
        {
            vk_previous_renderer_->render(
                target,
                {cx, cy, 1, clear, 1.0f - fade_alpha, controller_.previous_program()->mirror()},
                tc);
            vk_renderer_->render(target, {cx, cy, 1, clear, fade_alpha, controller_.mirror()},
                                 render_vk::TargetContents::Preserve);
        }
        else
        {
            vk_renderer_->render(
                target,
                {cx, cy, 1, clear,
                 controller_.switch_state() == application::SwitchState::Idle ? 1.0f : fade_alpha,
                 controller_.mirror()},
                tc);
        }
    }
#endif
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::render_ui(float dt)
{
    if (!std::isfinite(dt) || dt < 0)
    {
        set_error(NEKOMOTE_ERROR_INVALID_INPUT, "invalid UI delta");
        return NEKOMOTE_ERROR_INVALID_INPUT;
    }
    // Explicit model/session mode is a model-only diagnostic host, without an
    // asset catalog or wallpaper controls.
    if (!initialized_ || !assets_scanned_ || output_width_ <= 0 || output_height_ <= 0)
        return NEKOMOTE_OK;
#if defined(NEKOMOTE_HAS_VULKAN)
    if (backend_ == WallpaperBackend::Vulkan && !vk_target_set_)
    {
        set_error(NEKOMOTE_ERROR_RENDERER, "Vulkan target not set before render_ui");
        return NEKOMOTE_ERROR_RENDERER;
    }
#endif
    // Hosts may load before their first output size is known. Initialize at the
    // first drawable frame, including after explicit graphics-resource reset.
    if (!panel_.ui_ready)
    {
        const auto icon_path = assets_dir_ / "icons" / "toggle.png";
        auto icon_image = std::filesystem::exists(icon_path) ? asset_io::load_png(icon_path)
                                                             : asset_io::ImageResult{};
        if (icon_image.ok)
        {
            // hold until upload below
        }
        if (backend_ == WallpaperBackend::OpenGL)
        {
            ui_renderer_.reset();
            if (!ui_renderer_.init(output_width_, output_height_) ||
                !ui_renderer_.init_font("", 36))
            {
                ui_renderer_.reset();
                set_error(NEKOMOTE_ERROR_RENDERER,
                          "wallpaper UI shader or font initialization failed");
                return NEKOMOTE_ERROR_RENDERER;
            }
            ui_context_.init(&ui_renderer_);
            if (icon_image.ok)
            {
                panel_.icon_texture = ui_renderer_.upload_icon(icon_image.image);
                if (!panel_.icon_texture)
                {
                    set_error(NEKOMOTE_ERROR_RENDERER, "wallpaper UI icon upload failed");
                    return NEKOMOTE_ERROR_RENDERER;
                }
            }
        }
#if defined(NEKOMOTE_HAS_VULKAN)
        else
        {
            vk_ui_renderer_.reset();
            if (!vk_ui_renderer_.init(vk_ctx_, output_width_, output_height_) ||
                !vk_ui_renderer_.init_font("", 36))
            {
                vk_ui_renderer_.reset();
                set_error(NEKOMOTE_ERROR_RENDERER,
                          "wallpaper UI shader or font initialization failed");
                return NEKOMOTE_ERROR_RENDERER;
            }
            ui_context_.init(&vk_ui_renderer_);
            if (icon_image.ok)
            {
                panel_.icon_texture = vk_ui_renderer_.upload_icon(icon_image.image);
                if (!panel_.icon_texture)
                {
                    set_error(NEKOMOTE_ERROR_RENDERER, "wallpaper UI icon upload failed");
                    return NEKOMOTE_ERROR_RENDERER;
                }
            }
        }
#endif
        if (!icon_image.ok && std::filesystem::exists(icon_path))
        {
            set_error(NEKOMOTE_ERROR_RENDERER, icon_image.error.c_str());
            return NEKOMOTE_ERROR_RENDERER;
        }
        panel_.ui_ready = true;
    }
    if (backend_ == WallpaperBackend::OpenGL)
    {
        ui_renderer_.resize(output_width_, output_height_);
        ui_renderer_.set_render_target(render_target_fbo_);
    }
#if defined(NEKOMOTE_HAS_VULKAN)
    else
    {
        if (!vk_target_set_)
        {
            set_error(NEKOMOTE_ERROR_RENDERER, "Vulkan target not set before render_ui");
            return NEKOMOTE_ERROR_RENDERER;
        }
        vk_ui_renderer_.resize(output_width_, output_height_);
        vk_ui_renderer_.set_render_target(vk_target_);
    }
#endif
    draw_ui(paused_ ? 0.0f : std::min(dt, .25f));
    return NEKOMOTE_OK;
}

ui::Painter* WallpaperInstance::painter()
{
    if (backend_ == WallpaperBackend::OpenGL)
        return &ui_renderer_;
#if defined(NEKOMOTE_HAS_VULKAN)
    if (backend_ == WallpaperBackend::Vulkan)
        return &vk_ui_renderer_;
#endif
    return nullptr;
}

bool WallpaperInstance::ui_captures_mouse()
{
    const bool captured =
        panel_.ui_ready &&
        (ui_context_.mouse_in_rect(panel_.icon.x, panel_.icon.y, panel_.icon.size,
                                   panel_.icon.size) ||
         (panel_.sidebar.animation > .05f &&
          ui_context_.mouse_in_rect(panel_.sidebar.x, panel_.sidebar.y, panel_.sidebar.width,
                                    panel_.sidebar.height)) ||
         panel_.icon.dragging || panel_.sidebar.dragging);
    ui_context_.set_captures_mouse(captured);
    return captured;
}

void WallpaperInstance::draw_ui(float dt)
{
    if (!panel_.ui_ready)
        return;
    ui_context_.begin_frame();
    if (ui_context_.toggle_icon(panel_.icon, panel_.icon_texture))
        panel_.sidebar.open = !panel_.sidebar.open;
    ui_context_.sidebar(panel_.sidebar, dt);
    const bool mouse_over_icon =
        ui_context_.mouse_in_rect(panel_.icon.x, panel_.icon.y, panel_.icon.size, panel_.icon.size);
    const bool mouse_over_sidebar =
        panel_.sidebar.animation > 0.05f &&
        ui_context_.mouse_in_rect(panel_.sidebar.x, panel_.sidebar.y, panel_.sidebar.width,
                                  panel_.sidebar.height);
    ui_context_.set_captures_mouse(mouse_over_icon || mouse_over_sidebar || panel_.icon.dragging ||
                                   panel_.sidebar.dragging);
    if (mouse_over_sidebar && ui_context_.scroll_delta() != 0.0f)
        panel_.sidebar.scroll_y -= ui_context_.scroll_delta() * 32.0f;
    if (panel_.sidebar.animation > 0.001f)
    {
        ui::MenuState menu;
        menu.sidebar = panel_.sidebar;
        menu.fps_limit = panel_.fps_limit;
        menu.locked = panel_.locked;
        menu.muted = panel_.muted;
        if (auto* p = painter())
            ui::build_sidebar(ui_context_, *p, controller_.catalog(), controller_, menu,
                              output_height_);
        panel_.sidebar = menu.sidebar;
        panel_.fps_limit = menu.fps_limit;
        panel_.locked = menu.locked;
        const bool muted_changed = panel_.muted != menu.muted;
        panel_.muted = menu.muted;
        if (muted_changed)
            set_muted(panel_.muted);
    }
    if ((panel_.icon.moved || panel_.sidebar.moved || panel_.icon.position_reset ||
         panel_.sidebar.position_reset) &&
        !panel_.icon.dragging && !panel_.sidebar.dragging)
    {
        controller_.save_ui_layout({panel_.icon.x, panel_.icon.y},
                                   {panel_.sidebar.x, panel_.sidebar.y}, panel_.sidebar.is_free);
        panel_.icon.moved = panel_.sidebar.moved = false;
        panel_.icon.position_reset = panel_.sidebar.position_reset = false;
    }
    ui_context_.end_frame();
    painter()->end_frame();
}

nekomote_error WallpaperInstance::pointer_move(double x, double y,
                                               application::WallpaperController::PointerTime now)
{
    if (!initialized_)
        return NEKOMOTE_OK;
    const float px = static_cast<float>(x);
    const float py = static_cast<float>(y);
    ui_context_.mouse_move(x, y);
    if (ui_captures_mouse())
    {
        controller_.pointer_leave();
        return NEKOMOTE_OK;
    }
    const float cx = px - static_cast<float>(output_width_) * .5f;
    const float cy = py - static_cast<float>(output_height_) * .5f;
    if (panel_.locked)
        controller_.cancel_pointer_gesture();
    controller_.pointer_move({cx, cy}, now);
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::pointer_enter(double x, double y)
{
    if (!initialized_)
        return NEKOMOTE_OK;
    ui_context_.mouse_move(x, y);
    if (ui_captures_mouse())
        return NEKOMOTE_OK;
    const float cx = static_cast<float>(x) - static_cast<float>(output_width_) * .5f;
    const float cy = static_cast<float>(y) - static_cast<float>(output_height_) * .5f;
    controller_.pointer_enter({cx, cy});
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::pointer_leave()
{
    if (!initialized_)
        return NEKOMOTE_OK;
    controller_.pointer_leave();
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::touch(float x, float y)
{
    if (!initialized_ || paused_ || minimized_ || panel_.locked)
        return NEKOMOTE_OK;
    ui_context_.mouse_move(x, y);
    if (ui_captures_mouse())
        return NEKOMOTE_OK;
    const float cx = x - static_cast<float>(output_width_) * .5f;
    const float cy = y - static_cast<float>(output_height_) * .5f;
    controller_.touch({cx, cy});
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::mouse_button(int button, int action,
                                               application::WallpaperController::PointerTime now)
{
    if (!initialized_)
        return NEKOMOTE_OK;
    ui_context_.mouse_button(button, action);
    if (ui_captures_mouse() || panel_.locked || paused_ || minimized_)
    {
        controller_.cancel_pointer_gesture();
        return NEKOMOTE_OK;
    }
    if (button == 0)
    {
        const Vec2 position{ui_context_.mouse_x() - static_cast<float>(output_width_) * .5f,
                            ui_context_.mouse_y() - static_cast<float>(output_height_) * .5f};
        if (action == 1)
            controller_.pointer_press(position, now);
        else
            controller_.pointer_release(position, now);
    }
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::mouse_scroll(double amount)
{
    if (!initialized_)
        return NEKOMOTE_OK;
    ui_context_.mouse_scroll(amount);
    if (ui_captures_mouse())
        return NEKOMOTE_OK;
    if (panel_.locked)
        return NEKOMOTE_OK;
    controller_.zoom_at({ui_context_.mouse_x() - static_cast<float>(output_width_) * .5f,
                         ui_context_.mouse_y() - static_cast<float>(output_height_) * .5f},
                        amount);
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::key(int key, int action)
{
    if (action != 1 || !initialized_)
        return NEKOMOTE_OK;
    switch (key)
    {
        case NEKOMOTE_KEY_SPACE:
            paused_ = !paused_;
            controller_.set_paused(paused_);
            break;
        case NEKOMOTE_KEY_F:
            if (output_width_ > 0 && output_height_ > 0)
                controller_.fit_to_output(output_width_, output_height_);
            break;
        case NEKOMOTE_KEY_R:
            controller_.reset();
            break;
        case NEKOMOTE_KEY_TAB:
        {
            const int count = static_cast<int>(controller_.catalog().characters.size());
            if (count > 0)
                controller_.request_switch_character((controller_.current_character_index() + 1) %
                                                     count);
            break;
        }
        case NEKOMOTE_KEY_B:
        case NEKOMOTE_KEY_N:
        {
            const int count = static_cast<int>(controller_.catalog().backgrounds.size());
            if (count > 0)
            {
                int cur = controller_.current_background_index();
                controller_.request_switch_background(cur < 0 ? 0 : (cur + 1) % count);
            }
            break;
        }
        case NEKOMOTE_KEY_C:
            controller_.request_clear_background();
            break;
        case NEKOMOTE_KEY_DIGIT_1:
        case NEKOMOTE_KEY_DIGIT_2:
        case NEKOMOTE_KEY_DIGIT_3:
        case NEKOMOTE_KEY_DIGIT_4:
        case NEKOMOTE_KEY_DIGIT_5:
        case NEKOMOTE_KEY_DIGIT_6:
        case NEKOMOTE_KEY_DIGIT_7:
        case NEKOMOTE_KEY_DIGIT_8:
        case NEKOMOTE_KEY_DIGIT_9:
        {
            const int index = key - NEKOMOTE_KEY_DIGIT_1;
            const int count = static_cast<int>(controller_.catalog().characters.size());
            if (index < count)
                controller_.request_switch_character(index);
            break;
        }
    }
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::switch_character(int index)
{
    controller_.request_switch_character(index);
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::switch_background(int index)
{
    controller_.request_switch_background(index);
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::clear_background()
{
    controller_.request_clear_background();
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::fit()
{
    if (output_width_ > 0 && output_height_ > 0)
        controller_.fit_to_output(output_width_, output_height_);
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::reset()
{
    controller_.reset();
    return NEKOMOTE_OK;
}

int WallpaperInstance::character_count() const
{
    return static_cast<int>(controller_.catalog().characters.size());
}

const char* WallpaperInstance::character_name(int index) const
{
    const auto& chars = controller_.catalog().characters;
    if (index < 0 || index >= static_cast<int>(chars.size()))
        return nullptr;
    name_buffer_ = chars[index].name;
    return name_buffer_.c_str();
}

int WallpaperInstance::background_count() const
{
    return static_cast<int>(controller_.catalog().backgrounds.size());
}

const char* WallpaperInstance::background_name(int index) const
{
    const auto& bgs = controller_.catalog().backgrounds;
    if (index < 0 || index >= static_cast<int>(bgs.size()))
        return nullptr;
    name_buffer_ = bgs[index].name;
    return name_buffer_.c_str();
}

int WallpaperInstance::current_character() const
{
    return controller_.current_character_index();
}

int WallpaperInstance::current_background() const
{
    return controller_.current_background_index();
}

const char* WallpaperInstance::current_character_name() const
{
    name_buffer_ = controller_.current_character_name();
    return name_buffer_.c_str();
}

bool WallpaperInstance::has_background() const
{
    return controller_.has_background();
}

bool WallpaperInstance::paused() const
{
    return paused_;
}

int WallpaperInstance::fps_limit() const
{
    return controller_.config().fps;
}

float WallpaperInstance::fade_alpha() const
{
    return controller_.fade_alpha();
}

int WallpaperInstance::audio_state() const
{
    if (!audio_device_)
        return 0;
    switch (audio_device_->snapshot().state)
    {
        case audio::DeviceState::Connecting:
            return 1;
        case audio::DeviceState::Ready:
            return 2;
        case audio::DeviceState::Playing:
            return 3;
        case audio::DeviceState::Paused:
            return 4;
        case audio::DeviceState::Failed:
            return 5;
    }
    return 0;
}

float WallpaperInstance::audio_rms() const
{
    if (!audio_device_)
        return 0.0f;
    return audio_device_->snapshot().rms;
}

int WallpaperInstance::audio_error() const
{
    if (!audio_device_)
        return 0;
    return audio_device_->snapshot().error;
}

std::string WallpaperInstance::last_error() const
{
    return error_string_;
}

} // namespace nekomote::abi
