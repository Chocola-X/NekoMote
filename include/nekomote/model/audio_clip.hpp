#pragma once

#include <cstdint>
#include <vector>

namespace nekomote::model
{
// Immutable decoded PCM, interleaved channels, normalized -1..1. No device state.
struct AudioClip
{
    std::uint32_t sample_rate{0};
    std::uint32_t channels{0};
    std::vector<float> samples;
    // Mono RMS for each 512-frame window, prepared at load time for lip snapshots.
    std::vector<float> rms;
};
inline constexpr std::uint32_t kAudioRmsFrames = 512;
} // namespace nekomote::model
