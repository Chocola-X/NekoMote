#include <nekomote/application/controller.hpp>
#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/audio.hpp>
#include <nekomote/asset_io/image.hpp>
#include <nekomote/asset_io/session_asset.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <stdexcept>

namespace nekomote::application
{
namespace
{
using Clock = std::chrono::steady_clock;

int minutes_at(std::chrono::system_clock::time_point now)
{
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto* tmp = std::localtime(&t);
    if (!tmp)
        return 0;
    return tmp->tm_hour * 60 + tmp->tm_min;
}

int phase_at(const BackgroundEntry& bg, std::chrono::system_clock::time_point now)
{
    return static_cast<int>(bg.phase_index_at(minutes_at(now)));
}
} // namespace

WallpaperController::WallpaperController() = default;
WallpaperController::~WallpaperController() = default;

bool WallpaperController::scan_assets(const fs::path& assets_dir)
{
    catalog_ = AssetCatalog::scan(assets_dir);
    return !catalog_.characters.empty();
}

const AssetCatalog& WallpaperController::catalog() const noexcept
{
    return catalog_;
}

void WallpaperController::load_config(const fs::path& path)
{
    config_path_ = path;
    config_ = WallpaperConfig::load(path);
}

void WallpaperController::save_config(const fs::path& path) const
{
    config_.save(path);
}

WallpaperConfig& WallpaperController::config() noexcept
{
    return config_;
}

const WallpaperConfig& WallpaperController::config() const noexcept
{
    return config_;
}

fs::path WallpaperController::config_path() const noexcept
{
    return config_path_;
}

void WallpaperController::set_config_path(const fs::path& path) noexcept
{
    config_path_ = path;
}

bool WallpaperController::no_save() const noexcept
{
    return no_save_;
}

void WallpaperController::set_no_save(bool value) noexcept
{
    no_save_ = value;
}

SessionInstance* WallpaperController::current_instance() noexcept
{
    return current_instance_.get();
}

const SessionProgram* WallpaperController::current_program() const noexcept
{
    return current_program_.get();
}

SessionInstance* WallpaperController::previous_instance() noexcept
{
    return previous_instance_.get();
}

const SessionProgram* WallpaperController::previous_program() const noexcept
{
    return previous_program_.get();
}

void WallpaperController::apply_controller_config(const ControllerConfig& cfg)
{
    controller_config_ = cfg;
    initial_origin_ = cfg.origin;
    initial_scale_ = cfg.scale;
}

void WallpaperController::accept(SessionStatus status)
{
    if (!status && status.error != SessionError::Busy &&
        status.error != SessionError::InputUnavailable && last_error_)
        last_error_ = status;
}

void WallpaperController::synchronize_pause()
{
    if (paused_ || minimized_ || output_unavailable_)
        cancel_pointer_gesture();
    if (current_instance_)
        accept(current_instance_->set_playback_state(paused_ || minimized_ || output_unavailable_
                                                         ? runtime::PlaybackState::Paused
                                                         : runtime::PlaybackState::Running));
}

CandidateCpu WallpaperController::load_candidate_cpu(const CharacterEntry& entry,
                                                     const ControllerConfig& cfg)
{
    CandidateCpu result;
    try
    {
        result.name = entry.name;
        result.model_dir = entry.directory;
        result.cutoff_y = entry.cutoff_y;
        result.has_cutoff = entry.has_cutoff;
        auto model = asset_io::load_asset(entry.model_path);
        if (!model.ok)
            throw std::runtime_error(model.errors.front());
        auto definition = asset_io::load_session_asset(entry.session_path);
        if (!definition.ok)
            throw std::runtime_error(definition.errors.front());
        auto compiled = compile_session(model.asset, definition.asset);
        if (!compiled.program)
            throw std::runtime_error(compiled.errors.front());
        result.program = std::move(compiled.program);
        const auto& program = *result.program;
        result.instance = std::make_unique<SessionInstance>(
            program, SessionConfig{{cfg.seed}, cfg.seed, cfg.pointer, cfg.audio});
        accept(result.instance->set_origin(cfg.origin));
        accept(result.instance->set_scale(cfg.scale));
        accept(result.instance->start());
        accept(result.instance->prepare_frame());
        if (cfg.audio == AudioMode::Device)
        {
            for (const auto& path : program.audio_paths())
            {
                auto clip = asset_io::load_wave(entry.session_path.parent_path() / path);
                if (!clip.ok)
                    throw std::runtime_error(clip.error);
                result.audio_clips.push_back(std::move(clip.clip));
            }
        }
        const auto& scene = program.playback().scene();
        for (const auto& texture : scene.textures())
        {
            auto image = asset_io::load_png(entry.model_path.parent_path() / texture.path);
            if (!image.ok)
                throw std::runtime_error(image.error);
            result.textures.emplace_back(texture.id, std::move(image.image));
        }
        result.ok = true;
    }
    catch (const std::exception& error)
    {
        result.error = error.what();
    }
    return result;
}

CandidateCpu WallpaperController::load_candidate(const CharacterEntry& entry,
                                                 const ControllerConfig& cfg)
{
    return load_candidate_cpu(entry, cfg);
}

void WallpaperController::install_candidate_sync(CandidateCpu candidate)
{
    if (!candidate.ok)
    {
        std::fprintf(stderr, "Cannot switch: %s; current session retained\n",
                     candidate.error.c_str());
        return;
    }
    cancel_pointer_gesture();
    previous_program_ = std::move(current_program_);
    previous_instance_ = std::move(current_instance_);
    current_program_ = std::move(candidate.program);
    current_instance_ = std::move(candidate.instance);
    current_name_ = std::move(candidate.name);
    current_model_dir_ = std::move(candidate.model_dir);
    current_cutoff_y_ = candidate.cutoff_y;
    current_has_cutoff_ = candidate.has_cutoff;
    current_has_background_ = false;
    current_background_index_ = -1;
    controller_config_.origin = initial_origin_;
    controller_config_.scale = initial_scale_;
    if (current_instance_)
    {
        accept(current_instance_->set_origin(initial_origin_));
        accept(current_instance_->set_scale(initial_scale_));
    }
    for (std::size_t i = 0; i < catalog_.characters.size(); ++i)
        if (catalog_.characters[i].name == current_name_)
            current_character_index_ = static_cast<int>(i);
}

void WallpaperController::clear_previous() noexcept
{
    previous_program_.reset();
    previous_instance_.reset();
}

CandidateCpu WallpaperController::load_initial(int character_index, const ControllerConfig& cfg)
{
    apply_controller_config(cfg);
    if (character_index < 0 || character_index >= static_cast<int>(catalog_.characters.size()))
        return {};
    return load_candidate_cpu(catalog_.characters[character_index], cfg);
}

CandidateCpu WallpaperController::load_initial(const CharacterEntry& entry,
                                               const ControllerConfig& cfg)
{
    apply_controller_config(cfg);
    return load_candidate_cpu(entry, cfg);
}

void WallpaperController::install_initial(CandidateCpu candidate)
{
    if (!candidate.ok)
        throw std::runtime_error(candidate.error);
    cancel_pointer_gesture();
    current_program_ = std::move(candidate.program);
    current_instance_ = std::move(candidate.instance);
    current_name_ = std::move(candidate.name);
    current_model_dir_ = std::move(candidate.model_dir);
    current_cutoff_y_ = candidate.cutoff_y;
    current_has_cutoff_ = candidate.has_cutoff;
    for (std::size_t i = 0; i < catalog_.characters.size(); ++i)
        if (catalog_.characters[i].name == current_name_)
            current_character_index_ = static_cast<int>(i);
}

void WallpaperController::request_switch_character(int index)
{
    if (index < 0 || index >= static_cast<int>(catalog_.characters.size()))
        return;
    if (index == current_character_index_ || switch_state_ != SwitchState::Idle)
        return;
    fade_target_character_ = index;
    switch_state_ = SwitchState::Pending;
    const auto& entry = catalog_.characters[index];
    controller_config_.origin = initial_origin_;
    controller_config_.scale = initial_scale_;
    const auto cfg = controller_config_;
    candidate_future_ = std::async(std::launch::async,
                                   [this, entry, cfg]() { return load_candidate_cpu(entry, cfg); });
}

std::optional<CandidateCpu> WallpaperController::poll_candidate()
{
    if (switch_state_ != SwitchState::Pending)
        return std::nullopt;
    if (!candidate_future_.valid())
        return std::nullopt;
    if (candidate_future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return std::nullopt;
    return candidate_future_.get();
}

void WallpaperController::confirm_candidate(CandidateCpu candidate)
{
    if (!candidate.ok)
    {
        reject_candidate(candidate.error);
        return;
    }
    cancel_pointer_gesture();
    previous_program_ = std::move(current_program_);
    previous_instance_ = std::move(current_instance_);
    current_program_ = std::move(candidate.program);
    current_instance_ = std::move(candidate.instance);
    current_name_ = std::move(candidate.name);
    current_model_dir_ = std::move(candidate.model_dir);
    current_cutoff_y_ = candidate.cutoff_y;
    current_has_cutoff_ = candidate.has_cutoff;
    current_has_background_ = false;
    current_background_index_ = -1;
    current_character_index_ = fade_target_character_;
    config_.character = current_name_;
    controller_config_.origin = initial_origin_;
    controller_config_.scale = initial_scale_;
    if (current_instance_)
    {
        accept(current_instance_->set_origin(initial_origin_));
        accept(current_instance_->set_scale(initial_scale_));
    }
    if (!no_save_ && !config_path_.empty())
        config_.save(config_path_);
    switch_state_ = SwitchState::CrossFade;
    fade_alpha_ = 0.0f;
    fade_start_ = Clock::now();
}

void WallpaperController::reject_candidate(std::string error)
{
    std::fprintf(stderr, "Cannot switch to %s: %s; current session retained\n",
                 fade_target_character_ >= 0 &&
                         fade_target_character_ < static_cast<int>(catalog_.characters.size())
                     ? catalog_.characters[fade_target_character_].name.c_str()
                     : "?",
                 error.c_str());
    switch_state_ = SwitchState::Idle;
    fade_target_character_ = current_character_index_;
    fade_alpha_ = 1.0f;
}

SwitchState WallpaperController::switch_state() const noexcept
{
    return switch_state_;
}

float WallpaperController::fade_alpha() const noexcept
{
    return fade_alpha_;
}

int WallpaperController::fade_target_character() const noexcept
{
    return fade_target_character_;
}

void WallpaperController::request_switch_background(int index)
{
    if (index < 0 || index >= static_cast<int>(catalog_.backgrounds.size()))
        return;
    target_background_index_ = index;
    if (catalog_.backgrounds[index].type == BackgroundType::Dynamic)
        current_bg_phase_ = phase_at(catalog_.backgrounds[index], std::chrono::system_clock::now());
    need_bg_switch_ = true;
}

void WallpaperController::request_clear_background()
{
    need_bg_clear_ = true;
}

std::optional<BackgroundRequest> WallpaperController::poll_background()
{
    if (need_bg_switch_)
    {
        need_bg_switch_ = false;
        if (target_background_index_ >= 0 &&
            target_background_index_ < static_cast<int>(catalog_.backgrounds.size()))
        {
            const auto& bg = catalog_.backgrounds[target_background_index_];
            const auto path = bg.type == BackgroundType::Dynamic
                                  ? bg.path_for_phase(static_cast<std::size_t>(current_bg_phase_))
                                  : bg.path();
            return BackgroundRequest{BackgroundAction::Switch, target_background_index_, path,
                                     kBackgroundFadeSeconds};
        }
    }
    if (need_bg_clear_)
    {
        need_bg_clear_ = false;
        return BackgroundRequest{BackgroundAction::Clear, -1, {}, 0};
    }
    return std::nullopt;
}

void WallpaperController::confirm_background(int background_index, TextureId old_id,
                                             TextureId new_id, int width, int height,
                                             double fade_duration)
{
    if (!current_instance_)
        return;
    if (current_has_background_ && fade_duration > 0)
    {
        accept(current_instance_->set_background(old_id, width, height));
        accept(current_instance_->fade_background(new_id, width, height, fade_duration));
    }
    else
    {
        accept(current_instance_->set_background(new_id, width, height));
    }
    current_has_background_ = true;
    current_background_index_ = background_index;
    if (background_index >= 0 && background_index < static_cast<int>(catalog_.backgrounds.size()))
    {
        config_.background = catalog_.backgrounds[background_index].name;
        if (catalog_.backgrounds[background_index].type == BackgroundType::Dynamic)
            current_bg_phase_ =
                phase_at(catalog_.backgrounds[background_index], std::chrono::system_clock::now());
    }
    if (!no_save_ && !config_path_.empty())
        config_.save(config_path_);
}

void WallpaperController::confirm_background_clear()
{
    if (current_instance_)
        current_instance_->clear_background();
    current_has_background_ = false;
    current_background_index_ = -1;
    config_.background.clear();
    if (!no_save_ && !config_path_.empty())
        config_.save(config_path_);
}

bool WallpaperController::has_background() const noexcept
{
    return current_has_background_;
}

int WallpaperController::background_phase() const noexcept
{
    return current_bg_phase_;
}

std::filesystem::path WallpaperController::background_path(int index) const noexcept
{
    if (index < 0 || index >= static_cast<int>(catalog_.backgrounds.size()))
        return {};
    const auto& bg = catalog_.backgrounds[index];
    if (bg.type == BackgroundType::Dynamic)
        return bg.path_for_phase(
            static_cast<std::size_t>(phase_at(bg, std::chrono::system_clock::now())));
    return bg.path();
}

void WallpaperController::update_background_phase()
{
    if (!current_has_background_ || current_background_index_ < 0)
        return;
    if (current_background_index_ >= static_cast<int>(catalog_.backgrounds.size()))
        return;
    const auto& bg = catalog_.backgrounds[current_background_index_];
    if (bg.type != BackgroundType::Dynamic)
        return;
    const auto new_phase = phase_at(bg, std::chrono::system_clock::now());
    if (new_phase == current_bg_phase_)
        return;
    current_bg_phase_ = new_phase;
    target_background_index_ = current_background_index_;
    need_bg_switch_ = true;
}

void WallpaperController::update_fade()
{
    if (switch_state_ != SwitchState::CrossFade)
        return;
    const auto elapsed = std::chrono::duration<double>(Clock::now() - fade_start_).count();
    fade_alpha_ = static_cast<float>(elapsed / kCharacterFadeSeconds);
    if (fade_alpha_ >= 1.0f)
    {
        fade_alpha_ = 1.0f;
        switch_state_ = SwitchState::Idle;
        previous_program_.reset();
        previous_instance_.reset();
    }
}

void WallpaperController::advance(double elapsed_seconds)
{
    update_background_phase();
    if (current_instance_)
        accept(current_instance_->advance(elapsed_seconds));
    if (switch_state_ == SwitchState::CrossFade && previous_instance_)
        accept(previous_instance_->advance(elapsed_seconds));
}

void WallpaperController::prepare_frame()
{
    if (current_instance_)
        accept(current_instance_->prepare_frame());
    if (switch_state_ == SwitchState::CrossFade && previous_instance_)
        accept(previous_instance_->prepare_frame());
}

void WallpaperController::fit_to_output(int width, int height)
{
    if (!current_instance_ || width <= 32 || height <= 32)
        return;
    const auto frame = current_instance_->frame();
    float left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
    for (const auto& command : frame.commands)
        for (std::uint32_t i = 0; i < command.index_count; ++i)
        {
            const auto& v = frame.vertices[frame.indices[command.first_index + i]];
            if (v.color[3] <= 0)
                continue;
            left = std::min(left, v.x);
            right = std::max(right, v.x);
            top = std::min(top, v.y);
            bottom = std::max(bottom, v.y);
        }
    if (!(right > left) || !(bottom > top))
        return;
    const auto effective_bottom = current_has_cutoff_ ? current_cutoff_y_ : bottom;
    if (!(effective_bottom > top))
        return;
    auto& origin = controller_config_.origin;
    auto& scale = controller_config_.scale;
    if (current_has_cutoff_)
    {
        const auto floor = static_cast<float>(height) + 40.0f;
        const auto ratio = std::min(static_cast<float>(width - 32) / (right - left),
                                    floor / (effective_bottom - top));
        const auto new_scale = std::clamp(scale * ratio / 1.21f, .05f, 5.0f);
        const auto actual_ratio = new_scale / scale;
        origin = {(origin.x - (left + right) * .5f) * actual_ratio,
                  static_cast<float>(height) * .5f - (effective_bottom - top) * new_scale * .5f -
                      100.0f};
        scale = new_scale;
    }
    else
    {
        const auto ratio = std::min(static_cast<float>(width - 32) / (right - left),
                                    static_cast<float>(height - 32) / (bottom - top));
        const auto new_scale = std::clamp(scale * ratio, .05f, 5.0f);
        const auto actual_ratio = new_scale / scale;
        origin = {(origin.x - (left + right) * .5f) * actual_ratio,
                  (origin.y - (top + bottom) * .5f) * actual_ratio};
        scale = new_scale;
    }
    accept(current_instance_->set_scale(scale));
    accept(current_instance_->set_origin(origin));
}

void WallpaperController::set_output_size(int width, int height)
{
    if (current_instance_)
        accept(current_instance_->set_output_size(width, height));
}

void WallpaperController::set_paused(bool paused)
{
    paused_ = paused;
    synchronize_pause();
}

void WallpaperController::set_minimized(bool minimized)
{
    minimized_ = minimized;
    synchronize_pause();
}

void WallpaperController::set_output_unavailable(bool unavailable)
{
    output_unavailable_ = unavailable;
    synchronize_pause();
}

void WallpaperController::set_origin(Vec2 origin)
{
    controller_config_.origin = origin;
    if (current_instance_)
        accept(current_instance_->set_origin(origin));
}

void WallpaperController::set_scale(float scale)
{
    controller_config_.scale = scale;
    if (current_instance_)
        accept(current_instance_->set_scale(scale));
}

Vec2 WallpaperController::origin() const noexcept
{
    return controller_config_.origin;
}

float WallpaperController::scale() const noexcept
{
    return controller_config_.scale;
}

void WallpaperController::pointer_press(Vec2 position, PointerTime now)
{
    (void)now;
    cancel_pointer_gesture();
    if (paused_ || minimized_ || output_unavailable_)
        return;
    pointer_pressed_ = true;
    press_position_ = drag_previous_ = position;
}

void WallpaperController::pointer_move(Vec2 position, PointerTime now)
{
    (void)now;
    if (pointer_pressed_)
    {
        constexpr float kClickMotionTolerance = 4.0f;
        const float dx = position.x - press_position_.x;
        const float dy = position.y - press_position_.y;
        pointer_moved_ =
            pointer_moved_ || dx * dx + dy * dy > kClickMotionTolerance * kClickMotionTolerance;
        if (pointer_moved_)
        {
            const auto old_origin = origin();
            set_origin({old_origin.x + position.x - drag_previous_.x,
                        old_origin.y + position.y - drag_previous_.y});
        }
        drag_previous_ = position;
    }
    if (current_instance_)
        accept(current_instance_->pointer_move(position));
}

void WallpaperController::pointer_release(Vec2 position, PointerTime now)
{
    if (!pointer_pressed_)
        return;
    pointer_move(position, now);
    const bool click = !pointer_moved_;
    cancel_pointer_gesture();
    if (click && !paused_ && !minimized_ && !output_unavailable_)
        touch(position);
}

void WallpaperController::cancel_pointer_gesture() noexcept
{
    pointer_pressed_ = pointer_moved_ = false;
}

void WallpaperController::zoom_at(Vec2 position, double wheel_steps)
{
    constexpr float kMinimumModelScale = .05f;
    constexpr float kMaximumModelScale = 5.0f;
    constexpr double kWheelScaleFactor = 1.1;
    const float old_scale = scale();
    const float new_scale =
        std::clamp(old_scale * static_cast<float>(std::pow(kWheelScaleFactor, wheel_steps)),
                   kMinimumModelScale, kMaximumModelScale);
    const float ratio = new_scale / old_scale;
    const auto old_origin = origin();
    set_scale(new_scale);
    set_origin({position.x + (old_origin.x - position.x) * ratio,
                position.y + (old_origin.y - position.y) * ratio});
}

void WallpaperController::pointer_enter(Vec2 position)
{
    if (current_instance_)
        accept(current_instance_->pointer_enter(position));
}

void WallpaperController::pointer_leave()
{
    cancel_pointer_gesture();
    if (current_instance_)
        accept(current_instance_->pointer_leave());
}

void WallpaperController::touch(Vec2 position)
{
    if (current_instance_)
        accept(current_instance_->touch(position));
}

void WallpaperController::reset()
{
    cancel_pointer_gesture();
    if (current_instance_)
        accept(current_instance_->reset());
    synchronize_pause();
}

bool WallpaperController::paused() const noexcept
{
    return paused_;
}

std::optional<AudioCommand> WallpaperController::pop_audio_command()
{
    if (current_instance_)
        return current_instance_->pop_audio_command();
    return std::nullopt;
}

void WallpaperController::set_audio_level(float rms)
{
    if (current_instance_)
        accept(current_instance_->set_audio_level(rms));
}

int WallpaperController::current_character_index() const noexcept
{
    return current_character_index_;
}

int WallpaperController::current_background_index() const noexcept
{
    return current_background_index_;
}

void WallpaperController::set_current_character_index(int index) noexcept
{
    current_character_index_ = index;
}

std::string WallpaperController::current_character_name() const noexcept
{
    return current_name_;
}

float WallpaperController::cutoff_y() const noexcept
{
    return current_cutoff_y_;
}

bool WallpaperController::has_cutoff() const noexcept
{
    return current_has_cutoff_;
}

bool WallpaperController::mirror() const noexcept
{
    return current_program_ && current_program_->mirror();
}

SessionStatus WallpaperController::last_error() const noexcept
{
    return last_error_;
}

void WallpaperController::clear_error() noexcept
{
    last_error_ = {};
}
} // namespace nekomote::application
