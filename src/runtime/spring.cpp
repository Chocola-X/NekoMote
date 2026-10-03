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
constexpr double kSpringOutputExtent = 80 / std::numbers::pi;
constexpr double kSpringDeadZone = 0.01;
} // namespace
void advance_spring(SpringState& state, SpringCoefficients coefficients, Vec2 anchor_delta,
                    Vec2 outer_displacement) noexcept
{
    state.position[0] -=
        static_cast<double>(anchor_delta.x) + static_cast<double>(outer_displacement.x);
    state.position[1] -=
        static_cast<double>(anchor_delta.y) + static_cast<double>(outer_displacement.y);
    const double retained = 1 - static_cast<double>(coefficients.friction);
    state.velocity[0] =
        (state.velocity[0] - static_cast<double>(coefficients.stiffness) * state.position[0]) *
        retained;
    state.velocity[1] =
        (state.velocity[1] - static_cast<double>(coefficients.stiffness) * state.position[1] +
         static_cast<double>(coefficients.gravity)) *
        retained;
    state.position[0] += state.velocity[0];
    state.position[1] += state.velocity[1];
}
Vec2 spring_output(const SpringState& state, SpringOutput output) noexcept
{
    const double x = std::atan(state.position[0] * static_cast<double>(output.amplitude) *
                               static_cast<double>(output.axis_scale.x) * kSpringSaturationSlope) *
                     kSpringOutputExtent;
    const double y = std::atan((state.position[1] * static_cast<double>(output.amplitude) -
                                static_cast<double>(output.vertical_offset)) *
                               static_cast<double>(output.axis_scale.y) * kSpringSaturationSlope) *
                     kSpringOutputExtent;
    if (std::max(std::abs(x), std::abs(y)) < kSpringDeadZone)
        return {};
    return {static_cast<float>(x), static_cast<float>(y)};
}
} // namespace nekomote::runtime
