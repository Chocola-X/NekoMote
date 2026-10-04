#include <nekomote/runtime/spring.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nekomote::runtime
{
namespace
{
// Identical Math.atan slope observed in four source models, different axis/SDK
// scales and isolated unit outer displacement. Freeze the measured double;
// do not infer it from pixel markers, character height or model bounds.
constexpr double kSpringSaturationSlope = 0.045160394715788144;
// Observed atan result/output ratio uses the stored Float32 pi value.
constexpr double kSpringOutputExtent = 80 / static_cast<double>(std::numbers::pi_v<float>);
constexpr double kSpringDeadZone = 0.01;
} // namespace
SpringDeformation advance_spring(SpringState& state, SpringCoefficients coefficients,
                                 Vec2 anchor_position, Vec2 outer_displacement) noexcept
{
    const bool integrate = state.initialized;
    if (!integrate)
        state.origin_offset = {-anchor_position.x, -anchor_position.y};
    else
        state.origin_offset = {static_cast<float>(static_cast<double>(state.origin_offset.x) +
                                                  static_cast<double>(outer_displacement.x)),
                               static_cast<float>(static_cast<double>(state.origin_offset.y) +
                                                  static_cast<double>(outer_displacement.y))};
    state.anchor_offset = {static_cast<float>(static_cast<double>(anchor_position.x) +
                                              static_cast<double>(state.origin_offset.x)),
                           static_cast<float>(static_cast<double>(anchor_position.y) +
                                              static_cast<double>(state.origin_offset.y))};
    std::array<double, 2> raw{state.position[0], state.position[1]};
    if (integrate)
    {
        const std::array<double, 2> anchor{state.anchor_offset.x, state.anchor_offset.y};
        const double retained = 1.0 - static_cast<double>(coefficients.friction);
        for (std::size_t axis = 0; axis < 2; ++axis)
        {
            const double gravity = axis == 1 ? coefficients.gravity : 0;
            const double velocity =
                (static_cast<double>(state.velocity[axis]) -
                 (raw[axis] - anchor[axis]) * static_cast<double>(coefficients.stiffness) +
                 gravity) *
                retained;
            state.velocity[axis] = static_cast<float>(velocity);
            raw[axis] += velocity;
            state.position[axis] = static_cast<float>(raw[axis]);
        }
    }
    state.initialized = true;
    return {{{raw[0] - static_cast<double>(state.anchor_offset.x),
              raw[1] - static_cast<double>(state.anchor_offset.y)}}};
}
Vec2 spring_output(const SpringDeformation& deformation, SpringOutput output) noexcept
{
    const double x = std::atan(deformation.displacement[0] * static_cast<double>(output.amplitude) *
                               static_cast<double>(output.axis_scale.x) * kSpringSaturationSlope) *
                     kSpringOutputExtent;
    const double y =
        std::atan((deformation.displacement[1] * static_cast<double>(output.amplitude) -
                   static_cast<double>(output.vertical_offset)) *
                  static_cast<double>(output.axis_scale.y) * kSpringSaturationSlope) *
        kSpringOutputExtent;
    if (std::max(std::abs(x), std::abs(y)) < kSpringDeadZone)
        return {};
    return {static_cast<float>(x), static_cast<float>(y)};
}
} // namespace nekomote::runtime
