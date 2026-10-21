#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace nekomote::x11
{
enum class InputMode
{
    interactive,
    passthrough,
};

enum class Backend
{
    opengl,
    vulkan,
};

struct Options
{
    std::string assets;
    std::string model;
    std::string session;
    std::string output;
    std::filesystem::path config;
    std::filesystem::path state;
    std::filesystem::path capture;
    InputMode input = InputMode::interactive;
    Backend backend = Backend::opengl;
    uint64_t frames = 0;
    bool ui_open = false;
    bool muted = false;
    bool no_save = false;
    bool save_config = false;
    bool list_outputs = false;
    bool help = false;
};

Options parse_options(int argc, char** argv);
void print_help();
} // namespace nekomote::x11
