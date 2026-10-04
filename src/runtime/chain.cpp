#include <nekomote/runtime/chain.hpp>

#include <cmath>
#include <numbers>

namespace nekomote::runtime
{
namespace
{
using Point = std::array<double, 2>;
Point point(Vec2 value) noexcept
{
    return {value.x, value.y};
}
Vec2 stored(Point value) noexcept
{
    return {static_cast<float>(value[0]), static_cast<float>(value[1])};
}
double length(Vec2 value) noexcept
{
    const double x = value.x, y = value.y;
    return std::sqrt(x * x + y * y);
}
Vec2 normalized(Vec2 value, double distance) noexcept
{
    return {static_cast<float>(static_cast<double>(value.x) / distance),
            static_cast<float>(static_cast<double>(value.y) / distance)};
}
Point integrate(Vec2 position, Vec2& velocity, const ChainCoefficients& coefficients) noexcept
{
    // The saved gravity/friction velocity and the transient value have different
    // precision. Position consumes the transient product, not the stored velocity.
    const double acceleration = coefficients.gravity;
    const Point friction = point(coefficients.friction);
    const Point gravity{velocity.x, static_cast<double>(velocity.y) + acceleration};
    const Point damping{gravity[0] * (1.0 - friction[0]), gravity[1] * (1.0 - friction[1])};
    velocity = stored(damping);
    return {static_cast<double>(position.x) + damping[0],
            static_cast<double>(position.y) + damping[1]};
}
} // namespace
ChainDeformation advance_chain(ChainState& state, const ChainCoefficients& coefficients,
                               const ChainRest& rest, Vec2 anchor_delta,
                               Vec2 external_displacement) noexcept
{
    const bool advance = state.initialized;
    const std::array<double, 2> lengths{coefficients.length[0], coefficients.length[1]};
    const Point delta = point(anchor_delta), outer = point(external_displacement);
    if (advance)
        state.anchor_offset =
            stored({static_cast<double>(state.anchor_offset.x) + delta[0] + outer[0],
                    static_cast<double>(state.anchor_offset.y) + delta[1] + outer[1]});
    std::array<Vec2, 2> current_rest;
    for (std::size_t i = 0; i < current_rest.size(); ++i)
        current_rest[i] = rest.position[i] + state.anchor_offset;
    auto raw_root = point(state.position[0]);
    const Vec2 root_direction = state.anchor_offset - state.position[0];
    const double root_distance = length(root_direction);
    if (advance)
    {
        if (root_distance > lengths[0])
        {
            const auto unit = point(normalized(root_direction, root_distance));
            const double force =
                (root_distance - lengths[0]) * static_cast<double>(coefficients.stretch_stiffness);
            state.velocity[0] =
                stored({static_cast<double>(state.velocity[0].x) + unit[0] * force,
                        static_cast<double>(state.velocity[0].y) + unit[1] * force});
        }
        raw_root = integrate(state.position[0], state.velocity[0], coefficients);
        state.position[0] = stored(raw_root);
    }
    const Vec2 tip_direction = state.position[0] - state.position[1];
    const double tip_distance = length(tip_direction);
    if (tip_distance > lengths[1])
    {
        const auto unit = point(normalized(tip_direction, tip_distance));
        state.position[1] =
            stored({static_cast<double>(state.position[0].x) - unit[0] * lengths[1],
                    static_cast<double>(state.position[0].y) - unit[1] * lengths[1]});
        if (advance)
        {
            const double normal_velocity = static_cast<double>(state.velocity[1].x) * unit[0] +
                                           static_cast<double>(state.velocity[1].y) * unit[1];
            const double correction =
                -normal_velocity * static_cast<double>(coefficients.normal_velocity_damping);
            state.velocity[1] =
                stored({static_cast<double>(state.velocity[1].x) + unit[0] * correction,
                        static_cast<double>(state.velocity[1].y) + unit[1] * correction});
        }
    }
    auto raw_tip = point(state.position[1]);
    if (advance)
    {
        raw_tip = integrate(state.position[1], state.velocity[1], coefficients);
        state.position[1] = stored(raw_tip);
    }
    state.initialized = true;
    return {{{stored({raw_root[0] - static_cast<double>(current_rest[0].x),
                      raw_root[1] - static_cast<double>(current_rest[0].y)}),
              stored({raw_tip[0] - static_cast<double>(current_rest[1].x),
                      raw_tip[1] - static_cast<double>(current_rest[1].y)})}}};
}
ChainVariables unbent_chain_output(const ChainDeformation& deformation,
                                   const ChainOutput& mapping) noexcept
{
    constexpr double kSlope = 0.045160394715788144;
    constexpr double kExtent = 80.0 / std::numbers::pi;
    const auto& root = deformation.displacement[0];
    const auto& tip = deformation.displacement[1];
    const double amplitude = mapping.amplitude;
    const double offset = mapping.vertical_offset, scale_y = mapping.root_scale_y;
    const std::array<double, 2> scale_x{mapping.scale_x[0], mapping.scale_x[1]};
    return {static_cast<float>(
                std::atan(static_cast<double>(root.x) * amplitude * scale_x[0] * kSlope) * kExtent),
            static_cast<float>(
                std::atan((static_cast<double>(root.y) + offset) * amplitude * scale_y * kSlope) *
                kExtent),
            static_cast<float>(
                std::atan(static_cast<double>(tip.x) * amplitude * scale_x[1] * kSlope) * kExtent)};
}
} // namespace nekomote::runtime
