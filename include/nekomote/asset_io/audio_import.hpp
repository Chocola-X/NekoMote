#pragma once

#include <nekomote/asset_io/audio.hpp>

namespace nekomote::asset_io
{
// Offline FreeMote source reader: RIFF/WAVE PCM 8/16/24/32 or float32,
// mono/stereo. Compressed and extensible WAVE are rejected.
AudioLoadResult load_wave(const std::filesystem::path& path);

// Offline lossless integer PCM conversion. Float WAVE is rejected rather than
// quantized. Writes a temporary sibling and publishes only a finished FLAC;
// returns an empty string on success, otherwise a source-qualified error.
std::string transcode_wave_to_flac(const std::filesystem::path& source,
                                   const std::filesystem::path& destination);
} // namespace nekomote::asset_io
