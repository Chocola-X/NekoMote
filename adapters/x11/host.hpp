#pragma once

#include <memory>

#include "options.hpp"

namespace nekomote::x11
{
// One thread owns the X11 connection, GLX context and wallpaper ABI instance.
// Destruction releases the wallpaper before the context and native output target.
class Host
{
  public:
    explicit Host(Options options);
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    void run();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

void request_stop() noexcept;
} // namespace nekomote::x11
