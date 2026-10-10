#include <catch2/catch_test_macros.hpp>
#include <nekomote/application/controller.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

namespace
{
using namespace nekomote::application;
namespace fs = std::filesystem;

bool assets_available()
{
    return fs::exists("assets/characters/coconut-casual/model.nkm.json");
}
} // namespace

TEST_CASE("Controller default state", "[controller]")
{
    WallpaperController c;
    REQUIRE(c.switch_state() == SwitchState::Idle);
    REQUIRE(c.fade_alpha() == 1.0f);
    REQUIRE(c.current_character_index() == 0);
    REQUIRE(c.current_background_index() == -1);
    REQUIRE(!c.has_background());
    REQUIRE(!c.paused());
    REQUIRE(c.current_instance() == nullptr);
    REQUIRE(c.current_program() == nullptr);
    REQUIRE(c.previous_instance() == nullptr);
    REQUIRE(c.previous_program() == nullptr);
}

TEST_CASE("Controller poll_candidate idle returns nullopt", "[controller]")
{
    WallpaperController c;
    REQUIRE(!c.poll_candidate().has_value());
}

TEST_CASE("Controller poll_background idle returns nullopt", "[controller]")
{
    WallpaperController c;
    REQUIRE(!c.poll_background().has_value());
}

TEST_CASE("Controller request_switch_character ignored without catalog", "[controller]")
{
    WallpaperController c;
    c.request_switch_character(0);
    REQUIRE(c.switch_state() == SwitchState::Idle);
}

TEST_CASE("Controller request_switch_background ignored without catalog", "[controller]")
{
    WallpaperController c;
    c.request_switch_background(0);
    REQUIRE(!c.poll_background().has_value());
}

TEST_CASE("Controller request_clear_background produces clear request", "[controller]")
{
    WallpaperController c;
    c.request_clear_background();
    const auto req = c.poll_background();
    REQUIRE(req.has_value());
    REQUIRE(req->action == BackgroundAction::Clear);
    REQUIRE(!c.poll_background().has_value());
}

TEST_CASE("Controller pause resume", "[controller]")
{
    WallpaperController c;
    c.set_paused(true);
    REQUIRE(c.paused());
    c.set_paused(false);
    REQUIRE(!c.paused());
}

TEST_CASE("Controller reset no crash without instance", "[controller]")
{
    WallpaperController c;
    c.reset();
    REQUIRE(c.switch_state() == SwitchState::Idle);
}

