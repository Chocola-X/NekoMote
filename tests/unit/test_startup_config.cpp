#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nekomote/application/catalog.hpp>
#include <nekomote/application/controller.hpp>
#include <nekomote/application/startup_config.hpp>
#include <nlohmann/json.hpp>

#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <string>

namespace
{
namespace fs = std::filesystem;
using namespace nekomote::application;

struct Environment
{
    std::string name;
    std::optional<std::string> old;
    explicit Environment(std::string key) : name(std::move(key))
    {
        if (const char* value = std::getenv(name.c_str()))
            old = value;
    }
    ~Environment()
    {
        if (old)
            ::setenv(name.c_str(), old->c_str(), 1);
        else
            ::unsetenv(name.c_str());
    }
    void set(const fs::path& path) const
    {
        ::setenv(name.c_str(), path.c_str(), 1);
    }
};

struct Fixture
{
    fs::path root = fs::temp_directory_path() /
                    ("nekomote-startup-" + std::to_string(::getpid()) + "-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Environment xdg{"XDG_CONFIG_HOME"};
    Environment home{"HOME"};
    Fixture()
    {
        fs::create_directories(root);
        xdg.set(root / "xdg");
        home.set(root / "home");
    }
    ~Fixture()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    fs::path write(const std::string& document)
    {
        const auto file = root / "launch.json";
        std::ofstream(file) << document;
        return file;
    }
};
} // namespace

TEST_CASE("Launch and panel defaults resolve XDG without creating files", "[config][startup]")
{
    Fixture fixture;
    const auto launch = LaunchConfig::load(LaunchKind::X11);
    REQUIRE(launch.path() == fixture.root / "xdg/nekomote/x11.json");
    REQUIRE(launch.values().state_path == fixture.root / "xdg/nekomote/x11.panel.json");
    REQUIRE(launch.values().assets_dir.empty());
    REQUIRE(launch.values().model_path.empty());
    REQUIRE(launch.values().backend == "opengl");
    REQUIRE(launch.values().width == 1920);
    REQUIRE(launch.values().height == 1080);
    REQUIRE(default_panel_config_path() == fixture.root / "xdg/nekomote/panel.json");
    REQUIRE_FALSE(fs::exists(fixture.root / "xdg"));
    fixture.xdg.set("relative-xdg");
    REQUIRE(default_panel_config_path() == fixture.root / "home/.config/nekomote/panel.json");
    fixture.home.set("relative-home");
    REQUIRE_THROWS(LaunchConfig::load(LaunchKind::Player));
    // An explicit path does not depend on HOME or XDG.
    REQUIRE_NOTHROW(LaunchConfig::load(LaunchKind::LayerShell, fixture.root / "private.json"));
}

TEST_CASE("Launch configs roundtrip normalized paths and isolate panel files", "[config][startup]")
{
    Fixture fixture;
    const auto file = fixture.write(R"({"version":1,"assets_dir":"../assets","backend":"vulkan",
        "state_path":"panels/left.json","output":"DP-1","input":"passthrough","width":3840})");
    auto launch = LaunchConfig::load(LaunchKind::LayerShell, file);
    REQUIRE(launch.values().assets_dir == (fixture.root.parent_path() / "assets").string());
    REQUIRE(launch.values().state_path == (fixture.root / "panels/left.json").string());
    auto values = launch.values();
    values.assets_dir = "relative-assets";
    values.state_path = "left-panel.json";
    launch.save(values);
    const auto loaded = LaunchConfig::load(LaunchKind::LayerShell, file);
    REQUIRE(loaded.values().assets_dir == (fs::current_path() / "relative-assets").string());
    REQUIRE(loaded.values().state_path == (fs::current_path() / "left-panel.json").string());
    REQUIRE(loaded.values().backend == "vulkan");
    REQUIRE(loaded.values().input == "passthrough");
    REQUIRE(loaded.values().output == "DP-1");
    REQUIRE(loaded.values().width == 3840);
    REQUIRE(loaded.values().height == 1080);
    const auto other = LaunchConfig::load(LaunchKind::LayerShell, fixture.root / "right.json");
    REQUIRE(other.values().state_path != loaded.values().state_path);
    REQUIRE_FALSE(fs::exists(other.values().state_path));
    REQUIRE_FALSE(fs::exists(loaded.values().state_path));
}

TEST_CASE("Launch config rejects unsupported schema and invalid settings", "[config][startup]")
{
    Fixture fixture;
    const auto document = GENERATE(
        "null", "[]", "{}", "{", R"({"version":0})", R"({"version":2})", R"({"version":1.0})",
        R"({"version":0,"version":1})", R"({"version":1,"unexpected":true})",
        R"({"version":1,"backend":"auto"})", R"({"version":1,"input":"global"})",
        R"({"version":1,"backend":1})", R"({"version":1,"assets_dir":null})",
        R"({"version":1,"assets_dir":"bad\u0000path"})", R"({"version":1,"width":63})",
        R"({"version":1,"height":8193})", R"({"version":1,"width":320.5})",
        R"({"version":1,"width":18446744073709551615})",
        R"({"version":1,"assets_dir":"a","model_path":"b"})", R"({"version":1,"session_path":"s"})",
        R"({"version":1,"state_path":"launch.json"})", R"({"version":1,"state_path":"/"})");
    const auto file = fixture.write(document);
    INFO(document);
    REQUIRE_THROWS_AS(LaunchConfig::load(LaunchKind::X11, file), std::invalid_argument);
}

