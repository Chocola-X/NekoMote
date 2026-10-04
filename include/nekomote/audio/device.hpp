#pragma once

#include <nekomote/application/session.hpp>
#include <nekomote/model/audio_clip.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace nekomote::audio
{
enum class DeviceState
{
    Connecting,
    Ready,
    Playing,
    Paused,
    Failed
};
struct Snapshot
{
    DeviceState state{DeviceState::Connecting};
    float rms{0};
    int error{0}; // PulseAudio error code; 0 means no device failure.
};
// One producer (session/render thread), one device worker. Clip index+1 is
// AudioClipId. Owns decoded clips and PulseAudio context/streams. Construction
// validates/loading only; snapshot/submit never block or allocate. Destruction
// joins the worker; call at host shutdown, outside the frame loop.
class Device
{
  public:
    explicit Device(std::vector<model::AudioClip> clips);
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    bool submit(application::AudioCommand command) noexcept;
    Snapshot snapshot() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nekomote::audio
