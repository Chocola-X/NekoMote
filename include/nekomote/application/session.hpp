#pragma once

#include <nekomote/model/session_asset.hpp>
#include <nekomote/runtime/playback.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nekomote::application
{
enum class PointerScope
{
    Unavailable,
    LocalSurface
};
enum class AudioMode
{
    Disabled,
    Device
};
struct SessionConfig
{
    runtime::AnimationConfig animation;
    std::uint32_t reaction_seed{0};
    PointerScope pointer{PointerScope::Unavailable};
    AudioMode audio{AudioMode::Disabled};
};
enum class SessionError
{
    None,
    InvalidInput,
    InvalidReaction,
    Busy,
    InputUnavailable,
    EventQueueFull,
    Playback,
    Faulted
};
struct SessionStatus
{
    SessionError error{SessionError::None};
    runtime::PlaybackStatus playback;
    explicit operator bool() const noexcept
    {
        return error == SessionError::None;
    }
};
enum class AudioCommandKind
{
    Play,
    Stop,
    Pause,
    Resume
};
struct AudioCommand
{
    AudioCommandKind kind{AudioCommandKind::Stop};
    AudioClipId clip;
};
enum class ReactionPhase
{
    Idle,
    Playing,
    Recovering
};
class SessionInstance;
struct SessionCompileResult;
// Immutable compiled application bindings. Owns the runtime program and rules;
// source model/session assets may unload. Audio/GPU resources belong to hosts.
class SessionProgram
{
  public:
    ~SessionProgram();
    SessionProgram(const SessionProgram&) = delete;
    SessionProgram& operator=(const SessionProgram&) = delete;
    const runtime::PlaybackProgram& playback() const noexcept;
    std::span<const std::string> audio_paths() const noexcept;
    ReactionId find_reaction(std::string_view label) const noexcept;
    std::string_view reaction_label(ReactionId id) const noexcept;
    bool gaze_available() const noexcept;
    bool touch_available() const noexcept;
    bool mirror() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit SessionProgram(std::unique_ptr<Impl>);
    friend class SessionInstance;
    friend SessionCompileResult compile_session(const model::ModelAsset&,
                                                const model::SessionAsset&);
};
struct SessionCompileResult
{
    std::unique_ptr<const SessionProgram> program;
    std::vector<std::string> errors;
};
SessionCompileResult compile_session(const model::ModelAsset&, const model::SessionAsset&);
// Single evaluation thread, borrowed program must outlive instances. Input
// coordinates are centered output pixels (Y down). Hosts convert their surface
// coordinates explicitly; no hidden global pointer or graphics/audio calls.
class SessionInstance
{
  public:
    explicit SessionInstance(const SessionProgram&, SessionConfig config = {});
    ~SessionInstance();
    SessionInstance(const SessionInstance&) = delete;
    SessionInstance& operator=(const SessionInstance&) = delete;
    SessionStatus start() noexcept;
    SessionStatus set_origin(Vec2 origin) noexcept;
    SessionStatus set_scale(float scale) noexcept;
    SessionStatus pointer_move(Vec2 position) noexcept;
    SessionStatus pointer_leave() noexcept;
    SessionStatus pointer_enter(Vec2 position) noexcept;
    // Touch selects a seeded reaction from the first eligible region in authored
    // bust/eye/face/head/pant priority. Empty means no region; Busy preserves state.
    SessionStatus touch(Vec2 position) noexcept;
    SessionStatus react(ReactionId id) noexcept;
    // Latest RMS snapshot from actual PCM playback, in 0..1. No fake audio.
    SessionStatus set_audio_level(float rms) noexcept;
    SessionStatus set_playback_state(runtime::PlaybackState state) noexcept;
    // Model and background accept at most runtime::kMaximumAdvanceSeconds per
    // call; paused or failed advances leave the background fade unchanged.
    SessionStatus advance(double elapsed_seconds, runtime::PhysicsInputs inputs = {}) noexcept;
    SessionStatus prepare_frame() noexcept;
    SessionStatus reset() noexcept;
    std::optional<AudioCommand> pop_audio_command() noexcept;
    ReactionPhase reaction_phase() const noexcept;
    ReactionId active_reaction() const noexcept;
    const runtime::PlaybackInstance& playback() const noexcept;
    RenderFrame frame() const noexcept;
    // Background layer: rendered before frame() with Clear, then frame() with
    // Preserve. Hosts own texture upload; these calls only track geometry and
    // fade state. set_output_size is required before background_frame to size
    // the full-screen quad. fade_background cross-fades over duration_seconds.
    SessionStatus set_output_size(int width, int height) noexcept;
    SessionStatus set_background(TextureId id, int width, int height) noexcept;
    SessionStatus fade_background(TextureId id, int width, int height,
                                  double duration_seconds) noexcept;
    void clear_background() noexcept;
    RenderFrame background_frame() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nekomote::application
