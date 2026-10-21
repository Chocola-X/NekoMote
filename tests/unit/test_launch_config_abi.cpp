#include <catch2/catch_test_macros.hpp>

#include <nekomote/abi.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace
{
namespace fs = std::filesystem;
using Launch = std::unique_ptr<nekomote_launch_config, decltype(&nekomote_launch_config_destroy)>;
struct Fixture
{
    fs::path root = fs::temp_directory_path() /
                    ("nekomote-launch-abi-" + std::to_string(::getpid()) + "-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture()
    {
        fs::create_directory(root);
    }
    ~Fixture()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};
} // namespace

TEST_CASE("Launch C ABI validates handles and enum values before accessing storage",
          "[abi][config]")
{
    Fixture fixture;
    nekomote_launch_config* output = nullptr;
    const auto path = fixture.root / "launch.json";
    REQUIRE(nekomote_launch_config_load(999, path.c_str(), &output) ==
            NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(output == nullptr);
    REQUIRE(nekomote_launch_config_load(NEKOMOTE_LAUNCH_X11, "", &output) ==
            NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_launch_config_load(NEKOMOTE_LAUNCH_X11, path.c_str(), nullptr) ==
            NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_launch_config_path(nullptr) == nullptr);
    REQUIRE(nekomote_last_error() == NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_launch_config_values(nullptr).assets_dir == nullptr);
    REQUIRE(nekomote_launch_config_save(nullptr, {}) == NEKOMOTE_ERROR_INVALID_INPUT);
    nekomote_launch_config_destroy(nullptr);
    REQUIRE_FALSE(fs::exists(path));
}

TEST_CASE("Launch C ABI owns copied options and validates failures with a live handle",
          "[abi][config]")
{
    Fixture fixture;
    const auto path = fixture.root / "instance.json";
    nekomote_launch_config* raw = nullptr;
    REQUIRE(nekomote_launch_config_load(NEKOMOTE_LAUNCH_LAYER_SHELL, path.c_str(), &raw) ==
            NEKOMOTE_OK);
    Launch config(raw, nekomote_launch_config_destroy);
    REQUIRE(config);
    REQUIRE(std::string(nekomote_launch_config_path(config.get())) == path.string());
    auto values = nekomote_launch_config_values(config.get());
    for (const char* field : {values.assets_dir, values.model_path, values.session_path,
                              values.output, values.backend, values.input, values.state_path})
        REQUIRE(field != nullptr);
    REQUIRE(std::string(values.state_path) == (fixture.root / "instance.panel.json").string());
    REQUIRE(nekomote_launch_config_save(config.get(), values) == NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE_FALSE(fs::exists(path));
    std::string assets = (fixture.root / "assets").string();
    values.assets_dir = assets.c_str();
    values.backend = "vulkan";
    values.input = "passthrough";
    REQUIRE(nekomote_launch_config_save(config.get(), values) == NEKOMOTE_OK);
    assets.assign(assets.size(), 'x');
    values = nekomote_launch_config_values(config.get());
    REQUIRE(std::string(values.assets_dir) == (fixture.root / "assets").string());
    REQUIRE(std::string(values.backend) == "vulkan");
    values.width = 0;
    REQUIRE(nekomote_launch_config_save(config.get(), values) == NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(std::string(nekomote_error_message()).find("width") != std::string::npos);
    REQUIRE(nekomote_launch_config_values(config.get()).width == 1920);
    REQUIRE(nekomote_launch_config_load(NEKOMOTE_LAUNCH_LAYER_SHELL, path.c_str(), &raw) ==
            NEKOMOTE_OK);
    Launch reopened(raw, nekomote_launch_config_destroy);
    REQUIRE(std::string(nekomote_launch_config_values(reopened.get()).backend) == "vulkan");
    REQUIRE_FALSE(fs::exists(fixture.root / "instance.panel.json"));
}

TEST_CASE("Launch C ABI returns schema and I/O diagnostics without exceptions", "[abi][config]")
{
    Fixture fixture;
    const auto path = fixture.root / "bad.json";
    std::ofstream(path) << "{\"version\":2}";
    nekomote_launch_config* raw = nullptr;
    REQUIRE(nekomote_launch_config_load(NEKOMOTE_LAUNCH_PLAYER, path.c_str(), &raw) ==
            NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(raw == nullptr);
    REQUIRE(std::string(nekomote_error_message()).find(path.string()) != std::string::npos);
    const auto directory = fixture.root / "directory";
    fs::create_directory(directory);
    REQUIRE(nekomote_launch_config_load(NEKOMOTE_LAUNCH_X11, directory.c_str(), &raw) ==
            NEKOMOTE_ERROR_IO);
    REQUIRE(raw == nullptr);
    REQUIRE(std::string(nekomote_error_message()).find(directory.string()) != std::string::npos);
}
