#include "options.hpp"

#include <nekomote/abi.h>

#include <charconv>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace nekomote::layer_shell
{
Options parse_options(int argc, char** argv)
{
    Options result;
    bool assets_set = false;
    bool model_set = false;
    bool session_set = false;
    bool output_set = false;
    bool state_set = false;
    bool backend_set = false;
    bool input_set = false;
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
        else if (option == "--mute")
            result.muted = true;
        else if (option == "--no-save")
            result.no_save = true;
        else if (option == "--ui-open")
            result.ui_open = true;
        else if (option == "--save-config")
            result.save_config = true;
        else if (option == "--assets")
        {
            result.assets = value();
            assets_set = true;
        }
        else if (option == "--model")
        {
            result.model = value();
            model_set = true;
        }
        else if (option == "--session")
        {
            result.session = value();
            session_set = true;
        }
        else if (option == "--output")
        {
            result.output = value();
            output_set = true;
        }
        else if (option == "--config")
            result.config = value();
        else if (option == "--state")
        {
            result.state = value();
            state_set = true;
        }
        else if (option == "--capture")
            result.capture = value();
        else if (option == "--backend")
        {
            backend_set = true;
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
            input_set = true;
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
    {
        result.save_config = false;
        return result;
    }
    if (assets_set && model_set)
        throw std::runtime_error("select exactly one of --assets PATH or --model PATH");
    if (result.save_config && (result.no_save || result.frames || !result.capture.empty() ||
                               result.ui_open || result.muted))
        throw std::runtime_error(
            "--save-config cannot be combined with runtime diagnostics or --no-save");
    nekomote_launch_config* raw = nullptr;
    if (nekomote_launch_config_load(NEKOMOTE_LAUNCH_LAYER_SHELL,
                                    result.config.empty() ? nullptr : result.config.c_str(),
                                    &raw) != NEKOMOTE_OK)
        throw std::runtime_error(nekomote_error_message());
    const std::unique_ptr<nekomote_launch_config, decltype(&nekomote_launch_config_destroy)> config(
        raw, nekomote_launch_config_destroy);
    const auto saved = nekomote_launch_config_values(config.get());
    result.config = nekomote_launch_config_path(config.get());
    if (!assets_set && !model_set)
    {
        result.assets = saved.assets_dir;
        result.model = saved.model_path;
    }
    if (!assets_set && !model_set && !session_set)
        result.session = saved.session_path;
    if (!output_set)
        result.output = saved.output;
    if (!state_set)
        result.state = saved.state_path;
    if (!backend_set)
        result.backend =
            std::string_view(saved.backend) == "vulkan" ? Backend::vulkan : Backend::opengl;
#if !defined(NEKOMOTE_HAS_VULKAN)
    if (result.backend == Backend::vulkan)
        throw std::runtime_error("Vulkan backend is not enabled in this build");
#endif
    if (!input_set)
        result.input = std::string_view(saved.input) == "passthrough" ? InputMode::passthrough
                                                                      : InputMode::interactive;
    if (std::filesystem::weakly_canonical(std::filesystem::absolute(result.state)) ==
        std::filesystem::weakly_canonical(result.config))
        throw std::runtime_error("launch configuration and panel state must use different files");
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
    if (result.save_config)
    {
        nekomote_launch_options values = saved;
        values.assets_dir = result.assets.c_str();
        values.model_path = result.model.c_str();
        values.session_path = result.session.c_str();
        values.output = result.output.c_str();
        values.backend = result.backend == Backend::vulkan ? "vulkan" : "opengl";
        values.input = result.input == InputMode::passthrough ? "passthrough" : "interactive";
        const auto state = result.state.string();
        values.state_path = state.c_str();
        if (nekomote_launch_config_save(config.get(), values) != NEKOMOTE_OK)
            throw std::runtime_error(nekomote_error_message());
    }
    return result;
}

void print_help()
{
    std::cout
        << "Usage: nekomote_layer_shell --assets DIR [options]\n"
           "       nekomote_layer_shell --model FILE [--session FILE] [options]\n"
           "       nekomote_layer_shell --list-outputs\n\n"
           "  --output NAME                 compositor output name (otherwise compositor selects)\n"
           "  --config FILE                 launch configuration (default XDG config)\n"
           "  --state FILE                  panel state (default CONFIG_STEM.panel.json)\n"
           "  --save-config                 save merged launch settings and exit\n"
           "  --backend opengl|vulkan       graphics backend, default opengl\n"
           "  --input interactive|passthrough  local pointer input, default interactive\n"
           "  --mute                        disable audio\n"
           "  --no-save                     do not persist panel changes\n"
           "  --ui-open                     open shared controls after loading assets\n"
           "  --frames N                    diagnostic: fixed 1/60 step, muted, no save\n"
           "  --capture FILE                diagnostic: final frame as binary PPM\n"
           "  --help                        show this help\n\n"
           "Requires a Wayland compositor with wlr-layer-shell. No keyboard focus or global\n"
           "pointer tracking is requested. Run one process per output for multiple screens.\n";
}
} // namespace nekomote::layer_shell
