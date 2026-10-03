#pragma once

#include <nekomote/math.hpp>

#include <array>

namespace nekomote::runtime
{
struct SpringCoefficients
{
    float stiffness{0}, friction{0}, gravity{0};
};
struct SpringState
{
    // Position relative to the current anchor, in author units (Y down).
    // Velocity is author units per 60 Hz author step. No shared instance state.
    std::array<double, 2> position{}, velocity{};
};
struct SpringOutput
{
    Vec2 axis_scale{1, 1};
    float vertical_offset{0}, amplitude{1};
};
// One fixed author step. Coefficients must be finite, stiffness/gravity >= 0,
// friction in [0,1]. Anchor delta and outer displacement are already in author
// units; convert model placement/scale at the scene boundary. Outer displacement
// is a position disturbance, not a force integrated into velocity.
// Initialize the first anchor without advancing the saved particle snapshot.
// Source experiments and limits: docs/p6-physics-investigation.md.
void advance_spring(SpringState& state, SpringCoefficients coefficients, Vec2 anchor_delta,
                    Vec2 outer_displacement) noexcept;
// Verified paired scalar map and dead zone. Amplitude scales particle position
// before subtracting the saved vertical offset; zero amplitude isn't disabling.
// Inputs must be finite, amplitude >= 0. No geometry or implicit controller IDs.
Vec2 spring_output(const SpringState& state, SpringOutput output) noexcept;
} // namespace nekomote::runtime
