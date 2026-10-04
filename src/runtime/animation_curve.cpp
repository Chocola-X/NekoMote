#include <nekomote/runtime/animation_curve.hpp>

#include <algorithm>
#include <cmath>

namespace nekomote::runtime
{
double ease_progress(double progress, float amount) noexcept
{
    const double t = std::clamp(static_cast<double>(progress), 0.0, 1.0);
    if (t == 0 || t == 1 || amount == 0)
        return t;
    const double easing = amount;
    const double exponent = easing > 0 ? 1 + easing : 1 / (1 - easing);
    return std::pow(t, exponent);
}
} // namespace nekomote::runtime
