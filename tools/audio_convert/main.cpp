// Explicit offline integer PCM conversion for FreeMote imports and migration.
#include <nekomote/asset_io/audio_import.hpp>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3)
            throw std::runtime_error("usage: nekomote_audio_convert SOURCE.wav NEW.flac");
        const auto error = nekomote::asset_io::transcode_wave_to_flac(argv[1], argv[2]);
        if (!error.empty())
            throw std::runtime_error(error);
        std::cout << "{\"ok\":true}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
