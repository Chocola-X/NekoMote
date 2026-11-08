#pragma once

#include <array>
#include <cstddef>

namespace nekomote::runtime::detail
{
struct PatchControl
{
    double x{0}, y{0};
};
using PreparedPatch = std::array<PatchControl, 16>;

inline std::array<double, 4> bernstein(double t) noexcept
{
    const double s = 1 - t;
    return {s * s * s, 3 * t * s * s, 3 * t * t * s, t * t * t};
}

// Keep the same row-major double accumulation for stored float controls and
// controls promoted once by scene evaluation. Reassociation changes saved bits.
template <typename Point>
std::array<double, 2> evaluate_precise(const std::array<Point, 16>& patch, double u,
                                       double v) noexcept
{
    const auto bx = bernstein(u), by = bernstein(v);
    std::array<double, 2> result{};
    for (std::size_t y = 0; y < 4; ++y)
        for (std::size_t x = 0; x < 4; ++x)
        {
            const double weight = bx[x] * by[y];
            result[0] += static_cast<double>(patch[y * 4 + x].x) * weight;
            result[1] += static_cast<double>(patch[y * 4 + x].y) * weight;
        }
    return result;
}
} // namespace nekomote::runtime::detail
