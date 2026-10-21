#include "options.hpp"

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace nekomote::x11
{
std::filesystem::path default_config_path(const std::string& output)
{
    std::filesystem::path base;
    const auto* xdg = std::getenv("XDG_CONFIG_HOME");
    const auto* home = std::getenv("HOME");
    if (xdg && *xdg)
    {
        base = xdg;
        if (!base.is_absolute())
        {
            throw std::runtime_error("XDG_CONFIG_HOME must be absolute");
        }
    }
    else if (home && *home)
    {
        base = std::filesystem::path(home) / ".config";
    }
    else
    {
        throw std::runtime_error("--config is required when HOME and XDG_CONFIG_HOME are unset");
    }
    // Hex encoding is injective and prevents output names becoming path components.
    std::string name = "default";
    if (!output.empty())
    {
        constexpr char digits[] = "0123456789abcdef";
        name = "output-";
        for (const unsigned char value : output)
        {
            name += digits[value >> 4];
            name += digits[value & 15];
        }
    }
    return base / "nekomote" / "x11" / (name + ".json");
}

Options parse_options(int argc, char** argv)
{
    Options result;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view option(argv[index]);
        const auto value = [&]() -> std::string_view
        {
            if (++index >= argc || !*argv[index])
            {
                throw std::runtime_error("missing value for " + std::string(option));
            }
            return argv[index];
        };
        if (option == "--help")
            result.help = true;
        else if (option == "--list-outputs")
            result.list_outputs = true;
        else if (option == "--ui-open")
            result.ui_open = true;
        else if (option == "--mute")
            result.muted = true;
        else if (option == "--no-save")
            result.no_save = true;
        else if (option == "--assets")
            result.assets = value();
        else if (option == "--model")
            result.model = value();
        else if (option == "--session")
            result.session = value();
        else if (option == "--output")
            result.output = value();
        else if (option == "--config")
            result.config = value();
        else if (option == "--capture")
            result.capture = value();
        else if (option == "--backend")
        {
            const auto backend = value();
            if (backend == "opengl")
                result.backend = Backend::opengl;
            else if (backend == "vulkan")
            {
#if defined(NEKOMOTE_HAS_VULKAN)
                result.backend = Backend::vulkan;
#else
                throw std::runtime_error("Vulkan backend is not enabled in this build");
#endif
            }
            else
                throw std::runtime_error("--backend expects opengl or vulkan");
        }
        else if (option == "--input")
        {
            const auto mode = value();
            if (mode == "interactive")
                result.input = InputMode::interactive;
            else if (mode == "passthrough")
                result.input = InputMode::passthrough;
            else
                throw std::runtime_error("--input expects interactive or passthrough");
        }
        else if (option == "--frames")
        {
            const auto text = value();
            const auto parsed =
                std::from_chars(text.data(), text.data() + text.size(), result.frames);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
                !result.frames)
            {
                throw std::runtime_error("--frames expects a positive integer");
            }
        }
        else
            throw std::runtime_error("unknown option: " + std::string(option));
    }
    if (result.help || result.list_outputs)
        return result;
    if (result.assets.empty() == result.model.empty())
    {
        throw std::runtime_error("select exactly one of --assets PATH or --model PATH");
    }
    if (!result.session.empty() && result.model.empty())
    {
        throw std::runtime_error("--session requires --model");
    }
    if (!result.capture.empty() && !result.frames)
    {
        throw std::runtime_error("--capture is diagnostic and requires --frames");
    }
    if (result.frames)
    {
        result.muted = true;
        result.no_save = true;
    }
    return result;
}

void print_help()
{
    std::cout
        << "Usage: nekomote_x11 --assets DIR [options]\n"
           "       nekomote_x11 --model FILE [--session FILE] [options]\n"
           "       nekomote_x11 --list-outputs\n\n"
           "  --backend opengl|vulkan       graphics backend, default opengl\n"
           "  --output NAME                 XRandR output name (otherwise primary/first active)\n"
           "  --config FILE                 per-output application configuration\n"
           "  --input interactive|passthrough  local pointer input, default interactive\n"
           "  --ui-open                     open the shared control panel\n"
           "  --mute                        disable audio\n"
           "  --no-save                     do not persist application configuration\n"
           "  --frames N                    diagnostic: fixed 1/60 step, muted, no save\n"
           "  --capture FILE                diagnostic: final frame as binary PPM\n"
           "  --help                        show this help\n\n"
           "Requires X11, an EWMH desktop-window WM, XRandR 1.3 and OpenGL 3.3 or Vulkan 1.0. No "
           "keyboard focus "
           "or global\n"
           "pointer tracking is requested. Run one process per output for multiple screens.\n";
}
} // namespace nekomote::x11
