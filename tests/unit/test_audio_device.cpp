#include <catch2/catch_test_macros.hpp>
#include <nekomote/audio/device.hpp>

#include <limits>
#include <memory>
#include <stdexcept>

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
