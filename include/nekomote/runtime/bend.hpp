#pragma once

namespace nekomote::runtime
{
// Already mapped physical variables in author parameter units. Amplitude and
// axis scales are applied by the particle mapping before this output stage.
struct ChainVariables
{
    float root_x{0}, vertical{0}, tip_x{0};
};
struct BendCoefficients
{
    float speed{0}, volume{0};
};
struct BendState
{
    float phase{0}, gain{0};
};
// Advances one 60 Hz author step. Inputs/state are finite; coefficients are
// nonnegative, gain is in [0,1]. The owning controller validates these at load.
// Scope: output bending, not particle integration or locator initialization.
// Source evidence and numerical reference: docs/p6-physics-investigation.md.
ChainVariables advance_bend(BendState& state, BendCoefficients coefficients,
                            ChainVariables unbent) noexcept;
} // namespace nekomote::runtime
