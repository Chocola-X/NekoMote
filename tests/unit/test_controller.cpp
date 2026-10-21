#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nekomote/application/controller.hpp>
#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/audio_import.hpp>
#include <nekomote/asset_io/import_emote.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>

namespace
{
using namespace nekomote::application;
namespace fs = std::filesystem;

bool assets_available()
{
    return fs::exists("assets/characters/coconut/casual/model.nkm");
}
struct LayoutFixture
{
    fs::path path = fs::temp_directory_path() /
                    ("nekomote-layout-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CharacterEntry entry;
    LayoutFixture()
    {
        const fs::path root{NEKOMOTE_FIXTURES_DIR};
        auto imported = nekomote::asset_io::import_emote(
            {root / "minimal.json", root / "minimal.resx.json", root, {}});
        REQUIRE(imported.ok);
        for (auto& part : imported.asset.parts)
            for (auto& layer : part.layers)
                layer.inherit_mask = 0x20007fc;
        imported.asset.base_group = "body_parts";
        imported.asset.base_motion = "torso";
        imported.asset.author_scale = {nekomote::model::FieldState::Value, 1.0f};
        imported.asset.variables.push_back({"drag_x", {}});
        imported.asset.variables.push_back({"drag_y", {}});
        nekomote::model::Layer anchor;
        anchor.label = "drag_anchor";
        anchor.type = 1;
        anchor.inherit_mask = 0x200020c;
        anchor.frame_list.push_back({0, 2, {nekomote::model::FieldState::Value, {}}});
        anchor.parameter = {{"drag_x", -30, 30, 1, 0, true}, {"drag_y", -30, 30, 1, 0, true}};
        imported.asset.parts[0].layers.push_back(anchor);
        ++imported.asset.total_layers;
        ++imported.asset.total_frames;
        nekomote::model::SpringControl spring;
        spring.binding = {"/metadata/bustControl/0",
                          "drag",
                          "drag_anchor",
                          "standard",
                          "drag_x",
                          "drag_y",
                          true,
                          {}};
        spring.stiffness = 0.03125f;
        spring.friction = 0.125f;
        spring.axis_scale = {1, 1};
        imported.asset.spring_controls.push_back(spring);
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
        imported.asset.timelines.push_back(idle);
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
        std::ofstream(path / "session.nks.json") << document;
        entry = {"synthetic", path / "model/model.nkm", path / "session.nks.json", path};
    }
    ~LayoutFixture()
    {
        fs::remove_all(path);
    }
    void install(WallpaperController& controller)
    {
        ControllerConfig cfg;
        cfg.audio = AudioMode::Disabled;
        auto candidate = controller.load_initial(entry, cfg);
        INFO(candidate.error);
        REQUIRE(candidate.ok);
        controller.install_initial(std::move(candidate));
    }
};
} // namespace

TEST_CASE("Controller held left button drags immediately after movement tolerance",
          "[controller][pointer]")
{
    WallpaperController c;
    using namespace std::chrono_literals;
    const WallpaperController::PointerTime start{1s};
    c.set_origin({20, 30});
    c.pointer_press({100, 100}, start);
    c.pointer_move({103, 100}, start + 1ms);
    REQUIRE(c.origin().x == 20.0f);
    c.pointer_move({110, 100}, start + 2ms);
    REQUIRE(c.origin().x == 27.0f);
    c.pointer_move({120, 115}, start + 3ms);
    REQUIRE(c.origin().x == 37.0f);
    REQUIRE(c.origin().y == 45.0f);
    c.pointer_release({125, 120}, start + 400ms);
    REQUIRE(c.origin().x == 42.0f);
    REQUIRE(c.origin().y == 50.0f);
    c.pointer_move({900, 900}, start + 500ms);
    REQUIRE(c.origin().x == 42.0f);
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

TEST_CASE("Controller drives physics during a held drag without accumulating its distance",
          "[controller][pointer]")
{
    LayoutFixture fixture;
    WallpaperController controller;
    fixture.install(controller);
    controller.advance(1.0 / 60);
    const auto x = controller.current_program()->playback().scene().find_variable("drag_x");
    const auto value = [&]
    { return controller.current_instance()->playback().values()[x.value - 1].value; };
    REQUIRE(value() == 0.0f);
    controller.pointer_press({0, 0});
    for (int frame = 1; frame <= 120; ++frame)
    {
        controller.pointer_move({static_cast<float>(frame * 30), 0});
        controller.prepare_frame();
        controller.advance(1.0 / 60);
        REQUIRE(controller.last_error());
        if (frame == 1)
            REQUIRE(value() < 0.f); // immediate response, no release event
    }
    REQUIRE(controller.origin().x == 3600.f);
    REQUIRE(value() < -10.f); // sustained speed applies a bounded force to the spring
    const float held = value();
    controller.pointer_release({3600, 0});
    controller.advance(1.0 / 60);
    REQUIRE(controller.last_error());
    REQUIRE(value() > held); // releasing injects the opposite impulse on the next tick
}

TEST_CASE("Controller short click reacts on release but long hold does not react",
          "[controller][pointer][integration]")
{
    const fs::path directory = "assets/characters/chocola/date-a";
    if (!fs::exists(directory / "model.nkm") || !fs::exists(directory / "session.nks.json"))
        SKIP("chocola/date-a assets are unavailable");
    WallpaperController c;
    ControllerConfig config;
    config.mute = true;
    config.audio = AudioMode::Disabled;
    auto candidate = c.load_initial(CharacterEntry{"chocola-date-a", directory / "model.nkm",
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

TEST_CASE("Controller character transform reset restores its first fitted state",
          "[controller][integration][fit]")
{
    const fs::path directory = "assets/characters/chocola/date-a";
    if (!fs::exists(directory / "model.nkm") || !fs::exists(directory / "session.nks.json"))
        SKIP("chocola/date-a assets are unavailable");

    WallpaperController c;
    ControllerConfig config;
    config.audio = AudioMode::Disabled;
    auto candidate = c.load_initial(CharacterEntry{"chocola-date-a", directory / "model.nkm",
                                                   directory / "session.nks.json", directory},
                                    config);
    INFO(candidate.error);
    REQUIRE(candidate.ok);
    c.install_initial(std::move(candidate));
    c.set_output_size(1920, 1080);
    c.prepare_frame();
    c.fit_to_output(1920, 1080);
    const auto initial_origin = c.origin();
    const auto initial_scale = c.scale();

    for (int i = 0; i < 5; ++i)
    {
        c.set_origin({static_cast<float>(i * 37), static_cast<float>(i * -29)});
        c.set_scale(.25f + static_cast<float>(i) * .1f);
        c.reset_character_transform();
        REQUIRE(c.origin().x == Catch::Approx(initial_origin.x));
        REQUIRE(c.origin().y == Catch::Approx(initial_origin.y));
        REQUIRE(c.scale() == Catch::Approx(initial_scale));
    }
}

TEST_CASE("Controller default state", "[controller]")
{
    WallpaperController c;
    REQUIRE(c.config().fps == 60);
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

TEST_CASE("Controller repeat switch request ignored while pending", "[controller][integration]")
{
    if (!assets_available())
        SKIP("coconut/casual assets are unavailable");
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

TEST_CASE("Controller reject candidate retains current", "[controller][integration]")
{
    if (!assets_available())
        SKIP("coconut/casual assets are unavailable");
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

TEST_CASE("Controller confirm candidate switches", "[controller][integration]")
{
    if (!assets_available())
        SKIP("coconut/casual assets are unavailable");
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

TEST_CASE("Controller character fade freezes with playback policy and bounds recovery time",
          "[controller][lifecycle]")
{
    LayoutFixture fixture;
    WallpaperController c;
    fixture.install(c);
    ControllerConfig cfg;
    cfg.audio = AudioMode::Disabled;
    auto next = c.load_initial(fixture.entry, cfg);
    REQUIRE(next.ok);
    c.confirm_candidate(std::move(next));
    REQUIRE(c.previous_instance());
    REQUIRE(c.fade_alpha() == 0.0f);
    SECTION("user pause survives host hide and show")
    {
        c.set_paused(true);
        c.set_output_unavailable(true);
        c.set_output_unavailable(false);
        REQUIRE(c.paused());
        c.advance(0.2);
        REQUIRE(c.fade_alpha() == 0.0f);
        c.set_paused(false);
    }
    SECTION("host unavailable")
    {
        c.set_output_unavailable(true);
        c.advance(10);
        REQUIRE(c.fade_alpha() == 0.0f);
        c.set_output_unavailable(false);
    }
    SECTION("minimized")
    {
        c.set_minimized(true);
        c.advance(10);
        REQUIRE(c.fade_alpha() == 0.0f);
        c.set_minimized(false);
    }
    c.advance(10);
    REQUIRE(c.fade_alpha() == Catch::Approx(0.625f));
    REQUIRE(c.previous_instance());
    c.advance(0.15);
    REQUIRE(c.fade_alpha() == 1.0f);
    REQUIRE(c.switch_state() == SwitchState::Idle);
    REQUIRE(c.previous_instance() == nullptr);
}

TEST_CASE("Controller fading session audio cannot fill an unconsumed queue",
          "[controller][lifecycle][audio]")
{
    LayoutFixture fixture;
    WallpaperController c;
    ControllerConfig cfg;
    cfg.audio = AudioMode::Device;
    auto initial = c.load_initial(fixture.entry, cfg);
    REQUIRE(initial.ok);
    c.install_initial(std::move(initial));
    auto next = c.load_initial(fixture.entry, cfg);
    REQUIRE(next.ok);
    c.confirm_candidate(std::move(next));
    for (int i = 0; i < 20; ++i)
    {
        c.set_output_unavailable(true);
        c.advance(0);
        while (c.pop_audio_command())
        {
        }
        c.set_output_unavailable(false);
        c.advance(0);
        while (c.pop_audio_command())
        {
        }
        REQUIRE(c.last_error());
    }
    c.advance(0.2);
    c.advance(0.2);
    REQUIRE(c.last_error());
    REQUIRE(c.switch_state() == SwitchState::Idle);
    REQUIRE(c.previous_instance() == nullptr);
}

TEST_CASE("Controller character fade advances with playback time", "[controller][integration]")
{
    if (!assets_available())
        SKIP("coconut/casual assets are unavailable");
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
    c.advance(0.1);
    REQUIRE(c.fade_alpha() > 0.0f);
    REQUIRE(c.fade_alpha() < 1.0f);
    c.advance(0.2);
    c.advance(0.2);
    REQUIRE(c.fade_alpha() == 1.0f);
    REQUIRE(c.switch_state() == SwitchState::Idle);
    REQUIRE(c.previous_instance() == nullptr);
}

TEST_CASE("Controller clear_previous", "[controller][integration]")
{
    if (!assets_available())
        SKIP("coconut/casual assets are unavailable");
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
    c.request_switch_character(1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (true)
    {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        if (auto candidate2 = c.poll_candidate())
        {
            INFO(candidate2->error);
            REQUIRE(candidate2->ok);
            c.confirm_candidate(std::move(*candidate2));
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(c.current_instance() != original);
    REQUIRE(c.previous_instance() == original);
    c.clear_previous();
    REQUIRE(c.previous_instance() == nullptr);
    REQUIRE(c.previous_program() == nullptr);
}

TEST_CASE("Controller rejects an invalid CPU candidate without changing live error state",
          "[controller][integration]")
{
    const fs::path directory = "assets/characters/chocola/date-a";
    if (!fs::exists(directory / "model.nkm") || !fs::exists(directory / "session.nks.json"))
        SKIP("chocola/date-a assets are unavailable");
    WallpaperController controller;
    ControllerConfig config;
    config.audio = AudioMode::Disabled;
    config.scale = 0;
    auto candidate =
        controller.load_initial(CharacterEntry{"chocola/date-a", directory / "model.nkm",
                                               directory / "session.nks.json", directory},
                                config);
    REQUIRE_FALSE(candidate.ok);
    REQUIRE(candidate.error.find("candidate session failure") != std::string::npos);
    REQUIRE(controller.last_error());
    REQUIRE(controller.current_instance() == nullptr);
}

TEST_CASE("Controller config save load", "[controller]")
{
    WallpaperController c;
    const fs::path path = fs::temp_directory_path() / "nekomote_controller_test_config.json";
    c.config().character = "test_character";
    c.config().background = "test_background";
    c.config().language = Language::Japanese;
    c.config().scale = 1.5f;
    c.config().fps = 60;
    c.config().transform_saved = true;
    c.config().icon_position_saved = true;
    c.config().icon_x = 123.0f;
    c.config().icon_y = 45.0f;
    c.config().sidebar_position_saved = true;
    c.config().sidebar_x = 76.0f;
    c.config().sidebar_y = 89.0f;
    c.config().sidebar_free = true;
    c.set_config_path(path);
    c.save_config(path);
    WallpaperController c2;
    c2.load_config(path);
    REQUIRE(c2.config().character == "test_character");
    REQUIRE(c2.config().background == "test_background");
    REQUIRE(c2.config().language == Language::Japanese);
    REQUIRE(c2.config().scale == 1.5f);
    REQUIRE(c2.config().fps == 60);
    REQUIRE(c2.config().transform_saved);
    REQUIRE(c2.config().icon_position_saved);
    REQUIRE(c2.config().icon_x == 123.0f);
    REQUIRE(c2.config().icon_y == 45.0f);
    REQUIRE(c2.config().sidebar_position_saved);
    REQUIRE(c2.config().sidebar_x == 76.0f);
    REQUIRE(c2.config().sidebar_y == 89.0f);
    REQUIRE(c2.config().sidebar_free);
    fs::remove(path);
}

TEST_CASE("Config language defaults to English and rejects unknown codes", "[controller][config]")
{
    REQUIRE(WallpaperConfig{}.language == Language::English);
    const auto path = fs::temp_directory_path() / "nekomote_config_language_test.json";
    {
        std::ofstream f(path);
        f << R"({"version":1,"character":"a","language":"zh-CN"})";
    }
    auto config = WallpaperConfig::load(path);
    REQUIRE(config.language == Language::ChineseSimplified);
    fs::remove(path);
    {
        std::ofstream f(path);
        f << R"({"version":1,"character":"a","language":"klingon"})";
    }
    REQUIRE_THROWS_AS(WallpaperConfig::load(path), std::invalid_argument);
    fs::remove(path);
    {
        std::ofstream f(path);
        f << R"({"version":1,"character":"a"})";
    }
    config = WallpaperConfig::load(path);
    REQUIRE(config.language == Language::English);
    fs::remove(path);
}

TEST_CASE("Controller persists model transform and shared panel layout", "[controller][config]")
{
    const fs::path path = fs::temp_directory_path() / "nekomote_controller_layout_test.json";
    fs::remove(path);
    WallpaperController controller;
    controller.set_config_path(path);
    using namespace std::chrono_literals;
    const WallpaperController::PointerTime start{1s};
    controller.pointer_press({100, 100}, start);
    controller.pointer_move({112, 121}, start + 1ms);
    controller.pointer_release({112, 121}, start + 2ms);
    controller.zoom_at({20, 30}, 1.0);
    controller.save_ui_layout({500, 24}, {32, 80}, true);

    const auto saved = WallpaperConfig::load(path);
    REQUIRE(saved.transform_saved);
    REQUIRE(saved.origin_x == controller.origin().x);
    REQUIRE(saved.origin_y == controller.origin().y);
    REQUIRE(saved.scale == controller.scale());
    REQUIRE(saved.icon_position_saved);
    REQUIRE(saved.icon_x == 500.0f);
    REQUIRE(saved.icon_y == 24.0f);
    REQUIRE(saved.sidebar_position_saved);
    REQUIRE(saved.sidebar_x == 32.0f);
    REQUIRE(saved.sidebar_y == 80.0f);
    REQUIRE(saved.sidebar_free);
    fs::remove(path);
}

TEST_CASE("Catalog scans static and dynamic backgrounds", "[catalog]")
{
    const auto root = fs::temp_directory_path() / "nekomote_bg_scan_test";
    fs::remove_all(root);
    fs::create_directories(root / "backgrounds" / "dynamic" / "BG_22");
    fs::create_directories(root / "backgrounds" / "dynamic" / "BG_29");
    std::ofstream(root / "backgrounds" / "BG_07A.png").put('x');
    std::ofstream(root / "backgrounds" / "BG_13A.png").put('x');
    std::ofstream(root / "backgrounds" / "dynamic" / "BG_22" / "A.png").put('x');
    std::ofstream(root / "backgrounds" / "dynamic" / "BG_22" / "B.png").put('x');
    std::ofstream(root / "backgrounds" / "dynamic" / "BG_22" / "C.png").put('x');
    {
        std::ofstream f(root / "backgrounds" / "dynamic" / "BG_22" / "config.json");
        f << R"({"phases":[{"image":"A.png","start":"06:00"},)"
          << R"({"image":"B.png","start":"16:00"},)"
          << R"({"image":"C.png","start":"18:00"}],"transition_seconds":1.8})";
        f << "}";
    }
    std::ofstream(root / "backgrounds" / "dynamic" / "BG_29" / "A.png").put('x');

    const auto catalog = AssetCatalog::scan(root);
    REQUIRE(catalog.backgrounds.size() == 3);

    const auto* bg07 = catalog.find_background("BG_07A");
    REQUIRE(bg07 != nullptr);
    REQUIRE(bg07->type == BackgroundType::Static);
    REQUIRE(bg07->path() == root / "backgrounds" / "BG_07A.png");

    const auto* bg22 = catalog.find_background("BG_22");
    REQUIRE(bg22 != nullptr);
    REQUIRE(bg22->type == BackgroundType::Dynamic);
    REQUIRE(bg22->phases.size() == 3);
    REQUIRE(bg22->phases[0].image == root / "backgrounds" / "dynamic" / "BG_22" / "A.png");
    REQUIRE(bg22->phases[1].image == root / "backgrounds" / "dynamic" / "BG_22" / "B.png");
    REQUIRE(bg22->phases[2].image == root / "backgrounds" / "dynamic" / "BG_22" / "C.png");
    REQUIRE(bg22->path_for_phase(0) == bg22->phases[0].image);
    REQUIRE(bg22->path_for_phase(1) == bg22->phases[1].image);
    REQUIRE(bg22->path_for_phase(2) == bg22->phases[2].image);

    REQUIRE(catalog.find_background("BG_29") == nullptr);

    fs::remove_all(root);
}

TEST_CASE("Catalog reads localized background names from JSON", "[catalog]")
{
    const auto root = fs::temp_directory_path() / "nekomote_bg_names_test";
    fs::remove_all(root);
    fs::create_directories(root / "backgrounds" / "dynamic" / "BG_22");
    std::ofstream(root / "backgrounds" / "BG_07A.png").put('x');
    std::ofstream(root / "backgrounds" / "BG_13A.png").put('x');
    std::ofstream(root / "backgrounds" / "dynamic" / "BG_22" / "A.png").put('x');
    std::ofstream(root / "backgrounds" / "dynamic" / "BG_22" / "B.png").put('x');
    std::ofstream(root / "backgrounds" / "dynamic" / "BG_22" / "C.png").put('x');
    {
        std::ofstream f(root / "backgrounds" / "dynamic" / "BG_22" / "config.json");
        f << R"({"phases":[{"image":"A.png","start":"06:00"},)"
          << R"({"image":"B.png","start":"16:00"},)"
          << R"({"image":"C.png","start":"18:00"}]})";
        f << "}";
    }
    {
        std::ofstream f(root / "backgrounds" / "background.json");
        f << R"({"BG_07A.png":{"zh-CN":"海滨餐厅露台","zh-TW":"海濱餐廳露台",)"
          << R"("en":"Seaside","ja":"海辺のレストラン"},)"
          << R"("BG_13A.png":"粉色瓷砖浴室"})";
        f << "}";
    }
    {
        std::ofstream f(root / "backgrounds" / "dynamic" / "background.json");
        f << R"({"BG_22":{"zh-CN":"游乐园入口广场","en":"Park entrance",)"
          << R"("zh-TW":"遊樂園入口廣場","ja":"遊園地入口広場"}})";
        f << "}";
    }

    const auto catalog = AssetCatalog::scan(root);
    const auto* bg07 = catalog.find_background("BG_07A");
    REQUIRE(bg07 != nullptr);
    REQUIRE(bg07->names.resolve(Language::English, bg07->name) == "Seaside");
    REQUIRE(bg07->names.resolve(Language::ChineseSimplified, bg07->name) == "海滨餐厅露台");
    REQUIRE(bg07->names.resolve(Language::ChineseTraditional, bg07->name) == "海濱餐廳露台");
    REQUIRE(bg07->names.resolve(Language::Japanese, bg07->name) == "海辺のレストラン");

    const auto* bg13 = catalog.find_background("BG_13A");
    REQUIRE(bg13 != nullptr);
    // A plain string value applies to every language.
    REQUIRE(bg13->names.resolve(Language::English, bg13->name) == "粉色瓷砖浴室");
    REQUIRE(bg13->names.resolve(Language::Japanese, bg13->name) == "粉色瓷砖浴室");

    const auto* bg22 = catalog.find_background("BG_22");
    REQUIRE(bg22 != nullptr);
    REQUIRE(bg22->names.resolve(Language::English, bg22->name) == "Park entrance");
    REQUIRE(bg22->names.resolve(Language::Japanese, bg22->name) == "遊園地入口広場");

    // A missing dictionary entry falls back to the asset name.
    const auto* missing = catalog.find_background("BG_29");
    REQUIRE(missing == nullptr);

    fs::remove_all(root);
}

TEST_CASE("LocalizedName fallback prefers zh-CN data then asset id", "[catalog]")
{
    LocalizedName name;
    name.zh_cn = "中文";
    REQUIRE(name.resolve(Language::English, "fallback") == "中文");
    REQUIRE(name.resolve(Language::Japanese, "fallback") == "中文");
    REQUIRE(name.resolve(Language::ChineseSimplified, "fallback") == "中文");

    LocalizedName partial;
    partial.ja = "日本語";
    REQUIRE(partial.resolve(Language::English, "fallback") == "日本語");
    REQUIRE(partial.resolve(Language::Japanese, "fallback") == "日本語");

    LocalizedName none;
    REQUIRE(none.resolve(Language::English, "fallback") == "fallback");
    REQUIRE(none.empty());
}

TEST_CASE("Catalog reads localized character names and group titles", "[catalog]")
{
    const auto root = fs::temp_directory_path() / "nekomote_char_names_test";
    fs::remove_all(root);
    fs::create_directories(root / "characters" / "chocola" / "casual-a-with-bell");
    fs::create_directories(root / "characters" / "unknownchar" / "variant-x");
    std::ofstream(root / "characters" / "chocola" / "casual-a-with-bell" / "model.nkm").put('x');
    std::ofstream(root / "characters" / "chocola" / "casual-a-with-bell" / "session.nks.json")
        .put('x');
    std::ofstream(root / "characters" / "unknownchar" / "variant-x" / "model.nkm").put('x');
    std::ofstream(root / "characters" / "unknownchar" / "variant-x" / "session.nks.json").put('x');
    {
        std::ofstream f(root / "characters" / "characters.json");
        f << R"({"chocola/casual-a-with-bell":{"en":"Casual · Pose A · With bell",)"
          << R"("zh-CN":"日常·站姿 A·有铃铛","zh-TW":"日常·站姿 A·有鈴鐺",)"
          << R"("ja":"私服・立ちポーズA・鈴付き"}})";
        f << "}";
    }

    const auto catalog = AssetCatalog::scan(root);
    REQUIRE(catalog.character_groups.size() == 2);

    const auto* chocola = catalog.find_character("chocola/casual-a-with-bell");
    REQUIRE(chocola != nullptr);
    REQUIRE(chocola->names.resolve(Language::English, chocola->name) ==
            "Casual · Pose A · With bell");
    REQUIRE(chocola->names.resolve(Language::ChineseSimplified, chocola->name) ==
            "日常·站姿 A·有铃铛");
    REQUIRE(chocola->names.resolve(Language::ChineseTraditional, chocola->name) ==
            "日常·站姿 A·有鈴鐺");
    REQUIRE(chocola->names.resolve(Language::Japanese, chocola->name) ==
            "私服・立ちポーズA・鈴付き");

    const auto& group = catalog.character_groups.front();
    REQUIRE(group.id == "chocola");
    REQUIRE(group.title.resolve(Language::English, group.id) == "Chocola");
    REQUIRE(group.title.resolve(Language::ChineseSimplified, group.id) == "巧克力");
    REQUIRE(group.title.resolve(Language::Japanese, group.id) == "チョコラ");
    const auto& item = group.items.front();
    REQUIRE(item.names.resolve(Language::English, item.fallback_name) ==
            "Casual · Pose A · With bell");
    REQUIRE(item.fallback_name == "casual-a-with-bell");

    // Unknown group ids and missing dictionary entries fall back to ids.
    const auto& unknown_group = catalog.character_groups.back();
    REQUIRE(unknown_group.id == "unknownchar");
    REQUIRE(unknown_group.title.resolve(Language::English, unknown_group.id) == "unknownchar");
    const auto& unknown_item = unknown_group.items.front();
    REQUIRE(unknown_item.names.resolve(Language::English, unknown_item.fallback_name) ==
            "variant-x");

    fs::remove_all(root);
}

TEST_CASE("First launch without config picks the first background", "[controller][config]")
{
    const auto root = fs::temp_directory_path() / "nekomote_first_launch_bg_test";
    fs::remove_all(root);
    fs::create_directories(root / "backgrounds" / "dynamic" / "BG_22");
    std::ofstream(root / "backgrounds" / "BG_07A.png").put('x');
    std::ofstream(root / "backgrounds" / "BG_13A.png").put('x');
    std::ofstream(root / "backgrounds" / "dynamic" / "BG_22" / "A.png").put('x');
    {
        std::ofstream f(root / "backgrounds" / "dynamic" / "BG_22" / "config.json");
        f << R"({"phases":[{"image":"A.png","start":"06:00"}]})";
        f << "}";
    }
    const fs::path config_path = root / "config.json";

    SECTION("no config file selects the first background deterministically")
    {
        WallpaperController c;
        c.scan_assets(root);
        c.load_config(config_path);
        REQUIRE(c.config().background == "BG_07A");
        // Defaults remain in memory until an actual panel operation saves.
        REQUIRE_FALSE(fs::exists(config_path));
    }

    SECTION("existing config with cleared background keeps the empty state")
    {
        {
            std::ofstream f(config_path);
            f << R"({"version":1,"character":"a","background":""})";
        }
        WallpaperController c;
        c.scan_assets(root);
        c.load_config(config_path);
        REQUIRE(c.config().background.empty());
    }

    SECTION("no-save keeps the pick in memory without writing the file")
    {
        WallpaperController c;
        c.set_no_save(true);
        c.scan_assets(root);
        c.load_config(config_path);
        REQUIRE(c.config().background == "BG_07A");
        REQUIRE_FALSE(fs::exists(config_path));
    }

    fs::remove_all(root);
}

TEST_CASE("Controller background_path returns current phase for dynamic", "[controller]")
{
    const auto root = fs::temp_directory_path() / "nekomote_bg_phase_test";
    fs::remove_all(root);
    fs::create_directories(root / "backgrounds" / "dynamic" / "BG_22");
    std::ofstream(root / "backgrounds" / "dynamic" / "BG_22" / "A.png").put('x');
    std::ofstream(root / "backgrounds" / "dynamic" / "BG_22" / "B.png").put('x');
    std::ofstream(root / "backgrounds" / "dynamic" / "BG_22" / "C.png").put('x');
    {
        std::ofstream f(root / "backgrounds" / "dynamic" / "BG_22" / "config.json");
        f << R"({"phases":[{"image":"A.png","start":"06:00"},)"
          << R"({"image":"B.png","start":"16:00"},)"
          << R"({"image":"C.png","start":"18:00"}]})";
        f << "}";
    }

    WallpaperController c;
    c.scan_assets(root);
    REQUIRE(c.catalog().backgrounds.size() == 1);
    const auto bg_index = 0;
    const auto path = c.background_path(bg_index);
    const auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto* tmp = std::localtime(&t);
    const int minutes = tmp ? tmp->tm_hour * 60 + tmp->tm_min : 0;
    const auto expected_index = c.catalog().backgrounds[0].phase_index_at(minutes);
    REQUIRE(path == c.catalog().backgrounds[0].path_for_phase(expected_index));

    fs::remove_all(root);
}

TEST_CASE("Saved transform restores after first output and reset keeps default fit",
          "[controller][config][layout]")
{
    LayoutFixture fixture;
    const auto path = fixture.path / "config.json";
    WallpaperController first;
    first.set_config_path(path);
    first.config().fit = true;
    fixture.install(first);
    first.set_output_size(800, 600);
    const auto default_origin = first.origin();
    const auto default_scale = first.scale();
    first.pointer_press({0, 0});
    first.pointer_release({80, 50});
    first.zoom_at({0, 0}, -1);
    first.config().fps = 120;
    first.save_ui_layout({300, 120}, {200, 60}, true);
    const auto saved = WallpaperConfig::load(path);
    WallpaperController reopened;
    reopened.load_config(path);
    fixture.install(reopened);
    // Loading without an output must not discard the saved layout.
    reopened.set_output_size(0, 0);
    REQUIRE(reopened.config().origin_x == saved.origin_x);
    reopened.set_output_size(800, 600);
    REQUIRE(reopened.origin().x == Catch::Approx(saved.origin_x));
    REQUIRE(reopened.origin().y == Catch::Approx(saved.origin_y));
    REQUIRE(reopened.scale() == Catch::Approx(saved.scale));
    REQUIRE(reopened.config().fps == 120);
    reopened.reset_character_transform();
    REQUIRE(reopened.origin().x == Catch::Approx(default_origin.x));
    REQUIRE(reopened.origin().y == Catch::Approx(default_origin.y));
    REQUIRE(reopened.scale() == Catch::Approx(default_scale));
}

TEST_CASE("Saved transform outside new output or invalid returns to default fit",
          "[controller][config][layout]")
{
    LayoutFixture fixture;
    WallpaperController defaults;
    defaults.config().fit = true;
    fixture.install(defaults);
    defaults.set_output_size(320, 240);
    WallpaperController reopened;
    reopened.set_no_save(true);
    reopened.config().fit = true;
    reopened.config().transform_saved = true;
    reopened.config().scale = .05f;
    reopened.config().origin_x = 500;
    reopened.config().origin_y = 0;
    SECTION("character outside a smaller new window")
    {
    }
    SECTION("nonfinite origin")
    {
        reopened.config().origin_x = INFINITY;
    }
    SECTION("invalid scale")
    {
        reopened.config().scale = -1;
    }
    SECTION("nonfinite scale")
    {
        reopened.config().scale = std::numeric_limits<float>::quiet_NaN();
    }
    fixture.install(reopened);
    reopened.set_output_size(320, 240);
    REQUIRE(reopened.last_error());
    REQUIRE(reopened.origin().x == Catch::Approx(defaults.origin().x));
    REQUIRE(reopened.origin().y == Catch::Approx(defaults.origin().y));
    REQUIRE(reopened.scale() == Catch::Approx(defaults.scale()));
    REQUIRE(reopened.config().origin_x == reopened.origin().x);
}

TEST_CASE("Resize retains visible transforms and recovers fully offscreen characters",
          "[controller][config][layout]")
{
    LayoutFixture fixture;
    WallpaperController c;
    c.config().fit = true;
    fixture.install(c);
    c.set_output_size(800, 600);
    c.set_scale(.05f);
    c.set_origin({250, 0});
    c.set_output_size(700, 500);
    REQUIRE(c.origin().x == 250.0f);
    REQUIRE(c.scale() == .05f);
    c.set_output_size(100, 100);
    REQUIRE(c.origin().x != 250.0f);
    REQUIRE(c.last_error());
}

TEST_CASE("Malformed panel fields reject the file instead of silently replacing preferences",
          "[controller][config][layout]")
{
    const auto path = fs::temp_directory_path() / "nekomote-layout-corrupt.json";
    std::ofstream(path) << R"({"version":1,"character":"saved", "background":"bg", "fps":-1,
        "transform_saved":true, "scale":"bad", "origin_x":null, "icon_x":null})";
    REQUIRE_THROWS_AS(WallpaperConfig::load(path), std::invalid_argument);
    fs::remove(path);
}

TEST_CASE("Layout persistence honors no-save and clears recovered sidebar coordinates",
          "[controller][config][layout]")
{
    const auto path = fs::temp_directory_path() / "nekomote-layout-no-save.json";
    fs::remove(path);
    WallpaperController c;
    c.set_config_path(path);
    c.set_no_save(true);
    c.zoom_at({0, 0}, 1);
    c.save_ui_layout({100, 80}, {200, 120}, true);
    REQUIRE_FALSE(fs::exists(path));
    c.set_no_save(false);
    c.save_ui_layout({100, 80}, {8, 8}, false);
    const auto saved = WallpaperConfig::load(path);
    REQUIRE_FALSE(saved.sidebar_position_saved);
    REQUIRE_FALSE(saved.sidebar_free);
    fs::remove(path);
}

TEST_CASE("Controller resolves shared FLAC links once within the character directory",
          "[controller][audio][asset]")
{
    LayoutFixture fixture;
    const auto role = fixture.path / "character";
    const auto outfit = role / "outfit";
    const auto shared = role / "audio/common";
    fs::create_directories(outfit);
    fs::create_directories(shared);
    const auto source = fixture.path / "source.wav";
    // Two signed 16-bit PCM samples: -1 and +0.5.
    const unsigned char wave[] = {'R',  'I',  'F', 'F', 40,   0,    0, 0, 'W', 'A',  'V', 'E',
                                  'f',  'm',  't', ' ', 16,   0,    0, 0, 1,   0,    1,   0,
                                  0x44, 0xac, 0,   0,   0x88, 0x58, 1, 0, 2,   0,    16,  0,
                                  'd',  'a',  't', 'a', 4,    0,    0, 0, 0,   0x80, 0,   0x40};
    {
        std::ofstream out(source, std::ios::binary);
        out.write(reinterpret_cast<const char*>(wave), sizeof(wave));
    }
    REQUIRE(nekomote::asset_io::transcode_wave_to_flac(source, shared / "voice.flac").empty());
    nlohmann::json document;
    {
        std::ifstream in(fixture.entry.session_path);
        in >> document;
    }
    const nlohmann::json action{{"main_timeline", "idle"},
                                {"difference_slots", {"", "", "", "", "", ""}},
                                {"variables", nlohmann::json::array()}};
    document["session"]["reactions"] = {{{"label", "voice"},
                                         {"zones", {"bust"}},
                                         {"action", action},
                                         {"recovery", action},
                                         {"audio_path", "audio/voice.flac"},
                                         {"duration_ticks", 60},
                                         {"recovery_ticks", 1},
                                         {"beats", nlohmann::json::array()}}};
    fixture.entry.session_path = outfit / "session.nks.json";
    fixture.entry.directory = outfit;
    bool accepted = true;
    SECTION("relative directory link")
    {
        fs::create_directory_symlink("../audio/common", outfit / "audio");
    }
    SECTION("relative file link")
    {
        fs::create_directories(outfit / "audio");
        fs::create_symlink("../../audio/common/voice.flac", outfit / "audio/voice.flac");
    }
    SECTION("directory link escaping character")
    {
        fs::create_directory_symlink("../../outside", outfit / "audio");
        fs::create_directories(fixture.path / "outside");
        fs::copy_file(shared / "voice.flac", fixture.path / "outside/voice.flac");
        accepted = false;
    }
    SECTION("file link escaping character")
    {
        fs::create_directories(outfit / "audio");
        fs::copy_file(shared / "voice.flac", fixture.path / "outside.flac");
        fs::create_symlink("../../../outside.flac", outfit / "audio/voice.flac");
        accepted = false;
    }
    SECTION("link cycle")
    {
        fs::create_directory_symlink("audio", outfit / "audio");
        accepted = false;
    }
    SECTION("legacy extension")
    {
        document["session"]["reactions"][0]["audio_path"] = "audio/voice.wav";
        fs::create_directory_symlink("../audio/common", outfit / "audio");
        accepted = false;
    }
    std::ofstream(fixture.entry.session_path) << document;
    WallpaperController controller;
    ControllerConfig config;
    config.audio = AudioMode::Device;
    auto candidate = controller.load_initial(fixture.entry, config);
    INFO(candidate.error);
    REQUIRE(candidate.ok == accepted);
    if (!accepted)
    {
        REQUIRE_FALSE(candidate.error.empty());
        return;
    }
    REQUIRE(candidate.audio_clips.size() == 1);
    REQUIRE(candidate.audio_clips[0].samples == std::vector<float>{-1, .5f});
    // Once PCM has loaded, filesystem links no longer participate in playback.
    fs::remove_all(outfit / "audio");
    REQUIRE(candidate.instance->advance(1.0 / 60));
    REQUIRE(candidate.audio_clips[0].samples == std::vector<float>{-1, .5f});
}

TEST_CASE("Initial model output and background setup perform no preference writes",
          "[controller][config]")
{
    LayoutFixture fixture;
    const auto panel = fixture.path / "new-directory/panel.json";
    fs::create_directory(fixture.path / "backgrounds");
    std::ofstream(fixture.path / "backgrounds/first.png").put('x');
    WallpaperController controller;
    controller.scan_assets(fixture.path);
    controller.load_config(panel);
    controller.config().fit = true;
    fixture.install(controller);
    controller.set_output_size(800, 600);
    controller.prepare_frame();
    controller.confirm_background(0, nekomote::TextureId{1}, nekomote::TextureId{1}, 1, 1, 0);
    controller.confirm_background(0, nekomote::TextureId{1}, nekomote::TextureId{2}, 1, 1, 1);
    controller.set_output_size(100, 100);
    REQUIRE_FALSE(fs::exists(panel.parent_path()));
    controller.request_clear_background();
    REQUIRE(controller.poll_background());
    controller.confirm_background_clear();
    REQUIRE(fs::exists(panel));
    REQUIRE(WallpaperConfig::load(panel).background.empty());
    fs::remove(panel);
    controller.request_switch_background(0);
    REQUIRE(controller.poll_background());
    controller.confirm_background(0, nekomote::TextureId{1}, nekomote::TextureId{1}, 1, 1, 0);
    REQUIRE(WallpaperConfig::load(panel).background == "first");
}

TEST_CASE("Queued background changes persist only the latest user choice", "[controller][config]")
{
    LayoutFixture fixture;
    fs::create_directory(fixture.path / "backgrounds");
    std::ofstream(fixture.path / "backgrounds/first.png").put('x');
    WallpaperController controller;
    controller.scan_assets(fixture.path);
    controller.load_config(fixture.path / "panel.json");
    fixture.install(controller);
    SECTION("select then clear")
    {
        controller.request_switch_background(0);
        controller.request_clear_background();
        const auto request = controller.poll_background();
        REQUIRE(request);
        REQUIRE(request->action == BackgroundAction::Clear);
        controller.confirm_background_clear();
        REQUIRE(WallpaperConfig::load(fixture.path / "panel.json").background.empty());
    }
    SECTION("clear then select")
    {
        controller.request_clear_background();
        controller.request_switch_background(0);
        const auto request = controller.poll_background();
        REQUIRE(request);
        REQUIRE(request->action == BackgroundAction::Switch);
        controller.confirm_background(0, nekomote::TextureId{1}, nekomote::TextureId{1}, 1, 1, 0);
        REQUIRE(WallpaperConfig::load(fixture.path / "panel.json").background == "first");
    }
    REQUIRE_FALSE(controller.poll_background());
}
