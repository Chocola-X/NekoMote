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
// Native FLAC integer PCM 8/16/24/32, mono/stereo, 8..192 kHz.
// Decodes directly into owned PCM and prepares RMS; no I/O in playback.
// Rejects unknown sample counts and streams exceeding 256 MiB decoded PCM.
AudioLoadResult load_flac(const std::filesystem::path& path);
} // namespace nekomote::asset_io
