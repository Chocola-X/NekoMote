#include <nekomote/ui/easing.hpp>

#include <cmath>

namespace nekomote::ui
{

float ease_out_back(float t)
{
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
}

float ease_out_cubic(float t)
{
    return 1.0f - std::pow(1.0f - t, 3.0f);
}

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

} // namespace nekomote::ui
