#include <nekomote/runtime/playback.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace nekomote::runtime
{
namespace
{
constexpr double kAuthorHz = 60;
constexpr double kClockBoundaryTolerance = 1e-9;
constexpr std::uint32_t kMaximumCatchUpTicks = 15;
} // namespace
struct PlaybackProgram::Impl
{
    std::unique_ptr<const GeometryScene> scene;
    std::unique_ptr<const AnimationProgram> animation;
    std::unique_ptr<const PhysicsProgram> physics; // destroyed before its borrowed scene
    PhysicsMode mode;
};
PlaybackProgram::PlaybackProgram(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
}
PlaybackProgram::~PlaybackProgram() = default;
const GeometryScene& PlaybackProgram::scene() const noexcept
{
    return *impl_->scene;
}
const AnimationProgram& PlaybackProgram::animation() const noexcept
{
    return *impl_->animation;
}
PhysicsMode PlaybackProgram::physics_mode() const noexcept
{
    return impl_->mode;
}
PlaybackCompileResult compile_playback(const model::ModelAsset& asset, PhysicsMode mode,
                                       std::span<const VariableSourceDefinition> sources)
{
    if (mode != PhysicsMode::Enabled && mode != PhysicsMode::DisabledDiagnostic)
        return {{}, {"unsupported playback physics mode"}};
    auto geometry = compile_geometry_scene(asset);
    if (!geometry.scene)
        return {{}, std::move(geometry.errors)};
    auto animation = compile_animation(asset, *geometry.scene, sources);
    if (!animation.program)
        return {{}, std::move(animation.errors)};
    auto data = std::make_unique<PlaybackProgram::Impl>();
    if (mode == PhysicsMode::Enabled)
    {
        auto physics = compile_physics(asset, *geometry.scene);
        if (!physics.program)
            return {{}, std::move(physics.errors)};
        data->physics = std::move(physics.program);
    }
    data->scene = std::move(geometry.scene);
    data->animation = std::move(animation.program);
    data->mode = mode;
    return {std::unique_ptr<const PlaybackProgram>(new PlaybackProgram(std::move(data))), {}};
}
struct PlaybackInstance::Impl
{
    GeometryInstance geometry;
    AnimationInstance animation;
    std::unique_ptr<PhysicsInstance> physics;
    std::vector<ParameterValue> output;
    PlaybackState state{PlaybackState::Running};
    double accumulator{0};
    std::uint64_t ticks{0};
    bool faulted{false}, dirty{true}, frame_ready{false};
    explicit Impl(const PlaybackProgram::Impl& data, AnimationConfig config)
        : geometry(*data.scene), animation(*data.animation, config)
    {
        if (data.physics)
            physics = std::make_unique<PhysicsInstance>(*data.physics);
        output.assign(animation.values().begin(), animation.values().end());
    }
    void compose() noexcept
    {
        for (const auto v : animation.values())
            output[v.id.value - 1] = v;
        if (physics)
            for (const auto v : physics->values())
                output[v.id.value - 1] = v;
        dirty = true;
        frame_ready = false;
    }
    PlaybackStatus fail(PlaybackStatus status) noexcept
    {
        faulted = true;
        frame_ready = false;
        return status;
    }
};
PlaybackInstance::PlaybackInstance(const PlaybackProgram& program, AnimationConfig animation)
    : impl_(std::make_unique<Impl>(*program.impl_, animation))
{
}
PlaybackInstance::~PlaybackInstance() = default;
PlaybackStatus PlaybackInstance::transition(ParamId id, float target, float frames,
                                            float easing) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {PlaybackError::Faulted, {}, {}, {}};
    const auto status = data.animation.transition(id, target, frames, easing);
    if (!status)
        return {PlaybackError::Animation, status, {}, {}};
    data.compose();
    return {};
}
PlaybackStatus PlaybackInstance::transition_difference(VariableSourceId source, ParamId id,
                                                       float target, float frames,
                                                       float easing) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {PlaybackError::Faulted, {}, {}, {}};
    const auto status = data.animation.transition_difference(source, id, target, frames, easing);
    if (!status)
        return {PlaybackError::Animation, status, {}, {}};
    data.compose();
    return {};
}
float PlaybackInstance::difference_value(VariableSourceId source, ParamId id) const noexcept
{
    return impl_->faulted ? 0 : impl_->animation.difference_value(source, id);
}
PlaybackStatus PlaybackInstance::play(TimelineId id, TimelinePlayMode mode) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {PlaybackError::Faulted, {}, {}, {}};
    const auto status = data.animation.play(id, mode);
    if (!status)
        return data.fail({PlaybackError::Animation, status, {}, {}});
    data.compose();
    return {};
}
PlaybackStatus PlaybackInstance::stop(TimelineId id) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {PlaybackError::Faulted, {}, {}, {}};
    const auto status = data.animation.stop(id);
    if (!status)
        return {PlaybackError::Animation, status, {}, {}};
    data.compose();
    return {};
}
bool PlaybackInstance::is_playing(TimelineId id) const noexcept
{
    return !impl_->faulted && impl_->animation.is_playing(id);
}
PlaybackStatus PlaybackInstance::set_timeline_weight(TimelineId id, float target, float frames,
                                                     float easing,
                                                     TimelineWeightCompletion completion) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {PlaybackError::Faulted, {}, {}, {}};
    const auto status = data.animation.set_timeline_weight(id, target, frames, easing, completion);
    if (!status)
        return {PlaybackError::Animation, status, {}, {}};
    return {};
}
float PlaybackInstance::timeline_weight(TimelineId id) const noexcept
{
    return impl_->faulted ? 0 : impl_->animation.timeline_weight(id);
}
PlaybackStatus PlaybackInstance::fade_in(TimelineId id, float frames, float easing) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {PlaybackError::Faulted, {}, {}, {}};
    const auto status = data.animation.fade_in(id, frames, easing);
    if (!status)
        return data.fail({PlaybackError::Animation, status, {}, {}});
    data.compose();
    return {};
}
PlaybackStatus PlaybackInstance::fade_out(TimelineId id, float frames, float easing) noexcept
{
    return set_timeline_weight(id, 0, frames, easing, TimelineWeightCompletion::Stop);
}
PlaybackStatus PlaybackInstance::set_origin(Vec2 origin) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {PlaybackError::Faulted, {}, {}, {}};
    const auto status = data.geometry.set_origin(origin);
    if (!status)
        return {PlaybackError::Geometry, {}, status, {}};
    data.dirty = true;
    data.frame_ready = false;
    return {};
}
PlaybackStatus PlaybackInstance::set_scale(float scale) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {PlaybackError::Faulted, {}, {}, {}};
    const auto status = data.geometry.set_scale(scale);
    if (!status)
        return {PlaybackError::Geometry, {}, status, {}};
    data.dirty = true;
    data.frame_ready = false;
    return {};
}
PlaybackStatus PlaybackInstance::step(PhysicsInputs inputs) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {PlaybackError::Faulted, {}, {}, {}};
    if (data.state == PlaybackState::Paused)
        return {};
    const auto animated = data.animation.step();
    if (!animated)
        return data.fail({PlaybackError::Animation, animated, {}, {}});
    for (const auto v : data.animation.values())
        if (const auto status = data.geometry.set_parameter(v.id, v.value); !status)
            return data.fail({PlaybackError::Geometry, {}, status, {}});
    if (data.physics)
    {
        if (const auto status = data.geometry.evaluate(); !status)
            return data.fail({PlaybackError::Geometry, {}, status, {}});
        if (const auto status = data.physics->step(data.geometry, inputs); !status)
            return data.fail({PlaybackError::Physics, {}, {}, status});
    }
    data.compose();
    ++data.ticks;
    return {};
}
PlaybackStatus PlaybackInstance::advance(double elapsed_seconds, PhysicsInputs inputs,
                                         AuthorTickHandler handler) noexcept
{
    if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0)
        return {PlaybackError::InvalidElapsed, {}, {}, {}};
    auto& data = *impl_;
    if (data.faulted)
        return {PlaybackError::Faulted, {}, {}, {}};
    if (data.state == PlaybackState::Paused)
        return {};
    data.accumulator +=
        std::min(elapsed_seconds, static_cast<double>(kMaximumCatchUpTicks) / kAuthorHz);
    const auto count = std::min(kMaximumCatchUpTicks,
                                static_cast<std::uint32_t>(std::floor(data.accumulator * kAuthorHz +
                                                                      kClockBoundaryTolerance)));
    data.accumulator -= static_cast<double>(count) / kAuthorHz;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (handler.before_tick)
            if (const auto status = handler.before_tick(handler.context, data.ticks + 1); !status)
                return data.fail(status);
        if (const auto status = step(inputs); !status)
            return status;
    }
    return {};
}
void PlaybackInstance::set_playback_state(PlaybackState state) noexcept
{
    impl_->state = state;
    impl_->accumulator = 0;
}
void PlaybackInstance::reset() noexcept
{
    auto& data = *impl_;
    data.animation.reset();
    if (data.physics)
        data.physics->reset();
    data.faulted = false;
    data.accumulator = 0;
    data.ticks = 0;
    data.state = PlaybackState::Running;
    data.compose();
}
PlaybackStatus PlaybackInstance::prepare_frame() noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {PlaybackError::Faulted, {}, {}, {}};
    if (!data.dirty && data.frame_ready)
        return {};
    for (const auto v : data.output)
        if (const auto status = data.geometry.set_parameter(v.id, v.value); !status)
            return data.fail({PlaybackError::Geometry, {}, status, {}});
    if (const auto status = data.geometry.evaluate(); !status)
        return data.fail({PlaybackError::Geometry, {}, status, {}});
    if (const auto status = data.geometry.build_frame(); !status)
        return data.fail({PlaybackError::Geometry, {}, status, {}});
    data.dirty = false;
    data.frame_ready = true;
    return {};
}
std::span<const ParameterValue> PlaybackInstance::values() const noexcept
{
    return impl_->faulted ? std::span<const ParameterValue>{}
                          : std::span<const ParameterValue>(impl_->output);
}
RenderFrame PlaybackInstance::frame() const noexcept
{
    return impl_->frame_ready && !impl_->faulted ? impl_->geometry.frame() : RenderFrame{};
}
std::uint64_t PlaybackInstance::tick_count() const noexcept
{
    return impl_->ticks;
}
std::span<const GeometryPrimitive> PlaybackInstance::frame_primitives() const noexcept
{
    return impl_->frame_ready && !impl_->faulted ? impl_->geometry.primitives()
                                                 : std::span<const GeometryPrimitive>{};
}
std::optional<GeometryNodeObservation> PlaybackInstance::observe_node(NodeId id) const noexcept
{
    if (!impl_->frame_ready || impl_->faulted)
        return std::nullopt;
    return impl_->geometry.observe_node(id);
}
std::optional<SpringObservation> PlaybackInstance::observe_spring(std::size_t index) const noexcept
{
    if (impl_->faulted || !impl_->physics)
        return std::nullopt;
    return impl_->physics->observe_spring(index);
}
} // namespace nekomote::runtime
