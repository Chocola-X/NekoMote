#pragma once

namespace nekomote::runtime
{
// Scalar transition curve observed through the original SetVariable API.
// Input is normalized progress, not seconds or authored timeline frames.
// Finite inputs are required. Progress clamps to [0,1]; endpoints are exact.
// Positive amount uses exponent 1+amount; negative uses 1/(1-amount).
// This does not interpret timeline keys, loops, variable graphs or blend order.
float ease_progress(float progress, float amount) noexcept;
} // namespace nekomote::runtime