TEST_CASE("Controller repeat switch request ignored while pending", "[controller]")
{
    if (!assets_available())
        return;
    WallpaperController c;
    REQUIRE(c.scan_assets("assets"));
    const int count = static_cast<int>(c.catalog().characters.size());
    REQUIRE(count > 1);
    ControllerConfig cfg;
    cfg.seed = 1;
    cfg.mute = true;
    cfg.audio = AudioMode::Disabled;
    auto candidate = c.load_initial(0, cfg);
    REQUIRE(candidate.ok);
    c.install_initial(std::move(candidate));
    c.request_switch_character(1);
    REQUIRE(c.switch_state() == SwitchState::Pending);
    c.request_switch_character(2);
    REQUIRE(c.switch_state() == SwitchState::Pending);
    REQUIRE(c.fade_target_character() == 1);
    while (c.switch_state() == SwitchState::Pending)
    {
        auto opt = c.poll_candidate();
        if (opt)
        {
            c.reject_candidate("test rejection");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(c.switch_state() == SwitchState::Idle);
    REQUIRE(c.current_character_index() == 0);
}

TEST_CASE("Controller reject candidate retains current", "[controller]")
{
    if (!assets_available())
        return;
    WallpaperController c;
    REQUIRE(c.scan_assets("assets"));
    const int count = static_cast<int>(c.catalog().characters.size());
    REQUIRE(count > 1);
    ControllerConfig cfg;
    cfg.seed = 1;
    cfg.mute = true;
    cfg.audio = AudioMode::Disabled;
    auto candidate = c.load_initial(0, cfg);
    REQUIRE(candidate.ok);
    c.install_initial(std::move(candidate));
    const auto* original_instance = c.current_instance();
    REQUIRE(original_instance != nullptr);
    c.request_switch_character(1);
    REQUIRE(c.switch_state() == SwitchState::Pending);
    while (c.switch_state() == SwitchState::Pending)
    {
        auto opt = c.poll_candidate();
        if (opt)
        {
            c.reject_candidate("simulated GPU failure");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(c.switch_state() == SwitchState::Idle);
    REQUIRE(c.current_instance() == original_instance);
    REQUIRE(c.current_character_index() == 0);
}

TEST_CASE("Controller confirm candidate switches", "[controller]")
{
    if (!assets_available())
        return;
    WallpaperController c;
    REQUIRE(c.scan_assets("assets"));
    const int count = static_cast<int>(c.catalog().characters.size());
    REQUIRE(count > 1);
    ControllerConfig cfg;
    cfg.seed = 1;
    cfg.mute = true;
    cfg.audio = AudioMode::Disabled;
    auto candidate = c.load_initial(0, cfg);
    REQUIRE(candidate.ok);
    c.install_initial(std::move(candidate));
    const auto* original_instance = c.current_instance();
    c.request_switch_character(1);
    REQUIRE(c.switch_state() == SwitchState::Pending);
    while (c.switch_state() == SwitchState::Pending)
    {
        auto opt = c.poll_candidate();
        if (opt)
        {
            REQUIRE(opt->ok);
            c.confirm_candidate(std::move(*opt));
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(c.switch_state() == SwitchState::CrossFade);
    REQUIRE(c.current_character_index() == 1);
    REQUIRE(c.current_instance() != original_instance);
    REQUIRE(c.previous_instance() == original_instance);
    REQUIRE(c.fade_alpha() == 0.0f);
}

TEST_CASE("Controller update_fade progresses", "[controller]")
{
    if (!assets_available())
        return;
    WallpaperController c;
    REQUIRE(c.scan_assets("assets"));
    ControllerConfig cfg;
    cfg.seed = 1;
    cfg.mute = true;
    cfg.audio = AudioMode::Disabled;
    auto candidate = c.load_initial(0, cfg);
    REQUIRE(candidate.ok);
    c.install_initial(std::move(candidate));
    c.request_switch_character(1);
    while (c.switch_state() == SwitchState::Pending)
    {
        auto opt = c.poll_candidate();
        if (opt)
        {
            c.confirm_candidate(std::move(*opt));
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(c.switch_state() == SwitchState::CrossFade);
    REQUIRE(c.fade_alpha() == 0.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    c.update_fade();
    REQUIRE(c.fade_alpha() > 0.0f);
    REQUIRE(c.fade_alpha() < 1.0f);
    std::this_thread::sleep_for(
        std::chrono::milliseconds(static_cast<int>(kCharacterFadeSeconds * 1000) + 100));
    c.update_fade();
    REQUIRE(c.fade_alpha() == 1.0f);
    REQUIRE(c.switch_state() == SwitchState::Idle);
    REQUIRE(c.previous_instance() == nullptr);
}

TEST_CASE("Controller clear_previous", "[controller]")
{
    if (!assets_available())
        return;
    WallpaperController c;
    REQUIRE(c.scan_assets("assets"));
    ControllerConfig cfg;
    cfg.seed = 1;
    cfg.mute = true;
    cfg.audio = AudioMode::Disabled;
    auto candidate = c.load_initial(0, cfg);
    REQUIRE(candidate.ok);
    c.install_initial(std::move(candidate));
    const auto* original = c.current_instance();
    auto candidate2 = c.load_candidate(c.catalog().characters[1], cfg);
    REQUIRE(candidate2.ok);
    c.install_candidate_sync(std::move(candidate2));
    REQUIRE(c.current_instance() != original);
    REQUIRE(c.previous_instance() == original);
    c.clear_previous();
    REQUIRE(c.previous_instance() == nullptr);
    REQUIRE(c.previous_program() == nullptr);
}

TEST_CASE("Controller config save load", "[controller]")
{
    WallpaperController c;
    const fs::path path = fs::temp_directory_path() / "nekomote_controller_test_config.json";
    c.config().character = "test_character";
    c.config().background = "test_background";
    c.config().scale = 1.5f;
    c.config().fps = 60;
    c.set_config_path(path);
    c.save_config(path);
    WallpaperController c2;
    c2.load_config(path);
    REQUIRE(c2.config().character == "test_character");
    REQUIRE(c2.config().background == "test_background");
    REQUIRE(c2.config().scale == 1.5f);
    REQUIRE(c2.config().fps == 60);
    fs::remove(path);
}
