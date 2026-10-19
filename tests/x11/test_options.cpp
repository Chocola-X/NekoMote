#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

#include "options.hpp"

namespace
{
using nekomote::x11::InputMode;
using nekomote::x11::Options;

Options parse(std::initializer_list<std::string> arguments)
{
    std::vector<std::string> storage{"nekomote_x11"};
    storage.insert(storage.end(), arguments);
    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (auto& argument : storage)
        argv.push_back(argument.data());
    return nekomote::x11::parse_options(static_cast<int>(argv.size()), argv.data());
}

} // namespace

TEST_CASE("X11 options select one model source", "[x11][options]")
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

TEST_CASE("X11 diagnostics require positive exact frame counts", "[x11][options]")
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

TEST_CASE("X11 accepts only supported local input modes", "[x11][options]")
{
    REQUIRE(parse({"--model", "model"}).input == InputMode::interactive);
    REQUIRE(parse({"--model", "model", "--input", "interactive"}).input == InputMode::interactive);
    REQUIRE(parse({"--model", "model", "--input", "passthrough"}).input == InputMode::passthrough);
    REQUIRE_THROWS_AS(parse({"--model", "model", "--input", "global"}), std::runtime_error);
    REQUIRE_THROWS_AS(parse({"--model", "model", "--input"}), std::runtime_error);
    REQUIRE_THROWS_AS(parse({"--model", "model", "--unknown"}), std::runtime_error);
}

TEST_CASE("X11 discovery does not require model assets", "[x11][options]")
{
    REQUIRE(parse({"--help"}).help);
    REQUIRE(parse({"--list-outputs"}).list_outputs);
    const auto selected =
        parse({"--assets", "assets", "--output", "DP-1", "--config", "wallpaper.json"});
    REQUIRE(selected.output == "DP-1");
    REQUIRE(selected.config == "wallpaper.json");
}

TEST_CASE("X11 output configuration names cannot escape their directory", "[x11][options]")
{
    using nekomote::x11::default_config_path;
    const auto parent = default_config_path("").parent_path();
    REQUIRE(parent.filename() == "x11");
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
}

TEST_CASE("X11 shared controls may be opened by the host", "[x11][options]")
{
    REQUIRE(parse({"--assets", "assets", "--ui-open"}).ui_open);
    REQUIRE_FALSE(parse({"--assets", "assets"}).ui_open);
}
