#include <nekomote/application/startup_config.hpp>

#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#include "config_io.hpp"

namespace nekomote::application
{
namespace
{
using Json = nlohmann::json;
namespace fs = std::filesystem;

std::string_view launch_name(LaunchKind kind)
{
    switch (kind)
    {
        case LaunchKind::Player:
            return "player";
        case LaunchKind::X11:
            return "x11";
        case LaunchKind::LayerShell:
            return "layer-shell";
    }
    throw std::invalid_argument("invalid launch kind");
}

void normalize_path(std::string& value, const fs::path& base)
{
    if (value.empty())
        return;
    fs::path path(value);
    if (!path.is_absolute())
        path = base / path;
    value = path.lexically_normal().string();
}

void validate(LaunchOptions& values, const fs::path& config_path, const fs::path& base,
              bool require_asset)
{
    for (const auto* value : {&values.assets_dir, &values.model_path, &values.session_path,
                              &values.output, &values.backend, &values.input, &values.state_path})
        if (value->find('\0') != std::string::npos)
            throw std::invalid_argument("launch configuration contains an embedded NUL");
    if (values.backend != "opengl" && values.backend != "vulkan")
        throw std::invalid_argument("launch backend must be opengl or vulkan");
    if (values.input != "interactive" && values.input != "passthrough")
        throw std::invalid_argument("launch input must be interactive or passthrough");
    if (values.width < 64 || values.width > 8192 || values.height < 64 || values.height > 8192)
        throw std::invalid_argument("launch width and height must be in 64..8192");
    if (!values.assets_dir.empty() && (!values.model_path.empty() || !values.session_path.empty()))
        throw std::invalid_argument(
            "launch assets_dir cannot be combined with model_path/session_path");
    if (!values.session_path.empty() && values.model_path.empty())
        throw std::invalid_argument("launch session_path requires model_path");
    if (require_asset && values.assets_dir.empty() && values.model_path.empty())
        throw std::invalid_argument("saving launch config requires assets_dir or model_path");
    normalize_path(values.assets_dir, base);
    normalize_path(values.model_path, base);
    normalize_path(values.session_path, base);
    normalize_path(values.state_path, base);
    if (values.state_path.empty())
        values.state_path =
            (config_path.parent_path() / (config_path.stem().string() + ".panel.json")).string();
    if (!fs::path(values.state_path).has_filename() || fs::is_directory(values.state_path))
        throw std::invalid_argument("panel config path must name a file");
    if (fs::weakly_canonical(values.state_path) == fs::weakly_canonical(config_path))
        throw std::invalid_argument("launch config and panel config must use different paths");
}
} // namespace

std::filesystem::path config_directory()
{
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && fs::path(xdg).is_absolute())
        return (fs::path(xdg) / "nekomote").lexically_normal();
    if (const char* home = std::getenv("HOME"); home && fs::path(home).is_absolute())
        return (fs::path(home) / ".config" / "nekomote").lexically_normal();
    throw std::system_error(
        std::make_error_code(std::errc::no_such_file_or_directory),
        "cannot resolve config directory: set absolute XDG_CONFIG_HOME or HOME");
}

std::filesystem::path default_panel_config_path()
{
    return config_directory() / "panel.json";
}

LaunchConfig LaunchConfig::load(LaunchKind kind, const fs::path& path)
{
    LaunchConfig config;
    const auto name = launch_name(kind);
    if (!path.empty() &&
        (!path.has_filename() || path.filename() == "." || path.filename() == ".."))
        throw std::invalid_argument("launch config path must name a file");
    config.path_ = path.empty() ? config_directory() / (std::string(name) + ".json")
                                : fs::absolute(path).lexically_normal();
    const auto json = config_io::read(config.path_);
    if (!json.is_discarded())
    {
        config_io::validate_keys(json,
                                 {"assets_dir", "model_path", "session_path", "output", "backend",
                                  "input", "state_path", "width", "height"},
                                 config.path_);
        for (auto [key, value] : {std::pair{"assets_dir", &config.values_.assets_dir},
                                  {"model_path", &config.values_.model_path},
                                  {"session_path", &config.values_.session_path},
                                  {"output", &config.values_.output},
                                  {"backend", &config.values_.backend},
                                  {"input", &config.values_.input},
                                  {"state_path", &config.values_.state_path}})
        {
            if (!json.contains(key))
                continue;
            if (!json[key].is_string())
                throw std::invalid_argument(config.path_.string() + ": '" + key +
                                            "' must be a string");
            *value = json[key].get<std::string>();
            if (value->find('\0') != std::string::npos)
                throw std::invalid_argument(config.path_.string() + ": embedded NUL in '" + key +
                                            "'");
        }
        for (auto [key, value] :
             {std::pair{"width", &config.values_.width}, {"height", &config.values_.height}})
        {
            if (!json.contains(key))
                continue;
            if (!json[key].is_number_integer() || json[key] < 64 || json[key] > 8192)
                throw std::invalid_argument(config.path_.string() + ": invalid '" + key + "'");
            *value = json[key].get<std::uint32_t>();
        }
    }
    validate(config.values_, config.path_, config.path_.parent_path(), false);
    return config;
}

void LaunchConfig::save(LaunchOptions values)
{
    validate(values, path_, fs::current_path(), true);
    Json document{{"version", 1},
                  {"backend", values.backend},
                  {"input", values.input},
                  {"width", values.width},
                  {"height", values.height}};
    for (const auto& [key, value] : {std::pair{"assets_dir", &values.assets_dir},
                                     {"model_path", &values.model_path},
                                     {"session_path", &values.session_path},
                                     {"output", &values.output},
                                     {"state_path", &values.state_path}})
        if (!value->empty())
            document[key] = *value;
    const auto derived_panel = path_.parent_path() / (path_.stem().string() + ".panel.json");
    if (fs::weakly_canonical(values.state_path) == fs::weakly_canonical(derived_panel))
        document.erase("state_path");
    config_io::write_atomic(path_, document);
    values_ = std::move(values);
}
} // namespace nekomote::application
