#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace nekomote::layer_shell
{
enum class Backend
{
    opengl,
    vulkan,
};

enum class InputMode
{
    interactive,
    passthrough,
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
    Backend backend = Backend::opengl;
    InputMode input = InputMode::interactive;
    uint64_t frames = 0;
    bool muted = false;
    bool no_save = false;
    bool save_config = false;
    bool ui_open = false;
    bool list_outputs = false;
    bool help = false;
};

Options parse_options(int argc, char** argv);
void print_help();
} // namespace nekomote::layer_shell
