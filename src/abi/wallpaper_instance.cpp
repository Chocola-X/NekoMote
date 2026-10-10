#include "wallpaper_instance.hpp"
#include "abi_internal.hpp"

#include <nekomote/asset_io/image.hpp>
#include <nekomote/abi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>

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

WallpaperInstance::~WallpaperInstance() = default;

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
    controller_.set_config_path(dir / "config.json");
    controller_.load_config(dir / "config.json");
    auto& config = controller_.config();
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
    panel_.muted = value;
    controller_cfg_.mute = value;
    controller_cfg_.audio =
        value ? application::AudioMode::Disabled : application::AudioMode::Device;
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::set_seed(std::uint32_t seed)
{
    controller_cfg_.seed = seed;
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::set_output_size(int width, int height)
{
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

void WallpaperInstance::set_render_target_fbo(std::uint32_t fbo)
{
    render_target_fbo_ = fbo;
}

void WallpaperInstance::reset_gl_resources()
{
    renderer_.reset();
    previous_renderer_.reset();
    background_renderer_.reset();
    ui_renderer_.reset();
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
    controller_.install_initial(std::move(candidate));
    if (!controller_.config().background.empty())
    {
        const auto* bg = catalog.find_background(controller_.config().background);
        if (bg)
        {
            int bg_index = static_cast<int>(bg - catalog.backgrounds.data());
            apply_background(bg->path, 0, bg_index);
        }
    }
    if (output_width_ > 0 && output_height_ > 0)
    {
        controller_.prepare_frame();
        if (controller_.config().fit)
            controller_.fit_to_output(output_width_, output_height_);
    }
    if (output_width_ > 0 && output_height_ > 0 && ui_renderer_.init(output_width_, output_height_) &&
        ui_renderer_.init_font("", 36))
    {
        ui_context_.init(&ui_renderer_);
        const auto icon_path = assets_dir_ / "icons" / "neko.png";
        if (std::filesystem::exists(icon_path))
        {
            auto image = asset_io::load_png(icon_path);
            if (image.ok)
                panel_.icon_texture = ui_renderer_.upload_icon(image.image);
        }
        panel_.icon.x = static_cast<float>(output_width_) - 60;
        panel_.icon.y = 16;
        panel_.ui_ready = true;
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
    controller_.install_initial(std::move(candidate));
    if (output_width_ > 0 && output_height_ > 0)
        controller_.prepare_frame();
    initialized_ = true;
    return NEKOMOTE_OK;
}

void WallpaperInstance::finish_candidate_gpu(const application::CandidateCpu& candidate)
{
    renderer_ = std::make_unique<render_gl::Renderer>(loader_);
    const auto& scene = candidate.program->playback().scene();
    const auto capacity = scene.frame_capacity();
    renderer_->reserve({capacity.vertices, capacity.indices, capacity.commands,
                        scene.textures().size(), capacity.mask_commands, capacity.masks});
    for (const auto& [id, image] : candidate.textures)
        renderer_->upload_texture(id, image);
    background_renderer_ = std::make_unique<render_gl::Renderer>(loader_);
    background_renderer_->reserve({8, 12, 2, 2});
    last_render_width_ = 0;
    last_render_height_ = 0;
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
        background_renderer_ = std::make_unique<render_gl::Renderer>(loader_);
        background_renderer_->reserve({8, 12, 2, 2});
        const TextureId id{1};
        background_renderer_->upload_texture(id, new_image.image);
        controller_.confirm_background(bg_index, id, id, new_image.image.width,
                                        new_image.image.height, 0);
        current_background_id_ = id;
        current_background_path_ = path;
        return;
    }
    auto old_image = asset_io::load_png(current_background_path_);
    if (!old_image.ok)
    {
        error_string_ = old_image.error;
        return;
    }
    background_renderer_ = std::make_unique<render_gl::Renderer>(loader_);
    background_renderer_->reserve({8, 12, 2, 2});
    const TextureId old_id{1}, new_id{2};
    background_renderer_->upload_texture(old_id, old_image.image);
    background_renderer_->upload_texture(new_id, new_image.image);
    controller_.confirm_background(bg_index, old_id, new_id, new_image.image.width,
                                   new_image.image.height, fade_duration);
    current_background_id_ = new_id;
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
        std::filesystem::path bg_path;
        int bg_index = controller_.current_background_index();
        if (bg_index >= 0 &&
            bg_index < static_cast<int>(controller_.catalog().backgrounds.size()))
            bg_path = controller_.catalog().backgrounds[bg_index].path;
        previous_renderer_ = std::move(renderer_);
        finish_candidate_gpu(*candidate_opt);
        controller_.confirm_candidate(std::move(*candidate_opt));
        if (!bg_path.empty())
            apply_background(bg_path, 0, bg_index);
        if (controller_.config().fit && output_width_ > 0 && output_height_ > 0)
        {
            controller_.prepare_frame();
            controller_.fit_to_output(output_width_, output_height_);
        }
        if (output_width_ > 0 && output_height_ > 0)
        {
            controller_.set_output_size(output_width_, output_height_);
            controller_.prepare_frame();
            renderer_->prepare(controller_.current_instance()->frame());
            renderer_->resize_output(output_width_, output_height_);
            if (controller_.has_background())
            {
                background_renderer_->prepare(
                    controller_.current_instance()->background_frame());
                background_renderer_->resize_output(output_width_, output_height_);
            }
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
        controller_.confirm_background_clear();
}

nekomote_error WallpaperInstance::advance(double dt)
{
    if (!initialized_)
        return NEKOMOTE_OK;
    handle_switch_poll();
    controller_.update_fade();
    if (controller_.switch_state() == application::SwitchState::Idle && previous_renderer_)
        previous_renderer_.reset();
    handle_background_poll();
    if (output_width_ <= 0 || output_height_ <= 0 || minimized_)
        return NEKOMOTE_OK;
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
    controller_.prepare_frame();
    auto status = controller_.last_error();
    if (!status)
    {
        error_string_ = "prepare_frame failed";
        return ok_or_set(status);
    }
    renderer_->prepare(controller_.current_instance()->frame());
    const bool in_crossfade = controller_.switch_state() ==
                                  application::SwitchState::CrossFade &&
                              controller_.previous_instance();
    if (in_crossfade)
    {
        controller_.previous_instance()->prepare_frame();
        previous_renderer_->prepare(controller_.previous_instance()->frame());
    }
    const int w = output_width_;
    const int h = output_height_;
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
    const std::array<float, 4> clear = {0.06f, 0.07f, 0.10f, 1.0f};
    const render_gl::Target target{render_target_fbo_, w, h};
    RenderFrame bg_frame{};
    if (controller_.has_background())
        bg_frame = controller_.current_instance()->background_frame();
    if (!bg_frame.commands.empty())
    {
        background_renderer_->prepare(bg_frame);
        background_renderer_->render(target, {0, 0, 1, clear});
    }
    const auto target_contents = !bg_frame.commands.empty()
                                     ? render_gl::TargetContents::Preserve
                                     : render_gl::TargetContents::Clear;
    const float fade_alpha = controller_.fade_alpha();
    if (in_crossfade)
    {
        previous_renderer_->render(
            target,
            {static_cast<float>(w) * .5f, static_cast<float>(h) * .5f, 1, clear,
             1.0f - fade_alpha, controller_.previous_program()->mirror()},
            target_contents);
        renderer_->render(target,
                          {static_cast<float>(w) * .5f, static_cast<float>(h) * .5f, 1, clear,
                           fade_alpha, controller_.mirror()},
                          render_gl::TargetContents::Preserve);
    }
    else
    {
        renderer_->render(
            target,
            {static_cast<float>(w) * .5f, static_cast<float>(h) * .5f, 1, clear,
             controller_.switch_state() == application::SwitchState::Idle ? 1.0f : fade_alpha,
             controller_.mirror()},
            target_contents);
    }
    return NEKOMOTE_OK;
}

void WallpaperInstance::render_ui(float dt)
{
    draw_ui(dt);
}

void WallpaperInstance::draw_ui(float dt)
{
    if (!panel_.ui_ready)
        return;
    ui_context_.begin_frame();
    const ui::Style style;
    if (ui_context_.toggle_icon(panel_.icon, panel_.icon_texture))
        panel_.sidebar.open = !panel_.sidebar.open;
    ui_context_.sidebar(panel_.sidebar, style, dt);
    const bool mouse_over_icon =
        ui_context_.mouse_in_rect(panel_.icon.x, panel_.icon.y, panel_.icon.size, panel_.icon.size);
    const bool mouse_over_sidebar =
        panel_.sidebar.animation > 0.05f &&
        ui_context_.mouse_in_rect(panel_.sidebar.x, panel_.sidebar.y, panel_.sidebar.width,
                                  panel_.sidebar.height);
    ui_context_.set_captures_mouse(mouse_over_icon || mouse_over_sidebar ||
                                   panel_.icon.dragging || panel_.sidebar.dragging);
    if (mouse_over_sidebar && ui_context_.scroll_delta() != 0.0f)
        panel_.sidebar.scroll_y -= ui_context_.scroll_delta() * 32.0f;
    if (panel_.sidebar.animation > 0.001f)
    {
        const float sidebar_alpha = std::clamp(panel_.sidebar.animation, 0.0f, 1.0f);
        ui_renderer_.set_global_alpha(sidebar_alpha);
        const float sidebar_x = panel_.sidebar.x;
        const float sidebar_y = panel_.sidebar.y;
        const auto& catalog = controller_.catalog();
        float content_scrollable_h = 0.0f;
        for (std::size_t g = 0; g < catalog.character_groups.size(); ++g)
        {
            content_scrollable_h += 42.0f;
            if (panel_.sidebar.expanded_drawer == static_cast<int>(g))
                content_scrollable_h +=
                    static_cast<float>(catalog.character_groups[g].items.size()) * 34.0f;
        }
        content_scrollable_h += 42.0f;
        if (panel_.sidebar.expanded_drawer == 8)
        {
            content_scrollable_h += 30.0f + 4.0f * 32.0f;
            content_scrollable_h += 30.0f + 2.0f * 34.0f + 34.0f;
            content_scrollable_h += 30.0f + 30.0f + static_cast<float>(catalog.backgrounds.size()) * 32.0f;
        }
        content_scrollable_h += 68.0f;
        const float max_sidebar_h = std::max(64.0f, static_cast<float>(output_height_) - 16.0f);
        const float content_total_h = 44.0f + content_scrollable_h;
        panel_.sidebar.height = std::min(max_sidebar_h, content_total_h);
        const float max_scroll =
            std::max(0.0f, content_scrollable_h - (panel_.sidebar.height - 44.0f));
        panel_.sidebar.scroll_y = std::clamp(panel_.sidebar.scroll_y, 0.0f, max_scroll);
        ui_renderer_.draw_rounded_rect(sidebar_x, sidebar_y, panel_.sidebar.width,
                                       panel_.sidebar.height, style.corner_radius, style.panel_bg,
                                       style.accent, 2.0f);
        ui_context_.sidebar_header(panel_.sidebar, "猫娘乐园角色E-mote图鉴", style);
        const float clip_y = sidebar_y + 44.0f;
        const float clip_h = panel_.sidebar.height - 44.0f;
        ui_renderer_.push_clip_rect(sidebar_x, clip_y, panel_.sidebar.width, clip_h);
        float curr_y = clip_y - panel_.sidebar.scroll_y;
        for (std::size_t g = 0; g < catalog.character_groups.size(); ++g)
        {
            const auto& group = catalog.character_groups[g];
            const bool is_open = (panel_.sidebar.expanded_drawer == static_cast<int>(g));
            if (ui_context_.drawer_header(group.title.c_str(), is_open, sidebar_x, curr_y,
                                          panel_.sidebar.width, 42.0f, style))
                panel_.sidebar.expanded_drawer = is_open ? -1 : static_cast<int>(g);
            curr_y += 42.0f;
            if (is_open)
            {
                for (const auto& item : group.items)
                {
                    const bool is_selected =
                        (item.character_index == controller_.current_character_index());
                    if (ui_context_.drawer_item(item.display_name.c_str(), is_selected, sidebar_x,
                                                curr_y, panel_.sidebar.width, 34.0f, style))
                        controller_.request_switch_character(item.character_index);
                    curr_y += 34.0f;
                }
            }
        }
        const bool is_settings_open = (panel_.sidebar.expanded_drawer == 8);
        if (ui_context_.drawer_header("设置", is_settings_open, sidebar_x, curr_y,
                                     panel_.sidebar.width, 42.0f, style))
            panel_.sidebar.expanded_drawer = is_settings_open ? -1 : 8;
        curr_y += 42.0f;
        if (is_settings_open)
        {
            ui_context_.section_header("帧率限制", sidebar_x, curr_y, panel_.sidebar.width, 30.0f,
                                       style);
            curr_y += 30.0f;
            const struct
            {
                int fps;
                const char* label;
                const char* badge;
            } fps_opts[] = {{0, "无上限", "∞"}, {30, "30 FPS", ""}, {60, "60 FPS", "推荐"},
                            {120, "120 FPS", ""}};
            for (const auto& opt : fps_opts)
            {
                const bool selected = (panel_.fps_limit == opt.fps);
                if (ui_context_.radio_row(opt.label, opt.badge, selected, sidebar_x, curr_y,
                                         panel_.sidebar.width, 32.0f, style))
                {
                    panel_.fps_limit = opt.fps;
                    controller_.config().fps = opt.fps;
                    if (!controller_.no_save())
                        controller_.save_config(controller_.config_path());
                }
                curr_y += 32.0f;
            }
            ui_context_.section_header("角色", sidebar_x, curr_y, panel_.sidebar.width, 30.0f,
                                       style);
            curr_y += 30.0f;
            ui_context_.switch_row("锁定人物位置与大小", panel_.locked, sidebar_x, curr_y,
                                   panel_.sidebar.width, 34.0f, style);
            curr_y += 34.0f;
            bool is_muted = panel_.muted;
            if (ui_context_.switch_row("关闭声音", is_muted, sidebar_x, curr_y, panel_.sidebar.width,
                                       34.0f, style))
            {
                panel_.muted = is_muted;
                controller_.config().mute = is_muted;
                if (!controller_.no_save())
                    controller_.save_config(controller_.config_path());
            }
            curr_y += 34.0f;
            if (ui_context_.reset_button("重设角色位置", sidebar_x, curr_y, panel_.sidebar.width,
                                         34.0f, style))
            {
                if (output_width_ > 0 && output_height_ > 0)
                    controller_.fit_to_output(output_width_, output_height_);
            }
            curr_y += 34.0f;
            ui_context_.section_header("壁纸选择", sidebar_x, curr_y, panel_.sidebar.width, 30.0f,
                                       style);
            curr_y += 30.0f;
            ui_context_.section_header("固定背景", sidebar_x, curr_y, panel_.sidebar.width, 30.0f,
                                       style);
            curr_y += 30.0f;
            for (std::size_t b = 0; b < catalog.backgrounds.size(); ++b)
            {
                const auto& bg = catalog.backgrounds[b];
                const bool selected =
                    (static_cast<int>(b) == controller_.current_background_index());
                if (ui_context_.radio_row(bg.display_name.c_str(), "", selected, sidebar_x, curr_y,
                                         panel_.sidebar.width, 32.0f, style))
                    controller_.request_switch_background(static_cast<int>(b));
                curr_y += 32.0f;
            }
        }
        ui_context_.divider_line(sidebar_x, curr_y, panel_.sidebar.width, style);
        curr_y += 10.0f;
        const char* author_txt = "作者：GTX690战术核显卡导弹";
        const float atw =
            ui_renderer_.font() ? ui_renderer_.font()->text_width(author_txt, 12.0f) : 160.0f;
        ui_context_.label(author_txt, sidebar_x + (panel_.sidebar.width - atw) * 0.5f, curr_y, 12.0f,
                          style.text);
        curr_y += 24.0f;
        ui_context_.divider_line(sidebar_x, curr_y, panel_.sidebar.width, style);
        curr_y += 10.0f;
        const char* gh_txt = "Github项目：NekoWebShow";
        const float gtw =
            ui_renderer_.font() ? ui_renderer_.font()->text_width(gh_txt, 12.0f) : 170.0f;
        ui_context_.label(gh_txt, sidebar_x + (panel_.sidebar.width - gtw) * 0.5f, curr_y, 12.0f,
                          style.text);
        curr_y += 24.0f;
        ui_renderer_.pop_clip_rect();
        ui_context_.draw_scrollbar(sidebar_x, clip_y, panel_.sidebar.width, clip_h,
                                   content_scrollable_h, panel_.sidebar.scroll_y, style);
        ui_renderer_.set_global_alpha(1.0f);
    }
    ui_context_.end_frame();
    ui_renderer_.end_frame();
}

nekomote_error WallpaperInstance::pointer_move(double x, double y)
{
    if (!initialized_)
        return NEKOMOTE_OK;
    const float px = static_cast<float>(x);
    const float py = static_cast<float>(y);
    ui_context_.mouse_move(x, y);
    const float cx = px - static_cast<float>(output_width_) * .5f;
    const float cy = py - static_cast<float>(output_height_) * .5f;
    if (dragging_ && !panel_.locked)
    {
        auto origin = controller_.origin();
        origin.x += cx - prev_mouse_x_;
        origin.y += cy - prev_mouse_y_;
        controller_.set_origin(origin);
    }
    prev_mouse_x_ = cx;
    prev_mouse_y_ = cy;
    controller_.pointer_move({cx, cy});
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::pointer_enter(double x, double y)
{
    if (!initialized_)
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
    dragging_ = false;
    controller_.pointer_leave();
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::touch(float x, float y)
{
    if (!initialized_ || paused_ || minimized_ || panel_.locked)
        return NEKOMOTE_OK;
    if (panel_.ui_ready && ui_context_.captures_mouse())
        return NEKOMOTE_OK;
    const float cx = x - static_cast<float>(output_width_) * .5f;
    const float cy = y - static_cast<float>(output_height_) * .5f;
    controller_.touch({cx, cy});
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::mouse_button(int button, int action)
{
    if (!initialized_)
        return NEKOMOTE_OK;
    ui_context_.mouse_button(button, action);
    if (panel_.ui_ready && ui_context_.captures_mouse())
        return NEKOMOTE_OK;
    if (button == 1 && !panel_.locked)
    {
        dragging_ = (action == 1);
        prev_mouse_x_ = ui_context_.mouse_x();
        prev_mouse_y_ = ui_context_.mouse_y();
    }
    return NEKOMOTE_OK;
}

nekomote_error WallpaperInstance::mouse_scroll(double amount)
{
    if (!initialized_)
        return NEKOMOTE_OK;
    ui_context_.mouse_scroll(amount);
    if (panel_.ui_ready && ui_context_.captures_mouse())
        return NEKOMOTE_OK;
    if (panel_.locked)
        return NEKOMOTE_OK;
    auto scale = controller_.scale();
    auto origin = controller_.origin();
    const auto new_scale =
        std::clamp(scale * static_cast<float>(std::pow(1.1, amount)), .05f, 5.0f);
    const auto ratio = new_scale / scale;
    const float px = ui_context_.mouse_x();
    const float py = ui_context_.mouse_y();
    origin = {px + (origin.x - px) * ratio, py + (origin.y - py) * ratio};
    controller_.set_scale(new_scale);
    controller_.set_origin(origin);
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
                controller_.request_switch_character(
                    (controller_.current_character_index() + 1) % count);
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

float WallpaperInstance::fade_alpha() const
{
    return controller_.fade_alpha();
}

std::string WallpaperInstance::last_error() const
{
    return error_string_;
}

} // namespace nekomote::abi
