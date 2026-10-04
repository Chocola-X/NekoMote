#include <nekomote/runtime/chain.hpp>
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
struct ChainBinding
{
    std::string source;
    std::vector<NodeId> anchors;
    std::array<ParamId, 3> outputs;
    model::ChainKind kind;
    ChainCoefficients coefficients;
    ChainRest rest;
    ChainOutput mapping;
    ChainState snapshot;
    BendCoefficients bend_coefficients;
    BendState bend_snapshot;
};
struct BoundChainState
{
    ChainState chain;
    BendState bend;
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
} // namespace
struct PhysicsProgram::Impl
{
    const GeometryScene* scene{nullptr}; // borrowed, for ID ownership validation
    float author_scale{1};
    std::vector<SpringBinding> springs;
    std::vector<ChainBinding> chains;
};
PhysicsProgram::PhysicsProgram(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
}
PhysicsProgram::~PhysicsProgram() = default;
std::size_t PhysicsProgram::controller_count() const noexcept
{
    return impl_->springs.size() + impl_->chains.size();
}
std::string_view PhysicsProgram::controller_source(std::size_t index) const noexcept
{
    if (index < impl_->springs.size())
        return impl_->springs[index].source;
    index -= impl_->springs.size();
    return index < impl_->chains.size() ? impl_->chains[index].source : std::string_view{};
}
PhysicsCompileResult compile_physics(const model::ModelAsset& asset, const GeometryScene& scene)
{
    PhysicsCompileResult result;
    try
    {
        auto data = std::make_unique<PhysicsProgram::Impl>();
        data->scene = &scene;
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
        for (const auto& control : asset.chain_controls)
        {
            const auto& binding = control.binding;
            if (!binding.enabled)
                continue;
            require(asset.author_scale.has_value() && std::isfinite(asset.author_scale.data) &&
                        asset.author_scale.data > 0,
                    "/metadata/scale", "chain requires positive author scale");
            data->author_scale = asset.author_scale.data;
            require(binding.unsupported_fields.empty(), binding.source_path,
                    "unhandled physical fields");
            require(control.kind == model::ChainKind::Hair ||
                        control.kind == model::ChainKind::Parts,
                    binding.source_path, "unknown chain kind");
            require(control.vertical_effect == 0, binding.source_path,
                    "unverified vertical effect");
            require(control.previous_anchor == std::array<float, 3>{}, binding.source_path,
                    "nonzero saved anchor semantics not verified");
            require(std::isfinite(control.stretch_stiffness) && control.stretch_stiffness >= 0 &&
                        std::isfinite(control.gravity) && control.gravity >= 0 &&
                        std::isfinite(control.normal_velocity_damping) &&
                        control.normal_velocity_damping >= 0,
                    binding.source_path, "invalid chain coefficients");
            for (float value : control.friction)
                require(std::isfinite(value) && value >= 0 && value <= 1, binding.source_path,
                        "invalid chain friction");
            for (float value : control.length)
                require(std::isfinite(value) && value > 0, binding.source_path,
                        "invalid chain length");
            require(control.rest_position[0] == std::array<float, 3>{0, control.length[0], 0} &&
                        control.rest_position[1] ==
                            std::array<float, 3>{0, control.length[0] + control.length[1], 0},
                    binding.source_path, "unverified chain rest geometry");
            require(std::isfinite(control.bend_speed) && control.bend_speed >= 0 &&
                        std::isfinite(control.bend_volume) && control.bend_volume >= 0 &&
                        std::isfinite(control.bend_phase) && std::isfinite(control.bend_gain) &&
                        control.bend_gain >= 0 && control.bend_gain <= 1,
                    binding.source_path, "invalid bending snapshot or coefficients");
            ChainBinding chain;
            chain.source = binding.source_path;
            chain.kind = control.kind;
            chain.outputs = {scene.find_variable(control.variable_middle),
                             scene.find_variable(binding.variable_y),
                             scene.find_variable(binding.variable_x)};
            for (const auto id : chain.outputs)
            {
                require(id.valid(), binding.source_path, "unresolved physical output");
                require(std::find(outputs.begin(), outputs.end(), id) == outputs.end(),
                        binding.source_path, "physical outputs overlap");
                outputs.push_back(id);
            }
            for (const auto& locator : scene.locators())
                if (locator.name == binding.anchor)
                    chain.anchors.push_back(locator.node);
            // Source chains whose named locator does not exist still integrate
            // external displacement around a fixed zero anchor.
            chain.coefficients = {control.stretch_stiffness,
                                  control.gravity,
                                  {control.friction[0], control.friction[1]},
                                  control.length,
                                  control.normal_velocity_damping};
            chain.mapping = {control.scale_x, control.scale_y[0], control.vertical_offset, 1};
            for (float value : {control.scale_x[0], control.scale_x[1], control.scale_y[0],
                                control.scale_y[1], control.vertical_offset})
                require(std::isfinite(value), binding.source_path, "nonfinite chain mapping");
            for (std::size_t i = 0; i < 2; ++i)
            {
                require(control.position[i][2] == 0 && control.velocity[i][2] == 0,
                        binding.source_path, "unverified saved chain z");
                for (std::size_t axis = 0; axis < 2; ++axis)
                    require(std::isfinite(control.position[i][axis]) &&
                                std::isfinite(control.velocity[i][axis]),
                            binding.source_path, "nonfinite chain snapshot");
                chain.rest.position[i] = {control.rest_position[i][0], control.rest_position[i][1]};
                chain.snapshot.position[i] = {control.position[i][0], control.position[i][1]};
                chain.snapshot.velocity[i] = {control.velocity[i][0], control.velocity[i][1]};
            }
            chain.bend_coefficients = {control.bend_speed, control.bend_volume};
            chain.bend_snapshot = {control.bend_phase, control.bend_gain};
            data->chains.push_back(std::move(chain));
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
    std::vector<BoundChainState> chain_states, chain_next;
    std::vector<Vec2> anchors;
    std::vector<ParameterValue> output;
    std::uint64_t ticks{0};
    bool has_output{false};
    explicit Impl(const PhysicsProgram::Impl& data)
        : program(data), states(data.springs.size()), next(states.size()),
          chain_states(data.chains.size()), chain_next(chain_states.size()),
          anchors(states.size() + chain_states.size()),
          output(states.size() * 2 + chain_states.size() * 3)
    {
        reset();
    }
    void reset() noexcept
    {
        ticks = 0;
        has_output = false;
        for (std::size_t i = 0; i < states.size(); ++i)
            states[i] = {program.springs[i].snapshot, {}, false};
        for (std::size_t i = 0; i < chain_states.size(); ++i)
            chain_states[i] = {program.chains[i].snapshot, program.chains[i].bend_snapshot};
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
        !finite(inputs.bust_displacement) || !std::isfinite(inputs.hair_amplitude) ||
        inputs.hair_amplitude < 0 || !finite(inputs.hair_displacement) ||
        !std::isfinite(inputs.parts_amplitude) || inputs.parts_amplitude < 0 ||
        !finite(inputs.parts_displacement))
        return {PhysicsError::InvalidInput, kNoPhysicsController, {}};
    // Save the reciprocal of the author and instance scale, then save each
    // absolute anchor before subtraction. Dividing a delta changes its bits.
    // Source observations: docs/validation/p6-transform-precision.json.
    const double physical_scale =
        static_cast<double>(geometry.scale()) * static_cast<double>(program.author_scale);
    const double inverse_scale = static_cast<float>(1.0 / physical_scale);
    if (!std::isfinite(physical_scale) || physical_scale <= 0 || !std::isfinite(inverse_scale) ||
        inverse_scale <= 0)
        return {PhysicsError::InvalidInput, kNoPhysicsController, {}};
    for (std::size_t i = 0; i < instance.anchors.size(); ++i)
    {
        const auto& nodes = i < program.springs.size()
                                ? program.springs[i].anchors
                                : program.chains[i - program.springs.size()].anchors;
        instance.anchors[i] = {};
        if (nodes.empty())
            continue; // verified fixed-zero chain anchor; springs cannot compile without an anchor
        bool active = false;
        for (NodeId node : nodes)
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
            instance.anchors[i] = {
                static_cast<float>(static_cast<double>(sample.position.x) * inverse_scale),
                static_cast<float>(static_cast<double>(sample.position.y) * inverse_scale)};
            if (!finite(instance.anchors[i]))
                return {PhysicsError::InvalidAnchor, i, {}};
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
            const Vec2 delta = instance.anchors[i] - state.previous_anchor;
            if (!finite(delta))
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
    for (std::size_t i = 0; i < program.chains.size(); ++i)
    {
        const auto controller = program.springs.size() + i;
        auto& state = instance.chain_next[i];
        state = instance.chain_states[i];
        const auto& chain = program.chains[i];
        const bool hair = chain.kind == model::ChainKind::Hair;
        const auto displacement = hair ? inputs.hair_displacement : inputs.parts_displacement;
        const auto pose = advance_chain(state.chain, chain.coefficients, chain.rest,
                                        instance.anchors[controller], displacement);
        for (std::size_t particle = 0; particle < 2; ++particle)
            if (!finite(state.chain.position[particle]) || !finite(state.chain.velocity[particle]))
                return {PhysicsError::NonfiniteState, controller, {}};
        if (!finite(state.chain.anchor_offset) || !finite(state.chain.origin_offset))
            return {PhysicsError::NonfiniteState, controller, {}};
        auto mapping = chain.mapping;
        mapping.amplitude = hair ? inputs.hair_amplitude : inputs.parts_amplitude;
        const auto value =
            advance_bend(state.bend, chain.bend_coefficients, unbent_chain_output(pose, mapping));
        const std::array<float, 3> values{value.root_x, value.vertical, value.tip_x};
        if (!std::isfinite(state.bend.phase) || !std::isfinite(state.bend.gain))
            return {PhysicsError::NonfiniteState, controller, {}};
        for (std::size_t component = 0; component < values.size(); ++component)
        {
            if (!std::isfinite(values[component]))
                return {PhysicsError::NonfiniteState, controller, {}};
            instance.output[program.springs.size() * 2 + i * 3 + component] = {
                chain.outputs[component], values[component]};
        }
    }
    instance.states.swap(instance.next);
    instance.chain_states.swap(instance.chain_next);
    ++instance.ticks;
    instance.has_output = true;
    return {};
}
} // namespace nekomote::runtime
