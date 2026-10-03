#pragma once

#include <nekomote/math.hpp>

#include <array>

namespace nekomote::runtime
{
using Patch = std::array<Vec2, 16>;
struct PatchDomain
{
    Vec2 size, origin;
};
// Row-major 4x4 normalized control points. Evaluation extrapolates polynomially
// outside [0,1]; do not clamp points at the domain's edge.
Patch identity_patch() noexcept;
Vec2 evaluate_patch(const Patch& patch, Vec2 uv) noexcept;
Vec2 deform_point(const Patch& patch, PatchDomain domain, Vec2 point) noexcept;
// Same-domain, zero-transform helper: add displacement relative to identity.
// Independent nested domains are applied from child to parent. Evidence and
// scope restrictions: docs/semantics.md (P3 experiments).
void add_patch_displacement(Patch& destination, const Patch& helper) noexcept;
// Symmetric type=0 paired control. Input and output are in variable units;
// extent must be positive and finite. Inputs clamp to [-extent,+extent].
// This maps the pair radially, preserving direction. Physics type=1 is pending.
Vec2 clamp_pair(Vec2 input, float extent) noexcept;
} // namespace nekomote::runtime
