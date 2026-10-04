#include <nekomote/runtime/bend.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nekomote::runtime
{
namespace
{
constexpr float kBendVerticalThreshold = 28;
constexpr float kBendGainStep = 1.0f / 32;
constexpr float kBendPeriod = 2 * std::numbers::pi_v<float>;
constexpr double kChainDeadZone = 0.01;
} // namespace
ChainVariables advance_bend(BendState& state, BendCoefficients coefficients,
                            ChainVariables unbent) noexcept
{
    state.gain = std::abs(unbent.vertical) > kBendVerticalThreshold
                     ? std::min(1.0f, state.gain + kBendGainStep)
                     : std::max(0.0f, state.gain - kBendGainStep);
    double phase = static_cast<double>(state.phase) +
                   static_cast<double>(coefficients.speed) * static_cast<double>(state.gain);
    // Within one period the source feeds the double increment directly to sin,
    // then stores Float32. On wrapping it rounds before taking the remainder.
    // Negative input retains the sign of the remainder; do not remap to [0,2pi).
    if (std::abs(phase) >= static_cast<double>(kBendPeriod))
        phase = static_cast<double>(std::fmod(static_cast<float>(phase), kBendPeriod));
    const double bend = std::sin(phase) * static_cast<double>(coefficients.volume) *
                        static_cast<double>(state.gain);
    state.phase = static_cast<float>(phase);
    const double root = static_cast<double>(unbent.root_x) - bend;
    const double tip = static_cast<double>(unbent.tip_x) + bend;
    if (std::max({std::abs(root), std::abs(static_cast<double>(unbent.vertical)), std::abs(tip)}) <
        kChainDeadZone)
        return {};
    return {static_cast<float>(root), unbent.vertical, static_cast<float>(tip)};
}
} // namespace nekomote::runtime
