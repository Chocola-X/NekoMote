#pragma once

#include <nekomote/runtime/animation.hpp>
#include <nekomote/runtime/physics.hpp>

#include <memory>
#include <span>
#include <vector>

namespace nekomote::runtime
{
// Shared wall-time budget for model ticks and application fades. Long stalls
// discard excess time instead of jumping animations or completing fades.
inline constexpr double kMaximumAdvanceSeconds = 0.25;

enum class PhysicsMode
{
    Enabled,
    DisabledDiagnostic
};
enum class PlaybackState
{
    Running,
    Paused
};
enum class PlaybackError
{
    None,
    InvalidElapsed,
    Animation,
    Geometry,
    Physics,
    Faulted
};
struct PlaybackStatus
{
    PlaybackError error{PlaybackError::None};
    AnimationStatus animation;
    GeometryStatus geometry;
    PhysicsStatus physics;
    explicit operator bool() const noexcept
    {
        return error == PlaybackError::None;
    }
};
// Application schedulers may inject numeric events before each author tick.
// The callback runs on the evaluation thread and may call play/transition, but
// must not advance/reset this instance. Failed callbacks fault partial input.
struct AuthorTickHandler
{
    void* context{nullptr};
    PlaybackStatus (*before_tick)(void*, std::uint64_t) noexcept {nullptr};
};
class PlaybackInstance;
struct PlaybackCompileResult;
// Owns compiled scene, animation and physical bindings. The source asset may
// unload; all instances borrow this program. GPU assets remain separate.
class PlaybackProgram
{
  public:
    ~PlaybackProgram();
    PlaybackProgram(const PlaybackProgram&) = delete;
    PlaybackProgram& operator=(const PlaybackProgram&) = delete;
    const GeometryScene& scene() const noexcept;
    const AnimationProgram& animation() const noexcept;
    PhysicsMode physics_mode() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit PlaybackProgram(std::unique_ptr<Impl> impl);
    friend class PlaybackInstance;
    friend PlaybackCompileResult compile_playback(const model::ModelAsset&, PhysicsMode,
                                                  std::span<const VariableSourceDefinition>);
};
struct PlaybackCompileResult
{
    std::unique_ptr<const PlaybackProgram> program;
    std::vector<std::string> errors;
};
PlaybackCompileResult compile_playback(const model::ModelAsset& asset,
                                       PhysicsMode mode = PhysicsMode::Enabled,
                                       std::span<const VariableSourceDefinition> sources = {});
// Single-threaded CPU session. Each 60 Hz author tick advances animation, binds
// its parameters, evaluates native anchors and advances all physics once. Frame
// preparation then evaluates final parameters and builds the render protocol.
// Display partition never skips intermediate physical ticks. No steady heap/I/O.
// Failures during stepping/preparation fault the session until reset: no stale
// values/frame escape, and later calls cannot continue with inconsistent clocks.
class PlaybackInstance
{
  public:
    explicit PlaybackInstance(const PlaybackProgram& program, AnimationConfig animation = {});
    ~PlaybackInstance();
    PlaybackInstance(const PlaybackInstance&) = delete;
    PlaybackInstance& operator=(const PlaybackInstance&) = delete;
    PlaybackStatus transition(ParamId id, float target, float frames = 0,
                              float easing = 0) noexcept;
    PlaybackStatus transition_difference(VariableSourceId source, ParamId id, float target,
                                         float frames = 0, float easing = 0) noexcept;
    float difference_value(VariableSourceId source, ParamId id) const noexcept;
    PlaybackStatus play(TimelineId id, TimelinePlayMode mode) noexcept;
    PlaybackStatus stop(TimelineId id) noexcept;
    PlaybackStatus set_timeline_weight(
        TimelineId id, float target, float frames = 0, float easing = 0,
        TimelineWeightCompletion completion = TimelineWeightCompletion::KeepPlaying) noexcept;
    float timeline_weight(TimelineId id) const noexcept;
    PlaybackStatus fade_in(TimelineId id, float frames = 0, float easing = 0) noexcept;
    PlaybackStatus fade_out(TimelineId id, float frames = 0, float easing = 0) noexcept;
    bool is_playing(TimelineId id) const noexcept;
    PlaybackStatus set_origin(Vec2 origin) noexcept;
    PlaybackStatus set_scale(float scale) noexcept;
    PlaybackStatus step(PhysicsInputs inputs = {}) noexcept;
    // Drop elapsed beyond 250 ms and bound catch-up to 15 ticks. Invalid elapsed
    // changes no state. Pause/resume clears the fractional clock accumulator.
    PlaybackStatus advance(double elapsed_seconds, PhysicsInputs inputs = {},
                           AuthorTickHandler handler = {}) noexcept;
    void set_playback_state(PlaybackState state) noexcept;
    void reset() noexcept; // zero parameters, reset physics/clock; preserve origin/scale
    PlaybackStatus prepare_frame() noexcept;
    // Borrowed views expire at the next parameter/animation/physics mutation or
    // preparation. frame() is empty until preparation succeeds after a mutation.
    std::span<const ParameterValue> values() const noexcept;
    RenderFrame frame() const noexcept;
    std::uint64_t tick_count() const noexcept;
    // Offline inspection of the last prepared frame only. No source values are
    // accepted and no snapshots are gathered in advance/prepare_frame.
    std::optional<GeometryNodeObservation> observe_node(NodeId id) const noexcept;
    std::optional<SpringObservation> observe_spring(std::size_t index) const noexcept;
    // Scene-owned labels and prepared draw ranges; same validity as frame().
    std::span<const GeometryPrimitive> frame_primitives() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nekomote::runtime
