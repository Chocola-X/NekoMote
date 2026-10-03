#pragma once

#include <cmath>

namespace nekomote
{

// Time conventions (authoritative: docs/conventions.md):
//
//   - Animation advances on a monotonic clock measured in seconds (Seconds).
//   - E-mote internally uses a 60-frame base; runtime converts frame counts to
//     Seconds at the boundary. The 60-frame constant lives in runtime, not here.
//   - Physics may run on an independent fixed step (FixedStep); decoupled from
//     display FPS. FixedStep is a positive duration, not a rate.
//   - Display FPS is a host concern; the core never assumes a fixed display rate.
//
// Alpha convention:
//
//   - Alpha is linear [0,1] float. 0 = fully transparent, 1 = fully opaque.
//   - Premultiplied alpha is applied at the render_gl boundary; the core stores
//     straight alpha. Mixing straight and premul is a render_gl error.
//
// Angle convention:
//
//   - Radians throughout the core. Degrees only at import/host boundaries,
//     converted immediately to Radians.

struct Seconds
{
    float value{0.0f};

    constexpr Seconds() noexcept = default;
    explicit constexpr Seconds(float v) noexcept : value(v)
    {
    }

    constexpr Seconds operator+(Seconds o) const noexcept
    {
        return Seconds{value + o.value};
    }
    constexpr Seconds operator-(Seconds o) const noexcept
    {
        return Seconds{value - o.value};
    }
    constexpr Seconds operator*(float s) const noexcept
    {
        return Seconds{value * s};
    }
    constexpr Seconds operator/(float s) const noexcept
    {
        return Seconds{value / s};
    }

    constexpr bool operator==(Seconds o) const noexcept
    {
        return value == o.value;
    }
    constexpr bool operator<(Seconds o) const noexcept
    {
        return value < o.value;
    }
};

struct FixedStep
{
    float value{0.0f}; // positive duration in seconds

    constexpr FixedStep() noexcept = default;
    explicit constexpr FixedStep(float v) noexcept : value(v)
    {
    }

    constexpr bool valid() const noexcept
    {
        return value > 0.0f;
    }
    constexpr bool operator==(FixedStep o) const noexcept
    {
        return value == o.value;
    }
};

struct Alpha
{
    float value{0.0f}; // [0,1]

    constexpr Alpha() noexcept = default;
    explicit constexpr Alpha(float v) noexcept : value(v)
    {
    }

    constexpr bool operator==(Alpha o) const noexcept
    {
        return value == o.value;
    }
    constexpr Alpha operator*(Alpha o) const noexcept
    {
        return Alpha{value * o.value};
    }
};

struct Radians
{
    float value{0.0f};

    constexpr Radians() noexcept = default;
    explicit constexpr Radians(float v) noexcept : value(v)
    {
    }

    constexpr bool operator==(Radians o) const noexcept
    {
        return value == o.value;
    }
};

constexpr Radians degrees_to_radians(float deg) noexcept
{
    return Radians{deg * (3.14159265358979323846f / 180.0f)};
}

} // namespace nekomote
