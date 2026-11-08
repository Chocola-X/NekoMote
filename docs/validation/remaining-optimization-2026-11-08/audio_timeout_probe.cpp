#include <nekomote/audio/device.hpp>

#include <pulse/pulseaudio.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <thread>
int main()
{
    using namespace nekomote;
    model::AudioClip clip;
    clip.sample_rate = 8000;
    clip.channels = 1;
    clip.samples.assign(1024, .25f);
    clip.rms.assign((1024 + model::kAudioRmsFrames - 1) / model::kAudioRmsFrames, .25f);
    model::AudioBank bank;
    bank.push_back(std::move(clip));
    auto data = std::make_shared<const model::AudioBank>(std::move(bank));
    auto start = std::chrono::steady_clock::now();
    auto device = std::make_unique<audio::Device>(data);
    while (device->snapshot().state != audio::DeviceState::Failed &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(7))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto result = device->snapshot();
    if (result.state != audio::DeviceState::Failed || result.error != PA_ERR_TIMEOUT)
        throw std::runtime_error("expected connection deadline");
    if (device->submit({application::AudioCommandKind::Play, AudioClipId{1}}))
        throw std::runtime_error("accepted failed-device command");
    device.reset();
    if (data.use_count() != 1)
        throw std::runtime_error("bank leaked");
    std::printf(
        "connection_timeout_and_join_ms=%.3f error=%d bank_owners=%ld\n",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
        result.error, data.use_count());
}
