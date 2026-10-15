#include <catch2/catch_test_macros.hpp>

#include <fcntl.h>
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

nekomote_gl_proc no_gl(const char*)
{
    return nullptr;
}

using Wallpaper = std::unique_ptr<nekomote_wallpaper, decltype(&nekomote_wallpaper_destroy)>;

Wallpaper wallpaper()
{
    Wallpaper result{nekomote_wallpaper_create(no_gl), nekomote_wallpaper_destroy};
    REQUIRE(result);
    return result;
}

struct ConfigFixture
{
    fs::path root = fs::temp_directory_path() /
                    ("nekomote-config-" + std::to_string(::getpid()) + "-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path assets = root / "assets";

    ConfigFixture()
    {
        // Asset discovery only needs the names; these tests never load/render.
        const auto model_dir = assets / "characters" / "synthetic" / "default";
        fs::create_directories(model_dir);
        std::ofstream(model_dir / "model.nkm.json") << "{}";
        std::ofstream(model_dir / "session.nks.json") << "{}";
        std::ofstream(assets / "config.json") << R"({"fps":90})";
    }

    ~ConfigFixture()
    {
        std::error_code ec;
        fs::permissions(root / "readonly", fs::perms::owner_all, fs::perm_options::add, ec);
        fs::remove_all(root, ec);
    }

    fs::path config(const std::string& name, int fps)
    {
        const auto path = root / name;
        std::ofstream(path) << "{\"fps\":" << fps << "}";
        return path;
    }
};
} // namespace

TEST_CASE("Wallpaper configuration storage is copied and isolated per instance", "[abi][wallpaper]")
{
    ConfigFixture fixture;
    const auto path_a = fixture.config("output-a.json", 30);
    const auto path_b = fixture.config("output-b.json", 45);
    auto a = wallpaper();
    auto b = wallpaper();
    auto default_storage = wallpaper();
    std::string copied_path = path_a.string();
    REQUIRE(nekomote_wallpaper_set_config_path(a.get(), copied_path.c_str()) == NEKOMOTE_OK);
    copied_path.assign(copied_path.size(), 'x');
    REQUIRE(nekomote_wallpaper_set_config_path(b.get(), path_b.c_str()) == NEKOMOTE_OK);
    for (auto* handle : {a.get(), b.get(), default_storage.get()})
        REQUIRE(nekomote_wallpaper_set_assets_dir(handle, fixture.assets.c_str()) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_fps_limit(a.get()) == 30);
    REQUIRE(nekomote_wallpaper_fps_limit(b.get()) == 45);
    REQUIRE(nekomote_wallpaper_fps_limit(default_storage.get()) == 90);
    REQUIRE(nekomote_wallpaper_set_config_path(a.get(), path_b.c_str()) == NEKOMOTE_ERROR_BUSY);
    REQUIRE(nekomote_wallpaper_fps_limit(a.get()) == 30);
}

TEST_CASE("Wallpaper config path rejects invalid input and locks at asset setup",
          "[abi][wallpaper]")
{
    ConfigFixture fixture;
    auto wp = wallpaper();
    REQUIRE(nekomote_wallpaper_set_config_path(wp.get(), nullptr) == NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_wallpaper_set_config_path(wp.get(), "") == NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_wallpaper_set_config_path(wp.get(), "/") == NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_wallpaper_set_config_path(wp.get(), ".") == NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_wallpaper_set_config_path(wp.get(), "..") == NEKOMOTE_ERROR_INVALID_INPUT);
    const auto first = fixture.config("first.json", 30);
    const auto second = fixture.config("second.json", 45);
    REQUIRE(nekomote_wallpaper_set_config_path(wp.get(), first.c_str()) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_set_config_path(wp.get(), second.c_str()) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_set_assets_dir(wp.get(), (fixture.root / "absent").c_str()) ==
            NEKOMOTE_ERROR_NOT_FOUND);
    REQUIRE(nekomote_wallpaper_set_config_path(wp.get(), first.c_str()) == NEKOMOTE_ERROR_BUSY);
    REQUIRE(nekomote_wallpaper_set_assets_dir(wp.get(), fixture.assets.c_str()) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_fps_limit(wp.get()) == 45);
}

TEST_CASE("Wallpaper explicit config creates parents only when saves are enabled",
          "[abi][wallpaper]")
{
    ConfigFixture fixture;
    const auto path = fixture.root / "new" / "nested" / "config.json";
    auto wp = wallpaper();
    REQUIRE(nekomote_wallpaper_set_no_save(wp.get(), 1) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_set_config_path(wp.get(), path.c_str()) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_set_assets_dir(wp.get(), fixture.assets.c_str()) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_fps_limit(wp.get()) == 60);
    REQUIRE_FALSE(fs::exists(path.parent_path()));
    REQUIRE(nekomote_wallpaper_set_no_save(wp.get(), 0) == NEKOMOTE_OK);
    REQUIRE(fs::is_directory(path.parent_path()));
    REQUIRE_FALSE(fs::exists(path));
}

TEST_CASE("Wallpaper explicit config never falls back from unusable storage", "[abi][wallpaper]")
{
    ConfigFixture fixture;
    const auto directory = fixture.root / "not-a-file";
    fs::create_directory(directory);
    auto wp = wallpaper();
    REQUIRE(nekomote_wallpaper_set_config_path(wp.get(), directory.c_str()) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_set_assets_dir(wp.get(), fixture.assets.c_str()) ==
            NEKOMOTE_ERROR_IO);
    REQUIRE(std::string(nekomote_error_message()).find(directory.string()) != std::string::npos);
    REQUIRE(nekomote_wallpaper_set_no_save(wp.get(), 1) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_set_assets_dir(wp.get(), fixture.assets.c_str()) ==
            NEKOMOTE_ERROR_IO);
    // A storage failure does not allow changing the instance's storage identity.
    REQUIRE(nekomote_wallpaper_set_config_path(wp.get(), (fixture.root / "other.json").c_str()) ==
            NEKOMOTE_ERROR_BUSY);
}

TEST_CASE("Wallpaper no-save reads read-only storage without enabling writes", "[abi][wallpaper]")
{
    ConfigFixture fixture;
    const auto directory = fixture.root / "readonly";
    fs::create_directory(directory);
    const auto path = directory / "config.json";
    std::ofstream(path) << R"({"fps":24})";
    fs::permissions(directory, fs::perms::owner_read | fs::perms::owner_exec);
    // Root may bypass ordinary mode bits; do not report an unexecuted negative as PASS.
    if (::faccessat(AT_FDCWD, directory.c_str(), W_OK, AT_EACCESS) == 0)
        SKIP("effective credentials can write the read-only test directory");
    auto wp = wallpaper();
    REQUIRE(nekomote_wallpaper_set_config_path(wp.get(), path.c_str()) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_set_assets_dir(wp.get(), fixture.assets.c_str()) ==
            NEKOMOTE_ERROR_IO);
    REQUIRE(nekomote_wallpaper_set_no_save(wp.get(), 1) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_set_assets_dir(wp.get(), fixture.assets.c_str()) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_fps_limit(wp.get()) == 24);
    REQUIRE(nekomote_wallpaper_set_no_save(wp.get(), 0) == NEKOMOTE_ERROR_IO);
    REQUIRE(nekomote_wallpaper_fps_limit(wp.get()) == 24);
}
