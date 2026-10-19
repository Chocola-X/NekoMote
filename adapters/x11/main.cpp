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
    nekomote::x11::request_stop();
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        auto options = nekomote::x11::parse_options(argc, argv);
        if (options.help)
        {
            nekomote::x11::print_help();
            return 0;
        }
        struct sigaction action{};
        action.sa_handler = stop_handler;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGINT, &action, nullptr) || sigaction(SIGTERM, &action, nullptr))
        {
            throw std::runtime_error("cannot install signal handlers");
        }
        nekomote::x11::Host host(std::move(options));
        host.run();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "nekomote_x11: " << error.what() << '\n';
        return 1;
    }
}
