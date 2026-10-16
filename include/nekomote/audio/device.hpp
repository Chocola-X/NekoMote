#pragma once

#include <nekomote/application/session.hpp>
#include <nekomote/model/audio_clip.hpp>

#include <cstdint>
#include <memory>

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
// AudioClipId. Retains a non-null immutable bank shared with its host; never
// copies PCM/RMS. The caller must publish a completed bank without mutable
// aliases. Owns its PulseAudio context/streams. Construction validates the bank;
// snapshot/submit never block or allocate or change bank ownership. Destruction
// joins the worker before releasing its bank. Device recreation may reuse it.
class Device
{
  public:
    explicit Device(std::shared_ptr<const model::AudioBank> clips);
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
