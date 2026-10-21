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

fs::path resolve_audio_path(const fs::path& session_dir, const fs::path& role_dir,
                            const std::string& value)
{
    const fs::path relative(value);
    if (relative.empty() || relative.is_absolute() || relative.extension() != ".flac" ||
        std::any_of(relative.begin(), relative.end(),
                    [](const fs::path& part) { return part == ".."; }))
        throw std::runtime_error(value + ": audio must be a relative .flac path");
    const auto source = session_dir / relative;
    // Resolve directory and file links once, while constructing the candidate.
    // Shared voice groups may leave the outfit directory, but never the role.
    const auto resolved = fs::canonical(source);
    auto root = role_dir.begin();
    auto part = resolved.begin();
    for (; root != role_dir.end() && part != resolved.end() && *root == *part; ++root, ++part)
    {
    }
    if (root != role_dir.end() || !fs::is_regular_file(resolved))
        throw std::runtime_error(
            source.string() +
            ": audio target escapes character directory or is not a regular file");
    return resolved;
}

int minutes_at(std::chrono::system_clock::time_point now)
{
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    if (!::localtime_r(&t, &local))
        return 0;
    return local.tm_hour * 60 + local.tm_min;
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
    // First launch means no config file exists yet. Pick the first background
    // deterministically (catalog order) instead of leaving a black wallpaper;
    // an existing config with an explicitly cleared background keeps the empty
    // state. Hosts scan assets before loading the config, so the catalog is
    // available here.
    std::error_code ec;
    const bool first_launch = !fs::exists(path, ec);
    config_ = WallpaperConfig::load(path);
    if (first_launch && config_.background.empty() && !catalog_.backgrounds.empty())
    {
        config_.background = catalog_.backgrounds.front().name;
    }
}

void WallpaperController::save_config(const fs::path& path) const
{
    config_.save(path);
}

void WallpaperController::persist_character_transform()
{
    config_.scale = controller_config_.scale;
    config_.origin_x = controller_config_.origin.x;
    config_.origin_y = controller_config_.origin.y;
    config_.transform_saved = true;
    if (!no_save_ && !config_path_.empty())
    {
        try
        {
            config_.save(config_path_);
        }
        catch (const std::exception& error)
        {
            // Input can arrive through noexcept window callbacks. Preserve the
            // failure for the next checked application/ABI frame boundary.
            std::fprintf(stderr, "Cannot save character transform to %s: %s\n",
                         config_path_.c_str(), error.what());
            accept({SessionError::Faulted, {}});
        }
    }
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
    reset_origin_ = cfg.origin;
    reset_scale_ = cfg.scale;
    reset_transform_pending_fit_ = true;
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
    if (previous_instance_)
    {
        discard_previous_audio();
        accept(previous_instance_->set_playback_state(paused_ || minimized_ || output_unavailable_
                                                          ? runtime::PlaybackState::Paused
                                                          : runtime::PlaybackState::Running));
        discard_previous_audio();
    }
}

