#pragma once

#include <nekomote/abi.h>

#include <array>
#include <cstdio>

namespace nekomote::abi
{

struct ErrorState
{
    nekomote_error code{NEKOMOTE_OK};
    std::array<char, 1024> message{};
};

extern thread_local ErrorState g_error;

inline void set_error(nekomote_error code, const char* msg) noexcept
{
    g_error.code = code;
    std::snprintf(g_error.message.data(), g_error.message.size(), "%s", msg ? msg : "");
}

} // namespace nekomote::abi
