#include <catch2/catch_test_macros.hpp>
#include <nekomote/audio/device.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
nekomote::model::AudioBank clips(float sample = 0.25f)
{
    nekomote::model::AudioClip clip;
    clip.sample_rate = 8000;
    clip.channels = 1;
    clip.samples.assign(nekomote::model::kAudioRmsFrames, sample);
    clip.rms = {sample};
    nekomote::model::AudioBank bank;
    bank.push_back(std::move(clip));
    return bank;
}
} // namespace

TEST_CASE("Audio device retains the published PCM until its worker is joined", "[audio-device]")
{
    auto decoded = clips();
    const auto* samples = decoded.front().samples.data();
    auto bank = std::make_shared<const nekomote::model::AudioBank>(std::move(decoded));
    REQUIRE(bank->front().samples.data() == samples);
    std::weak_ptr<const nekomote::model::AudioBank> lifetime = bank;
    auto device = std::make_unique<nekomote::audio::Device>(bank);
    REQUIRE(bank.use_count() == 2);
    bank.reset();
    REQUIRE_FALSE(lifetime.expired());
    REQUIRE(lifetime.lock()->front().samples.data() == samples);
    // No playback command is submitted. These tests use an unavailable server,
    // and bank ownership must hold during both connecting and failed states.
    device.reset();
    REQUIRE(lifetime.expired());
}

TEST_CASE("Device recreation reuses the host bank without replacing PCM", "[audio-device]")
{
    const auto bank = std::make_shared<const nekomote::model::AudioBank>(clips());
    const auto* samples = bank->front().samples.data();
    for (int recreation = 0; recreation < 3; ++recreation)
    {
        INFO(recreation);
        {
            nekomote::audio::Device device(bank);
            REQUIRE(bank.use_count() == 2);
            REQUIRE(bank->front().samples.data() == samples);
        }
        REQUIRE(bank.use_count() == 1);
        REQUIRE(bank->front().samples.front() == 0.25f);
    }
}

TEST_CASE("Replacing a host bank preserves the previous device lifetime", "[audio-device]")
{
    auto bank = std::make_shared<const nekomote::model::AudioBank>(clips(0.25f));
    std::weak_ptr<const nekomote::model::AudioBank> previous = bank;
    auto old_device = std::make_unique<nekomote::audio::Device>(bank);
    bank = std::make_shared<const nekomote::model::AudioBank>(clips(0.5f));
    {
        nekomote::audio::Device next_device(bank);
        REQUIRE_FALSE(previous.expired());
        REQUIRE(previous.lock()->front().samples.front() == 0.25f);
        REQUIRE(bank->front().samples.front() == 0.5f);
        old_device.reset();
        REQUIRE(previous.expired());
        REQUIRE(bank.use_count() == 2);
    }
    REQUIRE(bank.use_count() == 1);
}

TEST_CASE("Audio device rejects invalid published banks", "[audio-device]")
{
    REQUIRE_THROWS_AS(nekomote::audio::Device(nullptr), std::invalid_argument);
    auto bank = clips();
    SECTION("nonfinite PCM")
    {
        bank.front().samples.front() = std::numeric_limits<float>::quiet_NaN();
    }
    SECTION("missing RMS window")
    {
        bank.front().rms.clear();
    }
    SECTION("unsupported sample rate")
    {
        bank.front().sample_rate = 0;
    }
    REQUIRE_THROWS_AS(nekomote::audio::Device(
                          std::make_shared<const nekomote::model::AudioBank>(std::move(bank))),
                      std::invalid_argument);
}

TEST_CASE("Audio event waits preserve playback, pause clocks and worker teardown",
          "[audio-device][pulse-integration]")
{
    // Explicit opt-in to a test-owned null sink. Never send this synthetic bank
    // to the user's ordinary PulseAudio/PipeWire server.
    const auto* server = std::getenv("NEKOMOTE_TEST_PULSE_SERVER");
    if (!server)
        SKIP("requires an explicitly configured private null-sink server");
    REQUIRE(std::getenv("PULSE_SERVER") != nullptr);
    REQUIRE(std::string(std::getenv("PULSE_SERVER")) == server);
    using nekomote::application::AudioCommandKind;
    using nekomote::audio::Device;
    using nekomote::audio::DeviceState;
    using Clock = std::chrono::steady_clock;

    auto decoded = clips();
    auto& clip = decoded.front();
    clip.samples.assign(16000, .25f);
    std::fill(clip.samples.begin() + 8000, clip.samples.end(), .5f);
    clip.rms.clear();
    const auto block_size = nekomote::model::kAudioRmsFrames;
    for (std::size_t first = 0; first < clip.samples.size(); first += block_size)
    {
        const auto last = std::min(first + block_size, clip.samples.size());
        float sum = 0;
        for (auto i = first; i < last; ++i)
            sum += clip.samples[i] * clip.samples[i];
        clip.rms.push_back(std::sqrt(sum / static_cast<float>(last - first)));
    }
    auto bank = std::make_shared<const nekomote::model::AudioBank>(std::move(decoded));
    auto device = std::make_unique<Device>(bank);
    const auto await_state = [&](DeviceState expected)
    {
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        auto snapshot = device->snapshot();
        while (snapshot.state != expected && snapshot.state != DeviceState::Failed &&
               Clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            snapshot = device->snapshot();
        }
        INFO(snapshot.error);
        REQUIRE(snapshot.state == expected);
    };
    const auto await_rms = [&]
    {
        const auto deadline = Clock::now() + std::chrono::seconds(1);
        while (device->snapshot().rms == 0 && Clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        REQUIRE(device->snapshot().state == DeviceState::Playing);
        REQUIRE(device->snapshot().rms == .25f);
    };
    const auto submit = [&](AudioCommandKind kind)
    { REQUIRE(device->submit({kind, nekomote::AudioClipId{1}})); };
    await_state(DeviceState::Ready);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    submit(AudioCommandKind::Play);
    await_state(DeviceState::Playing);
    await_rms();
    submit(AudioCommandKind::Pause);
    await_state(DeviceState::Paused);
    // Wall time crosses the first RMS segment; the paused playback clock must
    // remain in that segment when resumed.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    REQUIRE(device->snapshot().rms == 0.0f);
    submit(AudioCommandKind::Resume);
    await_state(DeviceState::Playing);
    await_rms();
    await_state(DeviceState::Ready); // Natural completion still retires the stream.
    REQUIRE(device->snapshot().rms == 0.0f);
    for (int i = 0; i < 20; ++i)
    {
        submit(AudioCommandKind::Play);
        await_state(DeviceState::Playing);
        submit(AudioCommandKind::Stop);
        await_state(DeviceState::Ready);
    }
    SECTION("ready worker teardown")
    {
        await_state(DeviceState::Ready);
    }
    SECTION("paused worker teardown and play while paused")
    {
        submit(AudioCommandKind::Pause);
        submit(AudioCommandKind::Play);
        await_state(DeviceState::Paused);
        REQUIRE(device->snapshot().rms == 0.0f);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    device.reset();
    REQUIRE(bank.use_count() == 1);
}
