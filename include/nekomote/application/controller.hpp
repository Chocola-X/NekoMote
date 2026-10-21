#pragma once

#include <nekomote/application/catalog.hpp>
#include <nekomote/application/session.hpp>
#include <nekomote/asset_io/image.hpp>
#include <nekomote/image.hpp>
#include <nekomote/model/audio_clip.hpp>
#include <nekomote/types.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nekomote::application
{
namespace fs = std::filesystem;

constexpr double kCharacterFadeSeconds = 0.4;

enum class SwitchState
{
    Idle,
    Pending,
    CrossFade,
};

enum class FitPolicy
{
    KeepConfigured,
    FitToWindow,
};

struct ControllerConfig
{
    std::uint32_t seed{1};
    bool mute{false};
    Vec2 origin{0, 400};
    float scale{1};
    PointerScope pointer{PointerScope::LocalSurface};
    AudioMode audio{AudioMode::Device};
};

struct CandidateTexture
{
    TextureId id;
    asset_io::DecodedImageRgba8 image;
    TextureRegion region;
};

struct CandidateCpu
{
    std::unique_ptr<const SessionProgram> program;
    std::unique_ptr<SessionInstance> instance;
    std::vector<CandidateTexture> textures;
    std::vector<model::AudioClip> audio_clips;
    std::string name;
    fs::path model_dir;
    float cutoff_y{0};
    bool has_cutoff{false};
    std::string error;
    bool ok{false};
};

enum class BackgroundAction
{
    None,
    Switch,
    Clear,
};

struct BackgroundRequest
{
    BackgroundAction action{BackgroundAction::None};
    int background_index{-1};
    fs::path path;
    double fade_duration{0};
};

// WallpaperController owns CPU sessions and business state for one wallpaper
// instance. Hosts own GPU renderers and audio devices; the controller produces
// CPU candidates and the host performs GPU upload, reporting success or failure.
// Single evaluation thread: the host must call advance/prepare_frame/poll on the
// same thread that owns the controller.
class WallpaperController
{
  public:
    WallpaperController();
    ~WallpaperController();
    WallpaperController(const WallpaperController&) = delete;
    WallpaperController& operator=(const WallpaperController&) = delete;

    // === Assets and config ===
    bool scan_assets(const fs::path& assets_dir);
    const AssetCatalog& catalog() const noexcept;
    void load_config(const fs::path& path);
    void save_config(const fs::path& path) const;
    WallpaperConfig& config() noexcept;
    const WallpaperConfig& config() const noexcept;
    fs::path config_path() const noexcept;
    void set_config_path(const fs::path& path) noexcept;
    bool no_save() const noexcept;
    void set_no_save(bool value) noexcept;

    // === Session access (host renders using these) ===
    SessionInstance* current_instance() noexcept;
    const SessionProgram* current_program() const noexcept;
    SessionInstance* previous_instance() noexcept;
    const SessionProgram* previous_program() const noexcept;

    // === Initial load (synchronous CPU) ===
    // Returns CandidateCpu for the host to perform GPU upload. After upload,
    // call install_initial.
    CandidateCpu load_initial(int character_index, const ControllerConfig& cfg);
    CandidateCpu load_initial(const CharacterEntry& entry, const ControllerConfig& cfg);
    // Host confirms GPU upload done; controller takes ownership of CPU session.
    void install_initial(CandidateCpu candidate);

    // Synchronous switching is used by owned-window failure diagnostics.
    CandidateCpu load_candidate(const CharacterEntry& entry, const ControllerConfig& cfg);
    void install_candidate_sync(CandidateCpu candidate);
    void clear_previous() noexcept;

    // === Character switch ===
    void request_switch_character(int index);
    // Returns candidate if CPU load is ready. Host performs GPU upload, then
    // calls confirm_candidate or reject_candidate.
    std::optional<CandidateCpu> poll_candidate();
    void confirm_candidate(CandidateCpu candidate);
    void reject_candidate(std::string error);
    SwitchState switch_state() const noexcept;
    float fade_alpha() const noexcept;
    int fade_target_character() const noexcept;

    // === Background ===
    void request_switch_background(int index);
    void request_clear_background();
    std::optional<BackgroundRequest> poll_background();
    void confirm_background(int background_index, TextureId old_id, TextureId new_id, int width,
                            int height, double fade_duration);
    void confirm_background_clear();
    bool has_background() const noexcept;
    std::filesystem::path background_path(int index) const noexcept;

    // === Session advance ===
    void advance(double elapsed_seconds);
    void prepare_frame();

    // === Fit ===
    void fit_to_output(int width, int height);
    void save_ui_layout(Vec2 icon_position, Vec2 sidebar_position, bool sidebar_free);

    // === Output ===
    // First nonzero output restores the saved transform and establishes the
    // default reset pose. Later resizes recover a character fully outside view.
    void set_output_size(int width, int height);
    void set_paused(bool paused);
    void set_minimized(bool minimized);
    void set_output_unavailable(bool unavailable);

    // === Interaction ===
    void set_origin(Vec2 origin);
    void set_scale(float scale);
    void reset_character_transform();
    Vec2 origin() const noexcept;
    float scale() const noexcept;
    using PointerTime = std::chrono::steady_clock::time_point;
    // Positions use centered output pixels, Y down. Hosts forward only events
    // not consumed by UI. A left click touches on release; held-button movement past 4px drags
    // immediately.
    void pointer_press(Vec2 position, PointerTime now = std::chrono::steady_clock::now());
    void pointer_move(Vec2 position, PointerTime now = std::chrono::steady_clock::now());
    void pointer_release(Vec2 position, PointerTime now = std::chrono::steady_clock::now());
    void cancel_pointer_gesture() noexcept;
    void zoom_at(Vec2 position, double wheel_steps);
    void pointer_enter(Vec2 position);
    void pointer_leave();
    void touch(Vec2 position);
    void reset();
    bool paused() const noexcept;

    // === Audio ===
    std::optional<AudioCommand> pop_audio_command();
    void set_audio_level(float rms);

    // === Indices ===
    int current_character_index() const noexcept;
    int current_background_index() const noexcept;

    // === Character info ===
    std::string current_character_name() const noexcept;
    float cutoff_y() const noexcept;
    bool has_cutoff() const noexcept;
    bool mirror() const noexcept;

    // === Error ===
    SessionStatus last_error() const noexcept;
    void clear_error() noexcept;

  private:
    static CandidateCpu load_candidate_cpu(const CharacterEntry& entry,
                                           const ControllerConfig& cfg);
    void apply_controller_config(const ControllerConfig& cfg);
    void synchronize_pause();
    void advance_fade(double elapsed_seconds);
    void discard_previous_audio() noexcept;
    void accept(SessionStatus status);
    void update_background_phase();
    void persist_character_transform();
    void fit_character_to_output(int width, int height);
    void restore_character_transform(int width, int height);
    bool character_visible(int width, int height) const;

    AssetCatalog catalog_{};
    WallpaperConfig config_{};
    fs::path config_path_{};
    bool no_save_{false};

    std::unique_ptr<const SessionProgram> current_program_;
    std::unique_ptr<SessionInstance> current_instance_;
    std::unique_ptr<const SessionProgram> previous_program_;
    std::unique_ptr<SessionInstance> previous_instance_;

    std::string current_name_;
    fs::path current_model_dir_;
    float current_cutoff_y_{0};
    bool current_has_cutoff_{false};
    bool current_has_background_{false};

    int current_character_index_{0};
    int current_background_index_{-1};

    SwitchState switch_state_{SwitchState::Idle};
    float fade_alpha_{1};
    int fade_target_character_{-1};
    std::future<CandidateCpu> candidate_future_;
    double fade_elapsed_seconds_{0};

    bool need_bg_switch_{false};
    int target_background_index_{-1};
    bool need_bg_clear_{false};
    bool background_preference_pending_{false};
    int current_bg_phase_{0};

    ControllerConfig controller_config_{};
    Vec2 initial_origin_{0, 400};
    float initial_scale_{1};
    Vec2 reset_origin_{0, 400};
    float reset_scale_{1};
    bool reset_transform_pending_fit_{true};
    bool initial_layout_pending_{true};
    int output_width_{0};
    int output_height_{0};
    bool paused_{false};
    bool minimized_{false};
    bool output_unavailable_{false};
    bool pointer_pressed_{false};
    bool pointer_moved_{false};
    Vec2 press_position_{};
    Vec2 drag_previous_{};

    SessionStatus last_error_{};
};
} // namespace nekomote::application
