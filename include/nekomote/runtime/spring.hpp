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
    // Float32 particle state in initial-anchor author space, Y down.
    // Velocity is author units per 60 Hz author step. Instance-owned.
    std::array<float, 2> position{}, velocity{};
    Vec2 origin_offset{}, anchor_offset{};
    bool initialized{false};
};
struct SpringDeformation
{
    // Displacement uses this step's transient doubles, before particle storage.
    std::array<double, 2> displacement{};
};
struct SpringOutput
{
    Vec2 axis_scale{1, 1};
    float vertical_offset{0}, amplitude{1};
};
// One author tick. Finite coefficients, stiffness/gravity >= 0, friction in [0,1].
// Absolute anchor and persistent external displacement are author units. First
// call initializes the negative anchor offset without advancing the snapshot.
// Subsequent calls accumulate external position disturbance in that Float32 offset.
// Source observations archived in project history.
SpringDeformation advance_spring(SpringState& state, SpringCoefficients coefficients,
                                 Vec2 anchor_position, Vec2 outer_displacement) noexcept;
// Verified paired scalar map and dead zone. Amplitude scales particle position
// before subtracting the saved vertical offset; zero amplitude isn't disabling.
// Inputs must be finite, amplitude >= 0. No geometry or implicit controller IDs.
Vec2 spring_output(const SpringDeformation& deformation, SpringOutput output) noexcept;
} // namespace nekomote::runtime
