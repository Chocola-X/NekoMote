#pragma once

#include <nekomote/model/audio_clip.hpp>

#include <filesystem>
#include <string>

namespace nekomote::asset_io
{
struct AudioLoadResult
{
    model::AudioClip clip;
    std::string error;
    bool ok{false};
};
// RIFF/WAVE integer PCM 8/16/24/32 or IEEE float32, mono/stereo. Explicitly
// rejects compressed/extensible formats. Loading only; no I/O in playback.
AudioLoadResult load_wave(const std::filesystem::path& path);
} // namespace nekomote::asset_io