void WallpaperController::discard_previous_audio() noexcept
{
    // A fading-out session supplies only pixels. Its bounded audio queue has no
    // device consumer; discard events at each lifecycle/advance boundary.
    if (previous_instance_)
        while (previous_instance_->pop_audio_command())
        {
        }
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
        {
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
        } // Source trees are no longer needed before instance/audio/image allocation.
        const auto& program = *result.program;
        result.instance = std::make_unique<SessionInstance>(
            program, SessionConfig{{cfg.seed}, cfg.seed, cfg.pointer, cfg.audio});
        // Loading runs on a worker. It owns only this candidate and must not
        // mutate Controller error state or publish a partially valid session.
        const auto check = [&program](SessionStatus status)
        {
            if (status)
                return;
            const auto& geometry = status.playback.geometry;
            throw std::runtime_error(
                "candidate session failure " + std::to_string(static_cast<int>(status.error)) +
                "/" + std::to_string(static_cast<int>(status.playback.error)) +
                "; geometry=" + std::to_string(static_cast<int>(geometry.error)) +
                "; node=" + std::string(program.playback().scene().node_label(geometry.node)));
        };
        check(result.instance->set_origin(cfg.origin));
        check(result.instance->set_scale(cfg.scale));
        check(result.instance->start());
        check(result.instance->prepare_frame());
        if (cfg.audio == AudioMode::Device && !program.audio_paths().empty())
        {
            const auto session_dir = entry.session_path.parent_path();
            const auto role_dir = fs::canonical(session_dir).parent_path();
            for (const auto& path : program.audio_paths())
            {
                auto clip = asset_io::load_flac(resolve_audio_path(session_dir, role_dir, path));
                if (!clip.ok)
                    throw std::runtime_error(clip.error);
                result.audio_clips.push_back(std::move(clip.clip));
            }
        }
        const auto& scene = program.playback().scene();
        for (const auto& texture : scene.textures())
        {
            auto image = asset_io::load_cropped_png(entry.model_path.parent_path() / texture.path);
            if (!image.ok)
                throw std::runtime_error(image.error);
            if (image.region.source_width != texture.width ||
                image.region.source_height != texture.height)
                throw std::runtime_error(texture.path + ": PNG dimensions disagree with scene");
            result.textures.push_back({texture.id, std::move(image.image), image.region});
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
    config_.character = current_name_;
    config_.transform_saved = false;
    initial_layout_pending_ = true;
    controller_config_.origin = initial_origin_;
    controller_config_.scale = initial_scale_;
    reset_origin_ = initial_origin_;
    reset_scale_ = initial_scale_;
    reset_transform_pending_fit_ = true;
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
    previous_instance_.reset();
    previous_program_.reset();
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
    initial_layout_pending_ = true;
    if (!config_.character.empty() && config_.character != current_name_)
        config_.transform_saved = false;
    config_.character = current_name_;
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
    auto cfg = controller_config_;
    cfg.origin = initial_origin_;
    cfg.scale = initial_scale_;
    candidate_future_ =
        std::async(std::launch::async, [entry, cfg]() { return load_candidate_cpu(entry, cfg); });
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
    config_.transform_saved = false;
    controller_config_.origin = initial_origin_;
    controller_config_.scale = initial_scale_;
    reset_origin_ = initial_origin_;
    reset_scale_ = initial_scale_;
    reset_transform_pending_fit_ = true;
    if (current_instance_)
    {
        accept(current_instance_->set_origin(initial_origin_));
        accept(current_instance_->set_scale(initial_scale_));
    }
    if (!no_save_ && !config_path_.empty())
        config_.save(config_path_);
    switch_state_ = SwitchState::CrossFade;
    fade_alpha_ = 0.0f;
    fade_elapsed_seconds_ = 0;
    synchronize_pause();
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
    need_bg_clear_ = false;
    background_preference_pending_ = true;
    if (catalog_.backgrounds[index].type == BackgroundType::Dynamic)
        current_bg_phase_ = phase_at(catalog_.backgrounds[index], std::chrono::system_clock::now());
    need_bg_switch_ = true;
}

void WallpaperController::request_clear_background()
{
    need_bg_switch_ = false;
    need_bg_clear_ = true;
    background_preference_pending_ = true;
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
                                     static_cast<double>(config_.background_fade_seconds)};
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
    const bool save_preference =
        background_preference_pending_ && background_index == target_background_index_;
    if (save_preference)
    {
        background_preference_pending_ = false;
        if (!no_save_ && !config_path_.empty())
            config_.save(config_path_);
    }
}

void WallpaperController::confirm_background_clear()
{
    if (current_instance_)
        current_instance_->clear_background();
    current_has_background_ = false;
    current_background_index_ = -1;
    config_.background.clear();
    if (background_preference_pending_)
    {
        background_preference_pending_ = false;
        if (!no_save_ && !config_path_.empty())
            config_.save(config_path_);
    }
}

bool WallpaperController::has_background() const noexcept
{
    return current_has_background_;
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
    // A user selection takes priority over an automatic phase refresh.
    if (need_bg_switch_ || need_bg_clear_)
        return;
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
    background_preference_pending_ = false;
    target_background_index_ = current_background_index_;
    need_bg_switch_ = true;
}

void WallpaperController::advance_fade(double elapsed_seconds)
{
    if (switch_state_ != SwitchState::CrossFade || paused_ || minimized_ || output_unavailable_ ||
        !std::isfinite(elapsed_seconds) || elapsed_seconds < 0 || !last_error_)
        return;
    fade_elapsed_seconds_ += std::min(elapsed_seconds, runtime::kMaximumAdvanceSeconds);
    fade_alpha_ = static_cast<float>(fade_elapsed_seconds_ / kCharacterFadeSeconds);
    if (fade_alpha_ >= 1.0f)
    {
        fade_alpha_ = 1.0f;
        switch_state_ = SwitchState::Idle;
        clear_previous();
    }
}

