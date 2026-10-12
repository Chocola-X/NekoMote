#include <nekomote/ui/easing.hpp>

namespace nekomote::ui
{

float ease_out_back(float t)
{
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    const float x = t - 1.0f;
    const float square = x * x;
    const float cube = square * x;
    return 1.0f + c3 * cube + c1 * square;
}

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

} // namespace nekomote::ui
