#include <nekomote/host_gpu/gpu.hpp>

#include <csignal>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

#include "host.hpp"
#include "options.hpp"

namespace
{
void stop_handler(int)
{
    nekomote::layer_shell::request_stop();
}
#if defined(NEKOMOTE_GNOME45)
constexpr auto kRole = nekomote::layer_shell::SurfaceRole::gnome_toplevel;
constexpr auto kProgram = "nekomote_gnome45";
#else
constexpr auto kRole = nekomote::layer_shell::SurfaceRole::layer;
constexpr auto kProgram = "nekomote_layer_shell";
#endif
} // namespace

int main(int argc, char** argv)
{
    try
    {
        auto options = nekomote::layer_shell::parse_options(argc, argv, kRole);
        if (options.help)
        {
            nekomote::layer_shell::print_help(kRole);
            return 0;
        }
        if (options.list_gpus)
        {
            nekomote::host_gpu::list_gpus();
            return 0;
        }
        if (options.save_config)
        {
            std::cout << "Saved launch configuration: " << options.config << '\n';
            return 0;
        }
        struct sigaction action{};
        action.sa_handler = stop_handler;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGINT, &action, nullptr) || sigaction(SIGTERM, &action, nullptr))
        {
            throw std::runtime_error("cannot install signal handlers");
        }
        nekomote::layer_shell::Host host(std::move(options));
        host.run();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << kProgram << ": " << error.what() << '\n';
        return 1;
    }
}
