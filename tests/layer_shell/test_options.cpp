#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <nekomote/abi.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "options.hpp"

namespace
{
using nekomote::layer_shell::Backend;
using nekomote::layer_shell::InputMode;
using nekomote::layer_shell::Options;

struct ConfigEnvironment
{
    std::filesystem::path root;
    std::optional<std::string> home;
    std::optional<std::string> xdg;
    ConfigEnvironment()
    {
        if (const char* value = std::getenv("HOME"))
            home = value;
        if (const char* value = std::getenv("XDG_CONFIG_HOME"))
            xdg = value;
        std::string pattern =
            (std::filesystem::temp_directory_path() / "nekomote-layer_shell-options-XXXXXX")
                .string();
        char* directory = ::mkdtemp(pattern.data());
        if (!directory)
            throw std::runtime_error("cannot create private config environment");
        root = directory;
        if (::setenv("HOME", directory, 1) || ::setenv("XDG_CONFIG_HOME", directory, 1))
            throw std::runtime_error("cannot set private config environment");
    }
    ~ConfigEnvironment()
    {
        if (home)
            ::setenv("HOME", home->c_str(), 1);
        else
            ::unsetenv("HOME");
        if (xdg)
            ::setenv("XDG_CONFIG_HOME", xdg->c_str(), 1);
        else
            ::unsetenv("XDG_CONFIG_HOME");
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};
const ConfigEnvironment environment;

std::string read(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

Options parse(std::initializer_list<std::string> arguments)
{
    std::vector<std::string> storage{"nekomote_layer_shell"};
    storage.insert(storage.end(), arguments);
    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (auto& argument : storage)
        argv.push_back(argument.data());
    return nekomote::layer_shell::parse_options(static_cast<int>(argv.size()), argv.data());
}

} // namespace

TEST_CASE("Layer shell options select one model source", "[layer-shell][options]")
{
    REQUIRE(parse({"--assets", "assets"}).assets == "assets");
    const auto model = parse({"--model", "model", "--session", "session"});
    REQUIRE(model.model == "model");
    REQUIRE(model.session == "session");
    REQUIRE_THROWS_AS(parse({}), std::runtime_error);
    REQUIRE_THROWS_AS(parse({"--assets", "assets", "--model", "model"}), std::runtime_error);
    REQUIRE_THROWS_AS(parse({"--assets", "assets", "--session", "session"}), std::runtime_error);
    REQUIRE_THROWS_AS(parse({"--model"}), std::runtime_error);
    REQUIRE_THROWS_AS(parse({"--assets", ""}), std::runtime_error);
}

TEST_CASE("Layer shell diagnostics require positive exact frame counts", "[layer-shell][options]")
{
    REQUIRE_THROWS_AS(parse({"--model", "model", "--capture", "capture.ppm"}), std::runtime_error);
    const auto diagnostic =
        parse({"--model", "model", "--frames", "4", "--capture", "capture.ppm"});
    REQUIRE(diagnostic.frames == 4);
    REQUIRE(diagnostic.muted);
    REQUIRE(diagnostic.no_save);
    REQUIRE(diagnostic.capture == "capture.ppm");
    REQUIRE_FALSE(parse({"--model", "model"}).no_save);
    REQUIRE_FALSE(parse({"--model", "model"}).muted);
    for (const auto* value : {"0", "-1", "+1", "1x", "1.5", "", "18446744073709551616"})
    {
        INFO(value);
        REQUIRE_THROWS_AS(parse({"--model", "model", "--frames", value}), std::runtime_error);
    }
}

TEST_CASE("Layer shell accepts only supported local input modes", "[layer-shell][options]")
{
    REQUIRE(parse({"--model", "model"}).input == InputMode::interactive);
    REQUIRE(parse({"--model", "model", "--input", "interactive"}).input == InputMode::interactive);
    REQUIRE(parse({"--model", "model", "--input", "passthrough"}).input == InputMode::passthrough);
    REQUIRE_THROWS_AS(parse({"--model", "model", "--input", "global"}), std::runtime_error);
    REQUIRE_THROWS_AS(parse({"--model", "model", "--input"}), std::runtime_error);
    REQUIRE_THROWS_AS(parse({"--model", "model", "--unknown"}), std::runtime_error);
}

TEST_CASE("Layer shell discovery does not require model assets", "[layer-shell][options]")
{
    REQUIRE(parse({"--help"}).help);
    REQUIRE(parse({"--list-outputs"}).list_outputs);
    const auto path = environment.root / "discovery-launch.json";
    const auto selected =
        parse({"--assets", "assets", "--output", "DP-1", "--config", path.string()});
    REQUIRE(selected.output == "DP-1");
    REQUIRE(selected.config == path);
}

TEST_CASE("Layer shell backend selection respects build capabilities", "[layer-shell][options]")
{
    REQUIRE(parse({"--model", "model"}).backend == Backend::opengl);
    REQUIRE(parse({"--model", "model", "--backend", "opengl"}).backend == Backend::opengl);
#if defined(NEKOMOTE_HAS_VULKAN)
    REQUIRE(parse({"--model", "model", "--backend", "vulkan"}).backend == Backend::vulkan);
#else
    REQUIRE_THROWS_WITH(parse({"--model", "model", "--backend", "vulkan"}),
                        "Vulkan backend is not enabled in this build");
#endif
    REQUIRE_THROWS_AS(parse({"--model", "model", "--backend", "software"}), std::runtime_error);
    REQUIRE_THROWS_AS(parse({"--model", "model", "--backend"}), std::runtime_error);
    REQUIRE(parse({"--assets", "assets", "--ui-open"}).ui_open);
    REQUIRE_FALSE(parse({"--assets", "assets"}).ui_open);
}

TEST_CASE("Layer shell configuration paths identify independent instances",
          "[layer-shell][options]")
{
    const auto first = parse({"--assets", "assets", "--output", "DP-1"});
    const auto second = parse({"--assets", "assets", "--output", "DP-2"});
    REQUIRE(first.config == environment.root / "nekomote" / "layer-shell.json");
    REQUIRE(first.config == second.config);
    REQUIRE(first.state == second.state);
    REQUIRE(first.state == environment.root / "nekomote" / "layer-shell.panel.json");
    const auto custom = environment.root / "instance.json";
    const auto separate = parse({"--assets", "assets", "--config", custom.string()});
    REQUIRE(separate.state == environment.root / "instance.panel.json");
    REQUIRE(separate.state != first.state);
    REQUIRE_FALSE(std::filesystem::exists(first.config));
    REQUIRE_FALSE(std::filesystem::exists(first.state));
    REQUIRE_FALSE(std::filesystem::exists(custom));
    REQUIRE_THROWS_AS(
        parse({"--assets", "assets", "--config", custom.string(), "--state", custom.string()}),
        std::runtime_error);
}

TEST_CASE("Layer shell launch settings save explicitly and merge CLI overrides",
          "[layer-shell][options]")
{
    const auto config = environment.root / "save.json";
    const auto state = environment.root / "custom-panel.json";
    const auto saved =
        parse({"--config", config.string(), "--assets", "assets", "--output", "DP-1", "--input",
               "passthrough", "--state", state.string(), "--save-config"});
    REQUIRE(saved.save_config);
    REQUIRE(std::filesystem::exists(config));
    REQUIRE_FALSE(std::filesystem::exists(state));
    const auto contents = read(config);
    const auto restored = parse({"--config", config.string()});
    REQUIRE(restored.assets == (std::filesystem::current_path() / "assets").string());
    REQUIRE(restored.output == "DP-1");
    REQUIRE(restored.state == state);
    REQUIRE(restored.input == InputMode::passthrough);
    REQUIRE(restored.backend == Backend::opengl);
#if defined(NEKOMOTE_HAS_VULKAN)
    parse({"--config", config.string(), "--backend", "vulkan", "--save-config"});
    REQUIRE(parse({"--config", config.string()}).backend == Backend::vulkan);
    REQUIRE(parse({"--config", config.string(), "--backend", "opengl"}).backend == Backend::opengl);
    parse({"--config", config.string(), "--backend", "opengl", "--save-config"});
#endif
    const auto override =
        parse({"--config", config.string(), "--output", "DP-2", "--input", "interactive"});
    REQUIRE(override.output == "DP-2");
    REQUIRE(override.input == InputMode::interactive);
    REQUIRE(read(config) == contents);
    const auto model =
        parse({"--config", config.string(), "--model", "model", "--session", "session"});
    REQUIRE(model.assets.empty());
    REQUIRE(model.model == "model");
    REQUIRE(model.session == "session");
    parse(
        {"--config", config.string(), "--model", "model", "--session", "session", "--save-config"});
    const auto assets = parse({"--config", config.string(), "--assets", "other-assets"});
    REQUIRE(assets.model.empty());
    REQUIRE(assets.session.empty());
    REQUIRE(assets.assets == "other-assets");
    REQUIRE_THROWS_AS(parse({"--config", config.string(), "--save-config", "--no-save"}),
                      std::runtime_error);
    for (const auto* flag : {"--mute", "--ui-open"})
        REQUIRE_THROWS_AS(parse({"--config", config.string(), "--save-config", flag}),
                          std::runtime_error);
    REQUIRE_THROWS_AS(parse({"--config", config.string(), "--save-config", "--frames", "4"}),
                      std::runtime_error);
}

TEST_CASE("Layer shell discovery skips configuration I/O and environment lookup",
          "[layer-shell][options]")
{
    const auto broken = environment.root / "invalid.json";
    std::ofstream(broken) << "not JSON";
    REQUIRE(::unsetenv("HOME") == 0);
    REQUIRE(::unsetenv("XDG_CONFIG_HOME") == 0);
    REQUIRE(parse({"--help", "--config", broken.string()}).help);
    REQUIRE(parse({"--list-outputs", "--config", broken.string()}).list_outputs);
    const auto discovery = parse({"--list-outputs", "--save-config", "--config", broken.string()});
    REQUIRE(discovery.list_outputs);
    REQUIRE_FALSE(discovery.save_config);
    REQUIRE_THROWS_AS(parse({"--assets", "assets"}), std::runtime_error);
    REQUIRE(::setenv("HOME", environment.root.c_str(), 1) == 0);
    REQUIRE(::setenv("XDG_CONFIG_HOME", environment.root.c_str(), 1) == 0);
    REQUIRE_THROWS_AS(parse({"--config", broken.string(), "--assets", "assets"}),
                      std::runtime_error);
}

TEST_CASE("Layer shell saved Vulkan backend respects build capabilities", "[layer-shell][options]")
{
    const auto path = environment.root / "vulkan.json";
    nekomote_launch_config* raw = nullptr;
    REQUIRE(nekomote_launch_config_load(NEKOMOTE_LAUNCH_LAYER_SHELL, path.c_str(), &raw) ==
            NEKOMOTE_OK);
    const std::unique_ptr<nekomote_launch_config, decltype(&nekomote_launch_config_destroy)> config(
        raw, nekomote_launch_config_destroy);
    auto values = nekomote_launch_config_values(config.get());
    values.assets_dir = "assets";
    values.backend = "vulkan";
    REQUIRE(nekomote_launch_config_save(config.get(), values) == NEKOMOTE_OK);
#if defined(NEKOMOTE_HAS_VULKAN)
    REQUIRE(parse({"--config", path.string()}).backend == Backend::vulkan);
#else
    REQUIRE_THROWS_WITH(parse({"--config", path.string()}),
                        "Vulkan backend is not enabled in this build");
#endif
    REQUIRE(parse({"--config", path.string(), "--backend", "opengl"}).backend == Backend::opengl);
    const auto panel = environment.root / "relative-panel.json";
    const auto relative_panel = std::filesystem::relative(panel, std::filesystem::current_path());
    parse({"--config", path.string(), "--backend", "opengl", "--state", relative_panel.string(),
           "--save-config"});
    REQUIRE(parse({"--config", path.string()}).state == panel);
    const auto session_first =
        parse({"--config", path.string(), "--session", "session", "--model", "model"});
    REQUIRE(session_first.model == "model");
    REQUIRE(session_first.session == "session");
}

TEST_CASE("Layer shell copied launch profiles derive separate panel files",
          "[layer-shell][options]")
{
    const auto source = environment.root / "copy-source.json";
    const auto destination = environment.root / "copy-destination.json";
    const auto saved = parse({"--config", source.string(), "--assets", "assets", "--save-config"});
    REQUIRE(saved.state == environment.root / "copy-source.panel.json");
    REQUIRE(read(source).find("state_path") == std::string::npos);
    std::filesystem::copy_file(source, destination);
    const auto copied = parse({"--config", destination.string()});
    REQUIRE(copied.state == environment.root / "copy-destination.panel.json");
    REQUIRE(copied.state != saved.state);
    REQUIRE_FALSE(std::filesystem::exists(saved.state));
    REQUIRE_FALSE(std::filesystem::exists(copied.state));
}
