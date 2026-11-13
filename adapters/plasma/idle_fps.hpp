#pragma once

namespace nekomote::plasma
{
inline int idle_fps_limit(int configured, bool idle)
{
    constexpr int kIdleFps = 30;
    if (configured <= kIdleFps)
        return configured;
    return idle ? kIdleFps : configured;
}
} // namespace nekomote::plasma
