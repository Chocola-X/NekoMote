#include <catch2/catch_approx.hpp>
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

TEST_CASE("Controller left hold drags after threshold without an activation jump",
          "[controller][pointer]")
{
    WallpaperController c;
    using namespace std::chrono_literals;
    const WallpaperController::PointerTime start{1s};
    c.set_origin({20, 30});
    c.pointer_press({100, 100}, start);
    c.pointer_move({110, 100}, start + 299ms);
    REQUIRE(c.origin().x == 20.0f);
    c.pointer_move({120, 115}, start + 300ms);
    REQUIRE(c.origin().x == 30.0f);
    REQUIRE(c.origin().y == 45.0f);
    c.pointer_release({125, 120}, start + 400ms);
    REQUIRE(c.origin().x == 35.0f);
    REQUIRE(c.origin().y == 50.0f);
    c.pointer_move({900, 900}, start + 500ms);
    REQUIRE(c.origin().x == 35.0f);
    REQUIRE(c.origin().y == 50.0f);
}

TEST_CASE("Controller short click cannot leave a drag active", "[controller][pointer]")
{
    WallpaperController c;
    using namespace std::chrono_literals;
    const WallpaperController::PointerTime start{1s};
    c.pointer_press({100, 100}, start);
    c.pointer_release({101, 101}, start + 50ms);
    c.pointer_move({200, 200}, start + 1s);
    REQUIRE(c.origin().x == 0.0f);
    REQUIRE(c.origin().y == 400.0f);
}

TEST_CASE("Controller short click reacts on release but long hold does not react",
          "[controller][pointer][integration]")
{
    const fs::path directory = "assets/characters/chocola-date-a";
    if (!fs::exists(directory / "model.nkm.json") || !fs::exists(directory / "session.nks.json"))
        SKIP("chocola-date-a assets are unavailable");
    WallpaperController c;
    ControllerConfig config;
    config.mute = true;
    config.audio = AudioMode::Disabled;
    auto candidate = c.load_initial(CharacterEntry{"chocola-date-a", directory / "model.nkm.json",
                                                   directory / "session.nks.json", directory},
                                    config);
    INFO(candidate.error);
    REQUIRE(candidate.ok);
    c.install_initial(std::move(candidate));
    auto* session = c.current_instance();
    const auto regions = session->touch_regions();
    REQUIRE(regions.front().has_value());
    const auto point = regions.front()->center;
    using namespace std::chrono_literals;
    const WallpaperController::PointerTime start{1s};
    c.pointer_press(point, start);
    REQUIRE(session->reaction_phase() == ReactionPhase::Idle);
    SECTION("short release selects a reaction")
    {
        c.pointer_release(point, start + 50ms);
        REQUIRE(session->reaction_phase() == ReactionPhase::Playing);
    }
    SECTION("long hold translates without triggering a reaction")
    {
        c.pointer_move({point.x + 20, point.y + 30}, start + 350ms);
        c.pointer_release({point.x + 20, point.y + 30}, start + 360ms);
        REQUIRE(session->reaction_phase() == ReactionPhase::Idle);
        REQUIRE(c.origin().x == 20.0f);
        REQUIRE(c.origin().y == 430.0f);
    }
}

TEST_CASE("Controller pointer gesture cancels at lifecycle boundaries", "[controller][pointer]")
{
    WallpaperController c;
    using namespace std::chrono_literals;
    const WallpaperController::PointerTime start{1s};
    c.pointer_press({10, 10}, start);
    SECTION("leave")
    {
        c.pointer_leave();
    }
    SECTION("UI consumes input")
    {
        c.cancel_pointer_gesture();
    }
    SECTION("pause")
    {
        c.set_paused(true);
        c.set_paused(false);
    }
    SECTION("minimize")
    {
        c.set_minimized(true);
        c.set_minimized(false);
    }
    SECTION("output unavailable")
    {
        c.set_output_unavailable(true);
        c.set_output_unavailable(false);
    }
    SECTION("reset")
    {
        c.reset();
    }
    c.pointer_move({300, 300}, start + 1s);
    REQUIRE(c.origin().x == 0.0f);
    REQUIRE(c.origin().y == 400.0f);
}

TEST_CASE("Controller wheel zoom keeps the model point under the pointer", "[controller][pointer]")
{
    WallpaperController c;
    c.set_origin({20, 30});
    c.set_scale(1);
    const nekomote::Vec2 pointer{100, 200};
    const nekomote::Vec2 model_point{80, 170};
    for (const double wheel : {1.0, -2.0, 100.0, -100.0})
    {
        c.zoom_at(pointer, wheel);
        REQUIRE((pointer.x - c.origin().x) / c.scale() ==
                Catch::Approx(model_point.x).margin(.001));
        REQUIRE((pointer.y - c.origin().y) / c.scale() ==
                Catch::Approx(model_point.y).margin(.001));
        REQUIRE(c.scale() >= .05f);
        REQUIRE(c.scale() <= 5.0f);
    }
}

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
