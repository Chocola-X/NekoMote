#pragma once

#include <nekomote/math.hpp>

#include <array>

namespace nekomote::runtime
{
using Patch = std::array<Vec2, 16>;
// Row-major 4x4 control points in a caller-defined coordinate space.
// Scene evaluation compiles controls into placed world coordinates.
// Evaluation extrapolates polynomially
// outside [0,1]; do not clamp points at the domain's edge.
Patch identity_patch() noexcept;
Vec2 evaluate_patch(const Patch& patch, Vec2 uv) noexcept;
// Same-domain, zero-transform helper: add displacement relative to identity.
// Independent nested domains are applied from child to parent. Evidence and
// scope restrictions: docs/semantics.md (P3 experiments).
void add_patch_displacement(Patch& destination, const Patch& helper) noexcept;
// Symmetric type=0 paired control. Input and output are in variable units;
// extent must be positive and finite. Inputs clamp to [-extent,+extent].
// This maps the pair radially, preserving direction.
Vec2 clamp_pair(Vec2 input, float extent) noexcept;
// Type-one hard circular limit. Preserve both components inside the radius;
// outside, normalize the original vector, without first clamping either axis.
// Finite input and a positive finite extent are required.
Vec2 clamp_circle(Vec2 input, float extent) noexcept;
} // namespace nekomote::runtime
