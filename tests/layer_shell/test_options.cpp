#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <cstdlib>
#include <filesystem>
#include <initializer_list>
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

struct ConfigEnvironment
{
    std::optional<std::string> previous;
    ConfigEnvironment()
    {
        if (const char* value = std::getenv("XDG_CONFIG_HOME"))
            previous = value;
        if (::setenv("XDG_CONFIG_HOME", "/tmp/nekomote-options-config", 1) != 0)
            throw std::runtime_error("cannot set private test config environment");
    }
    ~ConfigEnvironment()
    {
        if (previous)
            ::setenv("XDG_CONFIG_HOME", previous->c_str(), 1);
        else
            ::unsetenv("XDG_CONFIG_HOME");
    }
};
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
    const auto selected =
        parse({"--assets", "assets", "--output", "DP-1", "--config", "wallpaper.json"});
    REQUIRE(selected.output == "DP-1");
    REQUIRE(selected.config == "wallpaper.json");
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

TEST_CASE("Layer shell output configuration names cannot escape their directory",
          "[layer-shell][options]")
{
    ConfigEnvironment environment;
    const auto parent = std::filesystem::path("/tmp/nekomote-options-config/nekomote/layer-shell");
    using nekomote::layer_shell::default_config_path;
    REQUIRE(default_config_path("") == parent / "default.json");
    REQUIRE(default_config_path("a") == parent / "output-61.json");
    REQUIRE(default_config_path("../") == parent / "output-2e2e2f.json");
    REQUIRE(default_config_path("a") != default_config_path("61"));
    REQUIRE(default_config_path("/") != default_config_path("2f"));
    for (const auto* name : {"../DP-1", "/tmp/output", "a/b", "DP-1", "\xC3\xA9"})
    {
        INFO(name);
        REQUIRE(default_config_path(name).parent_path() == parent);
    }
    REQUIRE(::setenv("XDG_CONFIG_HOME", "relative-directory", 1) == 0);
    REQUIRE_THROWS_AS(default_config_path("DP-1"), std::runtime_error);
}
