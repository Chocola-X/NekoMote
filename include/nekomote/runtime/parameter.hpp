#pragma once

#include <nekomote/types.hpp>

namespace nekomote::runtime
{
struct ParameterValue
{
    ParamId id;
    float value{0};
};
} // namespace nekomote::runtime
