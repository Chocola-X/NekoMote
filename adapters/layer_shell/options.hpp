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

enum class SurfaceRole
{
    layer,
    gnome_toplevel,
};

struct Options
{
    std::string assets;
    std::string model;
    std::string session;
    std::string output;
    std::string gpu;
    std::filesystem::path config;
    std::filesystem::path state;
    std::filesystem::path capture;
    Backend backend = Backend::opengl;
    InputMode input = InputMode::interactive;
    SurfaceRole role = SurfaceRole::layer;
    bool desktop_input = false;    // GNOME Shell owns stdin when enabled.
    int32_t width = 0, height = 0; // GNOME Shell logical output dimensions.
    uint64_t frames = 0;
    bool global_gaze = true;
    bool muted = false;
    bool pause_on_session_events = true;
    bool no_save = false;
    bool save_config = false;
    bool ui_open = false;
    bool list_outputs = false;
    bool list_gpus = false;
    bool help = false;
};

Options parse_options(int argc, char** argv, SurfaceRole role = SurfaceRole::layer);
void print_help(SurfaceRole role = SurfaceRole::layer);
} // namespace nekomote::layer_shell