void WallpaperController::advance(double elapsed_seconds)
{
    update_background_phase();
    if (current_instance_)
        accept(current_instance_->advance(elapsed_seconds));
    if (switch_state_ == SwitchState::CrossFade && previous_instance_)
    {
        accept(previous_instance_->advance(elapsed_seconds));
        discard_previous_audio();
    }
    // Character and background transitions share the runtime's bounded budget.
    // Host unavailability and user pause freeze both models and the transition.
    advance_fade(elapsed_seconds);
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
    fit_character_to_output(width, height);
    if (current_instance_ && width > 32 && height > 32)
        persist_character_transform();
}

void WallpaperController::fit_character_to_output(int width, int height)
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
    if (reset_transform_pending_fit_)
    {
        reset_origin_ = origin;
        reset_scale_ = scale;
        reset_transform_pending_fit_ = false;
    }
}

bool WallpaperController::character_visible(int width, int height) const
{
    // Frame geometry uses centered output pixels; intentional partial cropping
    // is valid. Inspect draw geometry rather than the model's origin, which can
    // be outside the view even when the character remains visible.
    const auto frame = current_instance_->frame();
    const float half_width = static_cast<float>(width) * .5f;
    const float half_height = static_cast<float>(height) * .5f;
    for (const auto& command : frame.commands)
    {
        float left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
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
        if (left < half_width && right > -half_width && top < half_height && bottom > -half_height)
            return true;
    }
    return false;
}

void WallpaperController::restore_character_transform(int width, int height)
{
    const Vec2 saved_origin{config_.origin_x, config_.origin_y};
    const float saved_scale = config_.scale;
    const bool has_saved = config_.transform_saved;
    // Establish reset from the default pose for this output, independently of
    // a previous session's user transform. No intermediate configuration writes.
    set_scale(initial_scale_);
    set_origin(initial_origin_);
    reset_transform_pending_fit_ = true;
    prepare_frame();
    fit_character_to_output(width, height);
    if (has_saved && std::isfinite(saved_origin.x) && std::isfinite(saved_origin.y) &&
        std::isfinite(saved_scale) && saved_scale >= .05f && saved_scale <= 5.0f)
    {
        set_scale(saved_scale);
        set_origin(saved_origin);
        prepare_frame();
        if (!character_visible(width, height))
        {
            set_scale(reset_scale_);
            set_origin(reset_origin_);
        }
    }
    prepare_frame();
    // Output setup and resize may recover layout in memory; only a user action
    // persists preferences. This also keeps first launch completely read-only.
    config_.scale = controller_config_.scale;
    config_.origin_x = controller_config_.origin.x;
    config_.origin_y = controller_config_.origin.y;
}

void WallpaperController::save_ui_layout(Vec2 icon_position, Vec2 sidebar_position,
                                         bool sidebar_free)
{
    config_.icon_position_saved = true;
    config_.icon_x = icon_position.x;
    config_.icon_y = icon_position.y;
    config_.sidebar_position_saved = sidebar_free;
    config_.sidebar_x = sidebar_free ? sidebar_position.x : 8.0f;
    config_.sidebar_y = sidebar_free ? sidebar_position.y : 8.0f;
    config_.sidebar_free = sidebar_free;
    if (!no_save_ && !config_path_.empty())
        config_.save(config_path_);
}

void WallpaperController::set_output_size(int width, int height)
{
    const bool resized = width != output_width_ || height != output_height_;
    output_width_ = width;
    output_height_ = height;
    if (current_instance_)
    {
        accept(current_instance_->set_output_size(width, height));
        if (width > 32 && height > 32)
        {
            if (initial_layout_pending_)
            {
                initial_layout_pending_ = false;
                if (config_.fit || config_.transform_saved)
                    restore_character_transform(width, height);
            }
            else if (resized && (config_.fit || config_.transform_saved))
            {
                prepare_frame();
                if (!character_visible(width, height))
                {
                    config_.transform_saved = false;
                    restore_character_transform(width, height);
                }
            }
        }
    }
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

void WallpaperController::reset_character_transform()
{
    cancel_pointer_gesture();
    set_scale(reset_scale_);
    set_origin(reset_origin_);
    persist_character_transform();
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
            controller_config_.origin = {old_origin.x + position.x - drag_previous_.x,
                                         old_origin.y + position.y - drag_previous_.y};
            if (current_instance_)
                accept(current_instance_->drag_origin(controller_config_.origin));
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
    const bool dragged = pointer_moved_;
    cancel_pointer_gesture();
    if (dragged)
        persist_character_transform();
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
    persist_character_transform();
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
