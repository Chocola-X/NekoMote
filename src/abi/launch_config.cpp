#include <nekomote/application/startup_config.hpp>

#include <nekomote/abi.h>

#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "abi_internal.hpp"

struct nekomote_launch_config
{
    nekomote::application::LaunchConfig config;
    std::string path;
};

namespace
{
using nekomote::abi::set_error;
using nekomote::application::LaunchKind;

template <typename Operation>
nekomote_error config_call(Operation&& operation) noexcept
{
    try
    {
        operation();
        return NEKOMOTE_OK;
    }
    catch (const std::bad_alloc&)
    {
        set_error(NEKOMOTE_ERROR_OUT_OF_MEMORY, "launch config allocation failed");
        return NEKOMOTE_ERROR_OUT_OF_MEMORY;
    }
    catch (const std::invalid_argument& error)
    {
        set_error(NEKOMOTE_ERROR_INVALID_INPUT, error.what());
        return NEKOMOTE_ERROR_INVALID_INPUT;
    }
    catch (const std::exception& error)
    {
        set_error(NEKOMOTE_ERROR_IO, error.what());
        return NEKOMOTE_ERROR_IO;
    }
    catch (...)
    {
        set_error(NEKOMOTE_ERROR_FAULTED, "unknown launch config failure");
        return NEKOMOTE_ERROR_FAULTED;
    }
}
} // namespace

extern "C"
{
    nekomote_error nekomote_launch_config_load(nekomote_launch_kind kind, const char* path,
                                               nekomote_launch_config** out)
    {
        if (!out)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "launch config output pointer is NULL");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        *out = nullptr;
        return config_call(
            [&]
            {
                LaunchKind cpp_kind;
                switch (kind)
                {
                    case NEKOMOTE_LAUNCH_PLAYER:
                        cpp_kind = LaunchKind::Player;
                        break;
                    case NEKOMOTE_LAUNCH_X11:
                        cpp_kind = LaunchKind::X11;
                        break;
                    case NEKOMOTE_LAUNCH_LAYER_SHELL:
                        cpp_kind = LaunchKind::LayerShell;
                        break;
                    default:
                        throw std::invalid_argument("invalid launch kind");
                }
                if (path && !*path)
                    throw std::invalid_argument("launch config path must not be empty");
                auto handle = std::make_unique<nekomote_launch_config>();
                handle->config =
                    nekomote::application::LaunchConfig::load(cpp_kind, path ? path : "");
                handle->path = handle->config.path().string();
                *out = handle.release();
            });
    }

    const char* nekomote_launch_config_path(const nekomote_launch_config* config)
    {
        if (!config)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "launch config is NULL");
            return nullptr;
        }
        return config->path.c_str();
    }

    nekomote_launch_options nekomote_launch_config_values(const nekomote_launch_config* config)
    {
        if (!config)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "launch config is NULL");
            return {};
        }
        const auto& v = config->config.values();
        return {v.assets_dir.c_str(),
                v.model_path.c_str(),
                v.session_path.c_str(),
                v.output.c_str(),
                v.backend.c_str(),
                v.input.c_str(),
                v.state_path.c_str(),
                v.width,
                v.height};
    }

    nekomote_error nekomote_launch_config_save(nekomote_launch_config* config,
                                               nekomote_launch_options values)
    {
        if (!config)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "launch config is NULL");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return config_call(
            [&]
            {
                const auto string = [](const char* value)
                { return std::string(value ? value : ""); };
                nekomote::application::LaunchOptions options{string(values.assets_dir),
                                                             string(values.model_path),
                                                             string(values.session_path),
                                                             string(values.output),
                                                             string(values.backend),
                                                             string(values.input),
                                                             string(values.state_path),
                                                             values.width,
                                                             values.height};
                config->config.save(std::move(options));
            });
    }

    void nekomote_launch_config_destroy(nekomote_launch_config* config)
    {
        delete config;
    }
}
