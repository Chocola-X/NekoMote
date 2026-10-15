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
} // namespace

int main(int argc, char** argv)
{
    try
    {
        auto options = nekomote::layer_shell::parse_options(argc, argv);
        if (options.help)
        {
            nekomote::layer_shell::print_help();
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
        std::cerr << "nekomote_layer_shell: " << error.what() << '\n';
        return 1;
    }
}
