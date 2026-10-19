#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nekomote/asset_io/asset_file.hpp>
#include <nlohmann/json.hpp>

#include <nekomote/abi.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>

#include "test_abi_allocations.hpp"

namespace
{
using nekomote::test::abi_allocations::allocations;
using nekomote::test::abi_allocations::fail_allocations;
using nekomote::test::abi_allocations::track_allocations;
} // namespace

namespace
{
namespace fs = std::filesystem;
struct Fixture
{
    fs::path path = fs::temp_directory_path() /
                    ("nekomote-abi-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::unique_ptr<nekomote_model, decltype(&nekomote_model_destroy)> model{
        nullptr, nekomote_model_destroy};
    std::unique_ptr<nekomote_session_asset, decltype(&nekomote_session_asset_destroy)> asset{
        nullptr, nekomote_session_asset_destroy};
    std::unique_ptr<nekomote_program, decltype(&nekomote_program_destroy)> program{
        nullptr, nekomote_program_destroy};
    Fixture()
    {
        REQUIRE(fs::create_directory(path));
        const fs::path root{NEKOMOTE_FIXTURES_DIR};
        auto imported = nekomote::asset_io::import_emote(
            {root / "minimal.json", root / "minimal.resx.json", root, {}});
        REQUIRE(imported.ok);
        auto& native = imported.asset;
        for (auto& part : native.parts)
            for (auto& layer : part.layers)
                layer.inherit_mask = 0x20007fc;
        native.base_group = "body_parts";
        native.base_motion = "torso";
        nekomote::model::Timeline idle;
        idle.label = "idle";
        idle.loop_begin = 0;
        idle.loop_end = 30;
        idle.tracks.push_back({"move_UD",
                               {{0,
                                 2,
                                 {nekomote::model::FieldState::Value, 0.f},
                                 {nekomote::model::FieldState::Value, 0.f}},
                                {30, 0, {}, {}}}});
        native.timelines.push_back(idle);
        REQUIRE(nekomote::asset_io::write_bundle(imported, root, path / "model").empty());
        const nlohmann::json document{{"schema", "nekomote.session"},
                                      {"version", 1},
                                      {"session",
                                       {{"model", "synthetic"},
                                        {"idle_timeline", "idle"},
                                        {"talk_max", 5},
                                        {"interaction",
                                         {{"eye", nullptr},
                                          {"regions",
                                           {{"bust", nullptr},
                                            {"eye", nullptr},
                                            {"face", nullptr},
                                            {"head", nullptr},
                                            {"pant", nullptr}}}}},
                                        {"reactions", nlohmann::json::array()}}}};
        {
            std::ofstream out(path / "session.nks.json");
            out << document;
        }
        model.reset(nekomote_model_load((path / "model/model.nkm").c_str()));
        asset.reset(nekomote_session_asset_load((path / "session.nks.json").c_str()));
        REQUIRE(model);
        REQUIRE(asset);
        program.reset(nekomote_program_compile(model.get(), asset.get()));
        INFO((nekomote_error_message() ? nekomote_error_message() : "no error"));
        REQUIRE(program);
    }
    ~Fixture()
    {
        std::error_code error;
        fs::remove_all(path, error);
    }
};
} // namespace

