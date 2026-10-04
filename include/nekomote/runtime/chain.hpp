#pragma once

#include <nekomote/math.hpp>
#include <nekomote/runtime/bend.hpp>

#include <array>

namespace nekomote::runtime
{
struct ChainCoefficients
{
    float stretch_stiffness{0}, gravity{0};
    Vec2 friction{}; // horizontal/vertical, not particle indices
    std::array<float, 2> length{};
    float normal_velocity_damping{0}; // 1 removes the normal component; 2 reflects it
};
struct ChainState
{
    // Float32 positions in the initial anchor's author space, Y downward. Anchor
    // movement accumulates separately; rebasing positions each tick changes rounding.
    std::array<Vec2, 2> position{}, velocity{};
    Vec2 anchor_offset{};
    bool initialized{false};
};
struct ChainRest
{
    std::array<Vec2, 2> position{};
};
struct ChainDeformation
{
    // Float32 displacement from the current rest points, calculated from this
    // step's transient positions before saving those positions as Float32.
    std::array<Vec2, 2> displacement{};
};
struct ChainOutput
{
    std::array<float, 2> scale_x{1, 1};
    float root_scale_y{1}, vertical_offset{0}, amplitude{1};
};
// One author tick, no time scaling. First call initializes the anchor, projects
// the tip's maximum length, and skips velocity integration/external displacement.
// Subsequent ticks move the anchor by delta + external displacement (author units).
// Coefficients/rest/state are validated by the caller; lengths must be positive.
// Evidence and remaining binding scope: docs/p6-physics-investigation.md.
ChainDeformation advance_chain(ChainState& state, const ChainCoefficients& coefficients,
                               const ChainRest& rest, Vec2 anchor_delta,
                               Vec2 external_displacement) noexcept;
// Verified vertical_effect=0 mapping; the tip contributes horizontal displacement.
// Apply advance_bend afterward, including its final three-component dead zone.
ChainVariables unbent_chain_output(const ChainDeformation& deformation,
                                   const ChainOutput& mapping) noexcept;
} // namespace nekomote::runtime
