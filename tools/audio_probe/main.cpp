#include <nekomote/asset_io/audio.hpp>
#include <nekomote/audio/device.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace
{
using namespace nekomote;
using Clock = std::chrono::steady_clock;
void wait_for(audio::Device& device, audio::DeviceState wanted)
{
    const auto deadline = Clock::now() + std::chrono::seconds(6);
    while (Clock::now() < deadline)
    {
        const auto value = device.snapshot();
        if (value.state == audio::DeviceState::Failed)
            throw std::runtime_error("device error " + std::to_string(value.error));
        if (value.state == wanted)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("device state timeout");
}
void send(audio::Device& device, application::AudioCommandKind kind, AudioClipId id = {})
{
    if (!device.submit({kind, id}))
        throw std::runtime_error("command rejected");
}
void verify_playback(audio::Device& device, const std::string& csv_path)
{
    wait_for(device, audio::DeviceState::Ready);
    if (device.submit({application::AudioCommandKind::Play, AudioClipId{2}}))
        throw std::runtime_error("invalid clip accepted");
    // Pause before Play must also pause the newly connecting stream.
    send(device, application::AudioCommandKind::Pause);
    send(device, application::AudioCommandKind::Play, AudioClipId{1});
    wait_for(device, audio::DeviceState::Paused);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (device.snapshot().rms != 0)
        throw std::runtime_error("paused RMS not zero");
    send(device, application::AudioCommandKind::Resume);
    wait_for(device, audio::DeviceState::Playing);
    std::ofstream out(csv_path);
    if (!out)
        throw std::runtime_error("cannot open CSV");
    out << "elapsed_ms,state,rms\n";
    const auto begin = Clock::now();
    float maximum = 0;
    while (Clock::now() - begin < std::chrono::milliseconds(1200))
    {
        const auto s = device.snapshot();
        if (s.state == audio::DeviceState::Failed)
            throw std::runtime_error("device failed during playback");
        maximum = std::max(maximum, s.rms);
        out << std::chrono::duration<double, std::milli>(Clock::now() - begin).count() << ','
            << static_cast<int>(s.state) << ',' << s.rms << '\n';
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    send(device, application::AudioCommandKind::Stop);
    wait_for(device, audio::DeviceState::Ready);
    if (device.snapshot().rms != 0)
        throw std::runtime_error("stopped RMS not zero");
    if (maximum <= 0)
        throw std::runtime_error("no actual PCM RMS observed in first 1.2 seconds");
    send(device, application::AudioCommandKind::Play, AudioClipId{1});
    wait_for(device, audio::DeviceState::Playing);
    send(device, application::AudioCommandKind::Pause);
    wait_for(device, audio::DeviceState::Paused);
    send(device, application::AudioCommandKind::Resume);
    wait_for(device, audio::DeviceState::Playing);
    send(device, application::AudioCommandKind::Stop);
    wait_for(device, audio::DeviceState::Ready);
    std::printf("Device playback/pause/resume/stop/replay passed, actual RMS max %.6f\n",
                static_cast<double>(maximum));
}
int run(int argc, char** argv)
{
    if (argc != 3)
        throw std::runtime_error("usage: nekomote_audio_probe VOICE_FLAC OUTPUT_CSV");
    auto loaded = asset_io::load_flac(argv[1]);
    if (!loaded.ok)
        throw std::runtime_error(loaded.error);
    model::AudioBank clips;
    clips.push_back(std::move(loaded.clip));
    auto bank = std::make_shared<const model::AudioBank>(std::move(clips));
    {
        audio::Device device(bank);
        verify_playback(device, argv[2]);
    }
    // Recreate from the host's retained bank, as after unmuting. Then drop
    // the host owner: playback must retain PCM until the new worker joins.
    audio::Device recreated(bank);
    bank.reset();
    verify_playback(recreated, std::string(argv[2]) + ".recreated.csv");
    std::printf("Device recreation and playback without host bank owner passed\n");
    return 0;
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        return run(argc, argv);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "audio probe: %s\n", e.what());
        return 1;
    }
}