TEST_CASE("CPU ABI rejects invalid values on live synthetic sessions", "[abi]")
{
    Fixture fixture;
    nekomote_session_config config{1, NEKOMOTE_POINTER_UNAVAILABLE, NEKOMOTE_AUDIO_DISABLED};
    auto invalid = config;
    invalid.pointer = static_cast<nekomote_pointer_scope>(999);
    REQUIRE_FALSE(nekomote_session_create(fixture.program.get(), invalid));
    REQUIRE(nekomote_last_error() == NEKOMOTE_ERROR_INVALID_INPUT);
    auto* session = nekomote_session_create(fixture.program.get(), config);
    REQUIRE(session);
    const std::unique_ptr<nekomote_session, decltype(&nekomote_session_destroy)> owner(
        session, nekomote_session_destroy);
    REQUIRE(nekomote_session_start(session) == NEKOMOTE_OK);
    REQUIRE(nekomote_session_prepare_frame(session) == NEKOMOTE_OK);
    REQUIRE(nekomote_session_frame(session).command_count > 0);
    REQUIRE(nekomote_session_set_playback_state(session, static_cast<nekomote_playback_state>(
                                                             999)) == NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_session_advance(session, std::numeric_limits<double>::quiet_NaN()) ==
            NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_last_error() == NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_session_set_output_size(session, 0, 10) == NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_session_set_output_size(session, 800, 600) == NEKOMOTE_OK);
    REQUIRE(nekomote_session_set_background(session, 1, 800, 600) == NEKOMOTE_OK);
    REQUIRE(nekomote_session_fade_background(session, 2, 800, 600, 1) == NEKOMOTE_OK);
    REQUIRE(nekomote_session_background_frame(session).command_count == 2);
    allocations = 0;
    track_allocations = true;
    bool valid = true;
    for (int i = 0; i < 120; ++i)
    {
        valid &= nekomote_session_advance(session, 1.0 / 60.0) == NEKOMOTE_OK;
        valid &= nekomote_session_prepare_frame(session) == NEKOMOTE_OK;
        valid &= nekomote_session_frame(session).command_count > 0;
        (void)nekomote_session_background_frame(session);
    }
    track_allocations = false;
    REQUIRE(valid);
    REQUIRE(allocations == 0);
    REQUIRE(nekomote_session_advance(session, 1.0 / 60.0) == NEKOMOTE_OK);
    REQUIRE(nekomote_session_frame(session).command_count == 0);
    REQUIRE(nekomote_session_prepare_frame(session) == NEKOMOTE_OK);
    REQUIRE(nekomote_session_frame(session).command_count > 0);
    // OOM diagnostics themselves must not throw or leak partial wrappers.
    fail_allocations = true;
    auto* failed = nekomote_session_create(fixture.program.get(), config);
    fail_allocations = false;
    REQUIRE_FALSE(failed);
    REQUIRE(nekomote_last_error() == NEKOMOTE_ERROR_OUT_OF_MEMORY);
    REQUIRE(nekomote_error_message());
}

namespace
{
nekomote_gl_proc null_wallpaper_loader(const char*)
{
    return nullptr;
}
} // namespace

TEST_CASE("Wallpaper valid handle rejects invalid CPU inputs and seals OOM", "[abi][wallpaper]")
{
    std::unique_ptr<nekomote_wallpaper, decltype(&nekomote_wallpaper_destroy)> wallpaper{
        nekomote_wallpaper_create(null_wallpaper_loader), nekomote_wallpaper_destroy};
    REQUIRE(wallpaper);
    REQUIRE(nekomote_wallpaper_set_output_available(wallpaper.get(), 999) ==
            NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_wallpaper_set_paused(wallpaper.get(), 1) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_set_output_available(wallpaper.get(), 0) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_set_output_available(wallpaper.get(), 1) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_paused(wallpaper.get()) == 1);
    REQUIRE(nekomote_wallpaper_set_paused(wallpaper.get(), 0) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_set_ui_open(wallpaper.get(), 999) == NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_wallpaper_set_ui_open(wallpaper.get(), 1) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_set_ui_open(wallpaper.get(), 0) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_set_output_size(wallpaper.get(), -1, 32) ==
            NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_wallpaper_advance(wallpaper.get(), -1) == NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_wallpaper_advance(wallpaper.get(), std::numeric_limits<double>::quiet_NaN()) ==
            NEKOMOTE_ERROR_INVALID_INPUT);
    const nekomote_vk_target target{nullptr, nullptr, 32, 32};
    REQUIRE(nekomote_wallpaper_set_vk_target(wallpaper.get(), target) ==
            NEKOMOTE_ERROR_UNSUPPORTED);
    // Use a live wallpaper and valid required arguments. Allocation failure must
    // be converted while constructing filesystem input, before any GL access.
    const std::string long_path(2048, 'x');
    fail_allocations = true;
    const auto error =
        nekomote_wallpaper_load_model(wallpaper.get(), long_path.c_str(), long_path.c_str());
    fail_allocations = false;
    REQUIRE(error == NEKOMOTE_ERROR_OUT_OF_MEMORY);
    REQUIRE(nekomote_last_error() == NEKOMOTE_ERROR_OUT_OF_MEMORY);
}