TEST_CASE("Explicit startup save validates before replacing and cleans failed temporaries",
          "[config][startup]")
{
    Fixture fixture;
    const auto path = fixture.root / "subdir/instance.json";
    auto config = LaunchConfig::load(LaunchKind::Player, path);
    REQUIRE_THROWS_AS(config.save(config.values()), std::invalid_argument);
    REQUIRE_FALSE(fs::exists(path.parent_path()));
    auto values = config.values();
    values.model_path = (fixture.root / "model.nkm").string();
    values.session_path = (fixture.root / "session.nks.json").string();
    config.save(values);
    const auto original = nlohmann::json::parse(std::ifstream(path));
    values.backend = "bad";
    REQUIRE_THROWS_AS(config.save(values), std::invalid_argument);
    REQUIRE(nlohmann::json::parse(std::ifstream(path)) == original);
    REQUIRE(config.values().backend == "opengl");
    values.backend = "opengl";
    values.output = std::string("bad\0output", 10);
    REQUIRE_THROWS_AS(config.save(values), std::invalid_argument);
    REQUIRE(nlohmann::json::parse(std::ifstream(path)) == original);
    values.output.clear();
    values.backend = "vulkan";
    values.state_path = path.string();
    REQUIRE_THROWS_AS(config.save(values), std::invalid_argument);
    values.state_path = (fixture.root / "panel.json").string();
    // A target directory lets the write finish but makes atomic rename fail.
    // No unique temporary remains, and the directory itself is preserved.
    fs::remove(path);
    fs::create_directory(path);
    REQUIRE_THROWS(config.save(values));
    REQUIRE(fs::is_directory(path));
    REQUIRE(config.values().backend == "opengl");
    REQUIRE(std::distance(fs::directory_iterator(path.parent_path()), fs::directory_iterator{}) ==
            1);
}

TEST_CASE("Launch config rejects panel aliases through symbolic links", "[config][startup]")
{
    Fixture fixture;
    const auto path = fixture.write(R"({"version":1,"assets_dir":"assets"})");
    const auto alias = fixture.root / "alias.json";
    fs::create_symlink(path, alias);
    auto config = LaunchConfig::load(LaunchKind::X11, path);
    auto values = config.values();
    values.state_path = alias.string();
    REQUIRE_THROWS_AS(config.save(values), std::invalid_argument);
}

TEST_CASE("Panel config has a strict schema and never saves launch dimensions", "[config][panel]")
{
    Fixture fixture;
    WallpaperConfig config;
    const auto path = fixture.root / "panel.json";
    config.fps = 90;
    config.save(path);
    const auto json = nlohmann::json::parse(std::ifstream(path));
    REQUIRE(json["version"] == 1);
    REQUIRE_FALSE(json.contains("width"));
    REQUIRE_FALSE(json.contains("height"));
    REQUIRE(WallpaperConfig::load(path).fps == 90);
    for (const auto* document :
         {R"({"fps":30})", R"({"version":2})", R"({"version":1,"assets_dir":"a"})",
          R"({"version":1,"fps":1})", R"({"version":1,"mute":1})", R"({"version":1,"scale":0})",
          R"({"version":1,"language":"invalid"})", R"({"version":1,"transform_saved":true})"})
    {
        INFO(document);
        std::ofstream(path) << document;
        REQUIRE_THROWS_AS(WallpaperConfig::load(path), std::invalid_argument);
    }
}

TEST_CASE("Panel save failure preserves the existing preferences and reports failure",
          "[config][panel]")
{
    Fixture fixture;
    const auto blocker = fixture.root / "file-not-directory";
    std::ofstream(blocker) << "retain me";
    WallpaperController controller;
    controller.set_config_path(blocker / "panel.json");
    REQUIRE_THROWS(controller.save_ui_layout({20, 30}, {50, 70}, true));
    std::string content;
    std::ifstream(blocker) >> content;
    REQUIRE(content == "retain");
}

TEST_CASE("Copying a default launch profile derives independent panel storage", "[config][startup]")
{
    Fixture fixture;
    const auto left = fixture.root / "left.json";
    const auto right = fixture.root / "right.json";
    auto profile = LaunchConfig::load(LaunchKind::X11, left);
    auto values = profile.values();
    values.assets_dir = (fixture.root / "assets").string();
    profile.save(values);
    const auto document = nlohmann::json::parse(std::ifstream(left));
    REQUIRE_FALSE(document.contains("state_path"));
    fs::copy_file(left, right);
    const auto copied = LaunchConfig::load(LaunchKind::X11, right);
    REQUIRE(profile.values().state_path == (fixture.root / "left.panel.json").string());
    REQUIRE(copied.values().state_path == (fixture.root / "right.panel.json").string());
}

TEST_CASE("Concurrent atomic config replacement never exposes partial JSON or collides temporaries",
          "[config][startup]")
{
    Fixture fixture;
    const auto path = fixture.root / "shared.json";
    auto initial = LaunchConfig::load(LaunchKind::X11, path);
    auto values = initial.values();
    values.assets_dir = (fixture.root / "assets").string();
    initial.save(values);
    const auto writer = [&](const std::string& output)
    {
        auto instance = LaunchConfig::load(LaunchKind::X11, path);
        auto options = instance.values();
        options.output = output;
        for (int iteration = 0; iteration < 20; ++iteration)
            instance.save(options);
    };
    auto left = std::async(std::launch::async, writer, "left");
    auto right = std::async(std::launch::async, writer, "right");
    for (int iteration = 0; iteration < 40; ++iteration)
    {
        const auto reader = LaunchConfig::load(LaunchKind::X11, path);
        REQUIRE(reader.values().assets_dir == values.assets_dir);
    }
    REQUIRE_NOTHROW(left.get());
    REQUIRE_NOTHROW(right.get());
    REQUIRE(std::distance(fs::directory_iterator(fixture.root), fs::directory_iterator{}) == 1);
}
