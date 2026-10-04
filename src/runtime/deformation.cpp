#include <nekomote/runtime/deformation.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nekomote::runtime
{
namespace
{
std::array<double, 4> bernstein(double t) noexcept
{
    const double s = 1 - t;
    return {s * s * s, 3 * t * s * s, 3 * t * t * s, t * t * t};
}
std::array<double, 2> evaluate_precise(const Patch& patch, double u, double v) noexcept
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
} // namespace
Patch identity_patch() noexcept
{
    Patch result;
    for (std::size_t y = 0; y < 4; ++y)
        for (std::size_t x = 0; x < 4; ++x)
            result[y * 4 + x] = {static_cast<float>(x) / 3, static_cast<float>(y) / 3};
    return result;
}
Vec2 evaluate_patch(const Patch& patch, Vec2 uv) noexcept
{
    const auto result = evaluate_precise(patch, uv.x, uv.y);
    return {static_cast<float>(result[0]), static_cast<float>(result[1])};
}
void add_patch_displacement(Patch& destination, const Patch& helper) noexcept
{
    const auto neutral = identity_patch();
    for (std::size_t i = 0; i < destination.size(); ++i)
        destination[i] += helper[i] - neutral[i];
}
Vec2 clamp_pair(Vec2 input, float extent) noexcept
{
    // Use double intermediates: the reference controller uses double math even
    // though its final pose and vertex buffers contain floats.
    const double limit = extent;
    const double x = std::clamp(static_cast<double>(input.x) / limit, -1.0, 1.0);
    const double y = std::clamp(static_cast<double>(input.y) / limit, -1.0, 1.0);
    const double major = std::max(std::abs(x), std::abs(y));
    if (major == 0)
        return {};
    const double ratio = std::min(std::abs(x), std::abs(y)) / major;
    constexpr double kHalfPi = std::numbers::pi / 2;
    const double radius =
        major + (std::sin(major * kHalfPi) - major) * (1 - std::cos(ratio * kHalfPi));
    const double scale = radius / (major * std::sqrt(1 + ratio * ratio));
    return {static_cast<float>(x * scale * limit), static_cast<float>(y * scale * limit)};
}
Vec2 clamp_circle(Vec2 input, float extent) noexcept
{
    const double x = input.x, y = input.y, radius = extent;
    const double squared = x * x + y * y;
    if (squared <= radius * radius)
        return input;
    const double scale = radius / std::sqrt(squared);
    return {static_cast<float>(x * scale), static_cast<float>(y * scale)};
}
} // namespace nekomote::runtime
