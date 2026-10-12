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
    UnsupportedController,
    InvalidVariableSource
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
    // Linear numeric tracks add independently; typed controllers dispatch to
    // the base graph, including Selector outputs and Eye/Expression routes.
    AdditiveParallel
};
enum class TimelineWeightCompletion
{
    KeepPlaying,
    Stop
};
class AnimationInstance;
struct AnimationCompileResult;
struct AnimationConfig
{
    // Explicit per-instance seed. Zero is deterministic; a session owner may
    // provide its wall-clock Unix seconds. Author advancement stays monotonic.
    std::uint32_t random_seed{0};
};
struct VariableSourceDefinition
{
    std::string label;
    std::vector<std::string> variables;
};
// Immutable variable/controller/clip data. Geometry IDs are bound at compile
// time; neither source assets nor the GeometryScene must outlive this program.
// Eye blinking shares the author clock; physical controllers remain separate.
class AnimationProgram
{
  public:
    ~AnimationProgram();
    AnimationProgram(const AnimationProgram&) = delete;
    AnimationProgram& operator=(const AnimationProgram&) = delete;
    TimelineId find_timeline(std::string_view label) const noexcept;
    bool timeline_supported(TimelineId id) const noexcept;
    VariableSourceId find_variable_source(std::string_view label) const noexcept;
    std::span<const std::string> diagnostics() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit AnimationProgram(std::unique_ptr<Impl> impl);
    friend class AnimationInstance;
    friend AnimationCompileResult compile_animation(const model::ModelAsset&, const GeometryScene&,
                                                    std::span<const VariableSourceDefinition>);
};
struct AnimationCompileResult
{
    std::unique_ptr<const AnimationProgram> program;
    std::vector<std::string> errors;
};
AnimationCompileResult compile_animation(const model::ModelAsset& asset, const GeometryScene& scene,
                                         std::span<const VariableSourceDefinition> sources = {});
// Program must outlive instances. Single thread; all frame storage is allocated
// during construction. Author time is 60 Hz, independent of presentation FPS.
// values() expires on mutation; values include additive contributions once.
class AnimationInstance
{
  public:
    explicit AnimationInstance(const AnimationProgram& program, AnimationConfig config = {});
    ~AnimationInstance();
    AnimationInstance(const AnimationInstance&) = delete;
    AnimationInstance& operator=(const AnimationInstance&) = delete;
    AnimationStatus transition(ParamId parameter, float target, float author_frames = 0,
                               float easing = 0) noexcept;
    // Independent numeric contribution. Source/parameter bindings are frozen
    // at compilation; requests interpolate without running base controllers.
    AnimationStatus transition_difference(VariableSourceId source, ParamId parameter, float target,
                                          float author_frames = 0, float easing = 0) noexcept;
    float difference_value(VariableSourceId source, ParamId parameter) const noexcept;
    AnimationStatus play(TimelineId timeline, TimelinePlayMode mode) noexcept;
    AnimationStatus stop(TimelineId timeline) noexcept;
    // Weight transitions act on difference timelines. Main timeline requests
    // are inert, matching the observed SDK contract. No weight range clamp.
    // The ratio cache updates on author ticks; play restores weight one.
    AnimationStatus set_timeline_weight(
        TimelineId timeline, float target, float author_frames = 0, float easing = 0,
        TimelineWeightCompletion completion = TimelineWeightCompletion::KeepPlaying) noexcept;
    float timeline_weight(TimelineId timeline) const noexcept;
    AnimationStatus fade_in(TimelineId timeline, float author_frames = 0,
                            float easing = 0) noexcept;
    AnimationStatus fade_out(TimelineId timeline, float author_frames = 0,
                             float easing = 0) noexcept;
    bool is_playing(TimelineId timeline) const noexcept;
    AnimationStatus step() noexcept;
    void reset() noexcept;
    std::span<const ParameterValue> values() const noexcept;
    std::uint64_t tick_count() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nekomote::runtime
