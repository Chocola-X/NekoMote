#pragma once

#include <array>
#include <memory>

namespace nekomote::host_session
{
// Host-only passive session observation. Never requests a lock or treats user
// inactivity as an unavailable output. Unknown capabilities leave playback on.
// Owned and polled by one host thread; no application/model dependency.
class Monitor
{
  public:
    Monitor();
    ~Monitor();
    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;
    // Nonblocking message processing; the host schedules this at 1 Hz.
    // Reports ScreenSaver active or logind preparing-for-sleep/LockedHint.
    bool poll();
    // Cached, initially denied. Requires a verified active, awake session and
    // clear logind LockedHint (advisory; not proof from the compositor itself).
    // A ScreenSaver lock can veto, but an unlock cannot grant permission.
    bool desktop_gaze_allowed() const;
    // Borrowed session/system bus descriptors (-1 when unavailable). Never read or
    // close them. On POLLIN/HUP/ERR call poll(), then refresh both descriptors.
    // Keep periodic poll() calls for pending writes. Name changes are observed
    // on a live bus; after a bus disconnect recreate Monitor to reconnect.
    std::array<int, 2> dbus_fds() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nekomote::host_session
