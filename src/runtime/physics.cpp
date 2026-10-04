#include <nekomote/runtime/physics.hpp>
#include <nekomote/runtime/spring.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace nekomote::runtime
{
namespace
{
struct SpringBinding
{
    std::string source;
    std::vector<NodeId> anchors;
    ParamId x, y;
    SpringCoefficients coefficients;
    SpringOutput mapping;
    SpringState snapshot;
};
struct ControllerState
{
    SpringState spring;
    Vec2 previous_anchor{};
    bool initialized{false};
};
void require(bool value, const std::string& path, std::string_view reason)
{
    if (!value)
        throw std::runtime_error(path + ": " + std::string(reason));
}
bool finite(Vec2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}
bool normalized(Vec2 value, float scale, Vec2& result) noexcept
{
    const double x = static_cast<double>(value.x) / static_cast<double>(scale);
    const double y = static_cast<double>(value.y) / static_cast<double>(scale);
    constexpr double maximum = std::numeric_limits<float>::max();
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > maximum || std::abs(y) > maximum)
        return false;
    result = {static_cast<float>(x), static_cast<float>(y)};
    return true;
}
} // namespace
struct PhysicsProgram::Impl
{
    const GeometryScene* scene{nullptr}; // borrowed, for ID ownership validation
    float author_scale{1};
    std::vector<SpringBinding> springs;
};
PhysicsProgram::PhysicsProgram(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
}
PhysicsProgram::~PhysicsProgram() = default;
std::size_t PhysicsProgram::controller_count() const noexcept
{
    return impl_->springs.size();
}
std::string_view PhysicsProgram::controller_source(std::size_t index) const noexcept
{
    return index < impl_->springs.size() ? impl_->springs[index].source : std::string_view{};
}
PhysicsCompileResult compile_physics(const model::ModelAsset& asset, const GeometryScene& scene)
{
    PhysicsCompileResult result;
    try
    {
        auto data = std::make_unique<PhysicsProgram::Impl>();
        data->scene = &scene;
        for (const auto& chain : asset.chain_controls)
            require(!chain.binding.enabled, chain.binding.source_path,
                    "enabled chain integration is not verified");
        std::vector<ParamId> outputs;
        for (const auto& control : asset.spring_controls)
        {
            const auto& binding = control.binding;
            if (!binding.enabled)
                continue;
            require(asset.author_scale.has_value() && std::isfinite(asset.author_scale.data) &&
                        asset.author_scale.data > 0,
                    "/metadata/scale", "spring requires positive author scale");
            data->author_scale = asset.author_scale.data;
            require(binding.unsupported_fields.empty(), binding.source_path,
                    "unhandled physical fields");
            require(std::isfinite(control.stiffness) && control.stiffness >= 0 &&
                        std::isfinite(control.gravity) && control.gravity >= 0 &&
                        std::isfinite(control.friction) && control.friction >= 0 &&
                        control.friction <= 1,
                    binding.source_path, "invalid spring coefficients");
            require(control.previous_anchor == std::array<float, 3>{} && control.position[2] == 0 &&
                        control.velocity[2] == 0,
                    binding.source_path, "nonzero saved anchor or z semantics not verified");
            SpringBinding spring;
            spring.source = binding.source_path;
            spring.x = scene.find_variable(binding.variable_x);
            spring.y = scene.find_variable(binding.variable_y);
            require(spring.x.valid() && spring.y.valid() && spring.x != spring.y,
                    binding.source_path, "unresolved or repeated physical output");
            for (ParamId id : {spring.x, spring.y})
            {
                require(std::find(outputs.begin(), outputs.end(), id) == outputs.end(),
                        binding.source_path, "physical outputs overlap");
                outputs.push_back(id);
            }
            for (const auto& locator : scene.locators())
                if (locator.name == binding.anchor)
                    spring.anchors.push_back(locator.node);
            require(!spring.anchors.empty(), binding.source_path,
                    "unresolved exported physical anchor");
            spring.coefficients = {control.stiffness, control.friction, control.gravity};
            spring.mapping = {
                {control.axis_scale[0], control.axis_scale[1]}, control.vertical_offset, 1};
            require(finite(spring.mapping.axis_scale) && std::isfinite(control.vertical_offset),
                    binding.source_path, "nonfinite physical mapping");
            for (std::size_t axis = 0; axis < 2; ++axis)
            {
                require(std::isfinite(control.position[axis]) &&
                            std::isfinite(control.velocity[axis]),
                        binding.source_path, "nonfinite physical snapshot");
                spring.snapshot.position[axis] = control.position[axis];
                spring.snapshot.velocity[axis] = control.velocity[axis];
            }
            data->springs.push_back(std::move(spring));
        }
        result.program = std::unique_ptr<const PhysicsProgram>(new PhysicsProgram(std::move(data)));
    }
    catch (const std::runtime_error& error)
    {
        result.errors.push_back(error.what());
    }
    return result;
}
struct PhysicsInstance::Impl
{
    const PhysicsProgram::Impl& program;
    std::vector<ControllerState> states, next;
    std::vector<Vec2> anchors;
    std::vector<ParameterValue> output;
    std::uint64_t ticks{0};
    bool has_output{false};
    explicit Impl(const PhysicsProgram::Impl& data)
        : program(data), states(data.springs.size()), next(states.size()), anchors(states.size()),
          output(states.size() * 2)
    {
        reset();
    }
    void reset() noexcept
    {
        ticks = 0;
        has_output = false;
        for (std::size_t i = 0; i < states.size(); ++i)
            states[i] = {program.springs[i].snapshot, {}, false};
    }
};
PhysicsInstance::PhysicsInstance(const PhysicsProgram& program)
    : impl_(std::make_unique<Impl>(*program.impl_))
{
}
PhysicsInstance::~PhysicsInstance() = default;
void PhysicsInstance::reset() noexcept
{
    impl_->reset();
}
std::span<const ParameterValue> PhysicsInstance::values() const noexcept
{
    return impl_->has_output ? std::span<const ParameterValue>(impl_->output)
                             : std::span<const ParameterValue>{};
}
std::uint64_t PhysicsInstance::tick_count() const noexcept
{
    return impl_->ticks;
}
PhysicsStatus PhysicsInstance::step(const GeometryInstance& geometry, PhysicsInputs inputs) noexcept
{
    auto& instance = *impl_;
    instance.has_output = false;
    const auto& program = instance.program;
    if (&geometry.scene() != program.scene)
        return {PhysicsError::ForeignScene, kNoPhysicsController, {}};
    if (!std::isfinite(inputs.bust_amplitude) || inputs.bust_amplitude < 0 ||
        !finite(inputs.bust_displacement))
        return {PhysicsError::InvalidInput, kNoPhysicsController, {}};
    for (std::size_t i = 0; i < program.springs.size(); ++i)
    {
        bool active = false;
        for (NodeId node : program.springs[i].anchors)
        {
            const auto sample = geometry.sample_locator(node);
            if (!sample.status)
                return {PhysicsError::InvalidAnchor, i, sample.status};
            if (sample.state == LocatorState::Inactive)
                continue;
            if (active)
                return {PhysicsError::AmbiguousAnchor, i, {}};
            if (!finite(sample.position))
                return {PhysicsError::InvalidAnchor, i, {}};
            instance.anchors[i] = sample.position;
            active = true;
        }
        if (!active)
            return {PhysicsError::InactiveAnchor, i, {}};
    }
    for (std::size_t i = 0; i < program.springs.size(); ++i)
    {
        auto& state = instance.next[i];
        state = instance.states[i];
        const auto& spring = program.springs[i];
        if (state.initialized)
        {
            Vec2 delta;
            if (!normalized(instance.anchors[i] - state.previous_anchor, program.author_scale,
                            delta))
                return {PhysicsError::InvalidInput, i, {}};
            advance_spring(state.spring, spring.coefficients, delta, inputs.bust_displacement);
        }
        for (std::size_t axis = 0; axis < 2; ++axis)
            if (!std::isfinite(state.spring.position[axis]) ||
                !std::isfinite(state.spring.velocity[axis]))
                return {PhysicsError::NonfiniteState, i, {}};
        state.previous_anchor = instance.anchors[i];
        state.initialized = true;
        auto mapping = spring.mapping;
        mapping.amplitude = inputs.bust_amplitude;
        const auto value = spring_output(state.spring, mapping);
        if (!finite(value))
            return {PhysicsError::NonfiniteState, i, {}};
        instance.output[i * 2] = {spring.x, value.x};
        instance.output[i * 2 + 1] = {spring.y, value.y};
    }
    instance.states.swap(instance.next);
    ++instance.ticks;
    instance.has_output = true;
    return {};
}
} // namespace nekomote::runtime
