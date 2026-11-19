#pragma once

#include <chrono>
#include <cstdlib>
#include <string_view>

namespace nekomote::host_session
{
// DISPLAY and Qt's xcb platform are also present for XWayland clients.
inline bool wayland_session_hint() noexcept
{
    const auto* type = std::getenv("XDG_SESSION_TYPE");
    const auto* socket = std::getenv("WAYLAND_DISPLAY");
    return (type && std::string_view{type} == "wayland") || (socket && *socket);
}

// Reuse the playback schedule. A still cursor needs fewer synchronous X11
// queries; a detected move brings sampling back to roughly 30 Hz.
inline constexpr std::chrono::milliseconds native_gaze_poll_interval(bool moved) noexcept
{
    return std::chrono::milliseconds(moved ? 32 : 64);
}
} // namespace nekomote::host_session
