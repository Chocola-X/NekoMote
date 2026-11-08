#include <nekomote/audio/device.hpp>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace nekomote;
using Clock = std::chrono::steady_clock;
void require(bool b, const char* text)
{
    if (!b)
        throw std::runtime_error(text);
}
void state(audio::Device& device, audio::DeviceState expected)
{
    auto deadline = Clock::now() + std::chrono::seconds(6);
    while (device.snapshot().state != expected)
    {
        require(device.snapshot().state != audio::DeviceState::Failed, "device failed");
        require(Clock::now() < deadline, "state timeout");
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}
struct Usage
{
    long switches{}, ticks{};
};
Usage usage(int tid)
{
    auto root = std::filesystem::path("/proc/self/task") / std::to_string(tid);
    std::ifstream status(root / "status");
    std::string line;
    Usage result;
    while (std::getline(status, line))
        if (line.starts_with("voluntary_ctxt_switches:") ||
            line.starts_with("nonvoluntary_ctxt_switches:"))
            result.switches += std::stol(line.substr(line.find(':') + 1));
    std::ifstream stat(root / "stat");
    std::getline(stat, line);
    require(!line.empty(), "worker stat");
    line = line.substr(line.rfind(')') + 2);
    std::size_t i = 0;
    std::istringstream fields(line);
    std::string field;
    while (fields >> field)
    {
        if (i == 11 || i == 12)
            result.ticks += std::stol(field);
        ++i;
    }
    return result;
}
int main(int argc, char** argv)
{
    const auto* server = std::getenv("PULSE_SERVER");
    require(server && std::string(server).starts_with("unix:/tmp/nekomote-remaining/"),
            "requires own private null-sink server");
    model::AudioClip clip;
    clip.sample_rate = 8000;
    clip.channels = 1;
    clip.samples.assign(32000, .25f);
    clip.rms.assign((clip.samples.size() + model::kAudioRmsFrames - 1) / model::kAudioRmsFrames,
                    .25f);
    model::AudioBank bank;
    bank.push_back(std::move(clip));
    auto shared = std::make_shared<const model::AudioBank>(std::move(bank));
    auto device = std::make_unique<audio::Device>(shared);
    state(*device, audio::DeviceState::Ready);
    int tid = 0;
    for (auto& entry : std::filesystem::directory_iterator("/proc/self/task"))
    {
        int value = std::stoi(entry.path().filename().string());
        if (value != ::getpid())
            tid = value;
    }
    require(tid != 0, "worker ID");
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::puts("READY");
    if (argc > 1 && std::string(argv[1]) == "disconnect")
    {
        state(*device, audio::DeviceState::Failed);
        device.reset();
        std::puts("DISCONNECTED_AND_JOINED");
        return 0;
    }
    const auto measure = [&](const char* label, int seconds)
    {
        auto before = usage(tid);
        auto start = Clock::now();
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        auto after = usage(tid);
        double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
        std::printf("%s switches=%ld per_second=%.3f cpu_ticks=%ld seconds=%.3f\n", label,
                    after.switches - before.switches,
                    double(after.switches - before.switches) / elapsed, after.ticks - before.ticks,
                    elapsed);
    };
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    measure("idle", 3);
    require(device->submit({application::AudioCommandKind::Play, AudioClipId{1}}), "play");
    state(*device, audio::DeviceState::Playing);
    auto deadline = Clock::now() + std::chrono::seconds(2);
    while (device->snapshot().rms <= 0)
    {
        require(Clock::now() < deadline, "no RMS");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    measure("playing", 1);
    require(device->submit({application::AudioCommandKind::Pause, {}}), "pause");
    state(*device, audio::DeviceState::Paused);
    require(device->snapshot().rms == 0, "paused RMS");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    measure("paused", 3);
    require(device->submit({application::AudioCommandKind::Resume, {}}), "resume");
    state(*device, audio::DeviceState::Playing);
    state(*device, audio::DeviceState::Ready);
    std::vector<double> latency;
    for (int i = 0; i < 24; ++i)
    {
        auto t = Clock::now();
        require(device->submit({application::AudioCommandKind::Play, AudioClipId{1}}),
                "play again");
        state(*device, audio::DeviceState::Playing);
        latency.push_back(std::chrono::duration<double, std::micro>(Clock::now() - t).count());
        require(device->submit({application::AudioCommandKind::Stop, {}}), "stop");
        state(*device, audio::DeviceState::Ready);
    }
    std::sort(latency.begin(), latency.end());
    std::printf("play_command_us p50=%.3f p95=%.3f p99=%.3f\n", latency[12], latency[22],
                latency[23]);
    auto t = Clock::now();
    device.reset();
    std::printf("idle_join_us=%.3f\n",
                std::chrono::duration<double, std::micro>(Clock::now() - t).count());
}
