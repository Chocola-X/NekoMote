#pragma once

#include <cmath>
#include <cstdint>

namespace nekomote::application
{
// Counts rendered frames against monotonic wall time, independently of animation
// delta clamping and the configured FPS limit. Not a scanout/presentation metric.
class FrameRate
{
  public:
    void reset() noexcept
    {
        *this = {};
    }

    double observe(double now) noexcept
    {
        if (!std::isfinite(now))
        {
            reset();
            return 0;
        }
        if (!started_ || now < last_ || now - last_ > 1.0)
        {
            started_ = true;
            start_ = last_ = now;
            frames_ = 0;
            value_ = 0;
            return value_;
        }
        last_ = now;
        ++frames_;
        if (now - start_ >= 0.25)
        {
            value_ = static_cast<double>(frames_) / (now - start_);
            start_ = now;
            frames_ = 0;
        }
        return value_;
    }

  private:
    bool started_{false};
    double start_{0}, last_{0}, value_{0};
    std::uint64_t frames_{0};
};
} // namespace nekomote::application
