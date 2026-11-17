#pragma once

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

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nekomote::host_session
