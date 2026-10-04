#pragma once

#include <nekomote/model/asset.hpp>
#include <nekomote/runtime/geometry_scene.hpp>
#include <nekomote/runtime/parameter.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nekomote::runtime
{
enum class AnimationError
{
    None,
    InvalidParameter,
    InvalidTimeline,
    InvalidValue,
    UnsupportedTimeline,
    UnsupportedController
};
struct AnimationStatus
{
    AnimationError error{AnimationError::None};
    TimelineId timeline;
    ParamId parameter;
    explicit operator bool() const noexcept
    {
        return error == AnimationError::None;
    }
};
enum class TimelinePlayMode
{
    Replace,
    Parallel,
    AdditiveParallel
};
enum class PlaybackState
{
    Running,
    Paused
};
class AnimationInstance;
struct AnimationCompileResult;
// Immutable variable/controller/clip data. Geometry IDs are bound at compile
// time; neither source assets nor the GeometryScene must outlive this program.
// This module does not integrate physical controllers or automatic blinking.
class AnimationProgram
{
  public:
    ~AnimationProgram();
    AnimationProgram(const AnimationProgram&) = delete;
    AnimationProgram& operator=(const AnimationProgram&) = delete;
    TimelineId find_timeline(std::string_view label) const noexcept;
    std::string_view timeline_label(TimelineId id) const noexcept;
    std::span<const std::string> diagnostics() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit AnimationProgram(std::unique_ptr<Impl> impl);
    friend class AnimationInstance;
    friend AnimationCompileResult compile_animation(const model::ModelAsset&, const GeometryScene&);
};
struct AnimationCompileResult
{
    std::unique_ptr<const AnimationProgram> program;
    std::vector<std::string> errors;
};
AnimationCompileResult compile_animation(const model::ModelAsset& asset,
                                         const GeometryScene& scene);
// Program must outlive instances. Single thread; all frame storage is allocated
// during construction. Author time is 60 Hz, independent of presentation FPS.
// values() expires on mutation; values include additive contributions once.
class AnimationInstance
{
  public:
    explicit AnimationInstance(const AnimationProgram& program);
    ~AnimationInstance();
    AnimationInstance(const AnimationInstance&) = delete;
    AnimationInstance& operator=(const AnimationInstance&) = delete;
    AnimationStatus transition(ParamId parameter, float target, float author_frames = 0,
                               float easing = 0) noexcept;
    AnimationStatus play(TimelineId timeline, TimelinePlayMode mode) noexcept;
    AnimationStatus stop(TimelineId timeline) noexcept;
    bool is_playing(TimelineId timeline) const noexcept;
    AnimationStatus step() noexcept;
    // Limits catch-up to 15 author ticks (250 ms). Excess elapsed time is dropped,
    // preventing resume storms. Pause/resume discards the fractional accumulator.
    AnimationStatus advance(double elapsed_seconds) noexcept;
    void set_playback_state(PlaybackState state) noexcept;
    void reset() noexcept;
    std::span<const ParameterValue> values() const noexcept;
    std::uint64_t tick_count() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nekomote::runtime
