#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/asset_io/import_emote.hpp>
#include <nekomote/runtime/animation.hpp>
#include <nekomote/runtime/playback.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

namespace
{
using namespace nekomote;
using Json = nlohmann::json;
Json read(const std::filesystem::path& file)
{
    std::ifstream input(file);
    return Json::parse(input);
}
struct Reference
{
    Json input;
    model::ModelAsset asset;
    std::unique_ptr<const runtime::GeometryScene> scene;
    std::unique_ptr<const runtime::AnimationProgram> program;
    Reference()
    {
        const std::filesystem::path root{NEKOMOTE_FIXTURES_DIR};
        input = read(root / "animation_reference.json");
        auto source = read(root / "minimal.json");
        source["metadata"]["base"] = {{"chara", "body_parts"}, {"motion", "torso"}};
        for (const auto& [name, controls] : input.at("controllers").items())
            source["metadata"][name] = controls;
        source["metadata"]["timelineControl"] = input.at("timelines");
        const auto file =
            std::filesystem::temp_directory_path() /
            ("nekomote-animation-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
        {
            std::ofstream output(file);
            output << source;
        }
        auto imported = asset_io::import_emote({file, root / "minimal.resx.json", root, {}});
        std::filesystem::remove(file);
        if (!imported.ok)
            throw std::runtime_error(imported.report.errors.front());
        asset = std::move(imported.asset);
        auto geometry = runtime::compile_geometry_scene(asset);
        if (!geometry.scene)
            throw std::runtime_error(geometry.errors.front());
        scene = std::move(geometry.scene);
        auto animation = runtime::compile_animation(asset, *scene);
        if (!animation.program)
            throw std::runtime_error(animation.errors.front());
        program = std::move(animation.program);
    }
};
runtime::TimelinePlayMode mode(int flags)
{
    return flags == 0   ? runtime::TimelinePlayMode::Replace
           : flags == 1 ? runtime::TimelinePlayMode::Parallel
                        : runtime::TimelinePlayMode::AdditiveParallel;
}
float value(const runtime::AnimationInstance& player, ParamId id)
{
    return player.values()[id.value - 1].value;
}
} // namespace
TEST_CASE("Native variable transitions and author events match frozen SDK trajectories",
          "[animation]")
{
    Reference reference;
    const float tolerance = reference.input.at("raw_tolerance").get<float>();
    for (const auto& experiment : reference.input.at("experiments"))
    {
        INFO(experiment.at("case").get<std::string>());
        runtime::AnimationInstance player(*reference.program);
        for (const auto& [name, initial] : experiment.at("initial").items())
            REQUIRE(player.transition(reference.scene->find_variable(name), initial.get<float>()));
        for (const auto& [name, request] : experiment.at("transitions").items())
            REQUIRE(player.transition(
                reference.scene->find_variable(name), request.at("value").get<float>(),
                request.at("frames").get<float>(), request.at("easing").get<float>()));
        for (const auto& request : experiment.at("plays"))
            REQUIRE(player.play(
                reference.program->find_timeline(request.at("label").get<std::string>()),
                mode(request.at("flags").get<int>())));
        for (const auto& sample : experiment.at("samples"))
        {
            REQUIRE(player.step());
            INFO(sample.at("step").get<int>());
            for (const auto& [name, expected] : sample.at("values").items())
            {
                INFO(name);
                REQUIRE_THAT(value(player, reference.scene->find_variable(name)),
                             Catch::Matchers::WithinAbs(expected.get<float>(), tolerance));
            }
            std::size_t i = 0;
            for (const auto& request : experiment.at("plays"))
                REQUIRE(player.is_playing(reference.program->find_timeline(
                            request.at("label").get<std::string>())) ==
                        sample.at("playing")[i++].get<bool>());
        }
    }
}
TEST_CASE("Difference weights, fades and replay match public SDK ratio and playing samples",
          "[animation]")
{
    Reference reference;
    const auto fixture =
        read(std::filesystem::path(NEKOMOTE_FIXTURES_DIR) / "timeline_weight_reference.json");
    for (const auto& experiment : fixture.at("cases"))
    {
        INFO(experiment.at("case").get<std::string>());
        runtime::AnimationInstance player(*reference.program);
        const auto requests = [&](const Json& list)
        {
            for (const auto& r : list)
            {
                const auto id = reference.program->find_timeline(r.at("label").get<std::string>());
                const auto kind = r.at("kind").get<std::string>();
                if (kind == "play")
                    REQUIRE(player.play(id, mode(r.at("flags").get<int>())));
                else if (kind == "stop")
                    REQUIRE(player.stop(id));
                else if (kind == "fade_in")
                    REQUIRE(player.fade_in(id, r.at("frames").get<float>(),
                                           r.at("easing").get<float>()));
                else if (kind == "fade_out")
                    REQUIRE(player.fade_out(id, r.at("frames").get<float>(),
                                            r.at("easing").get<float>()));
                else
                    REQUIRE(player.set_timeline_weight(
                        id, r.at("value").get<float>(), r.at("frames").get<float>(),
                        r.at("easing").get<float>(),
                        r.at("completion") == "stop"
                            ? runtime::TimelineWeightCompletion::Stop
                            : runtime::TimelineWeightCompletion::KeepPlaying));
            }
        };
        for (const auto& play : experiment.at("plays"))
            REQUIRE(
                player.play(reference.program->find_timeline(play.at("label").get<std::string>()),
                            mode(play.at("flags").get<int>())));
        requests(experiment.at("requests"));
        for (const auto& sample : experiment.at("samples"))
        {
            INFO(sample.at("step").get<int>());
            if (sample.at("step") != 0)
            {
                for (const auto& event : experiment.at("events"))
                    if (event.at("step") == sample.at("step"))
                        requests(event.value("timeline_requests", Json::array()));
                REQUIRE(player.step());
            }
            for (const auto& [name, weight] : sample.at("weights").items())
            {
                const auto id = reference.program->find_timeline(name);
                REQUIRE_THAT(player.timeline_weight(id),
                             Catch::Matchers::WithinAbs(weight.get<double>(),
                                                        fixture.at("tolerance").get<double>()));
                REQUIRE(player.is_playing(id) == sample.at("playing").at(name).get<bool>());
            }
        }
    }
}

TEST_CASE("Disabling automatic blinking preserves the enabled eye transition graph", "[animation]")
{
    Reference reference;
    const auto fixture =
        read(std::filesystem::path(NEKOMOTE_FIXTURES_DIR) / "eye_blink_reference.json");
    const double tolerance = fixture.at("value_tolerance").get<double>();
    const auto eye = reference.scene->find_variable("face_eye_open");
    REQUIRE(eye.valid());
    for (const auto& experiment : fixture.at("cases"))
    {
        INFO(experiment.at("case").get<std::string>());
        for (auto& control : reference.asset.variable_controls)
            if (control.kind == model::VariableControlKind::Eye)
            {
                control.enabled = experiment.at("controller_enabled").get<bool>();
                control.blink_enabled = experiment.at("blink_enabled").get<bool>();
            }
        auto compiled = runtime::compile_animation(reference.asset, *reference.scene);
        REQUIRE(compiled.program);
        runtime::AnimationInstance player(*compiled.program);
        REQUIRE(player.transition(eye, experiment.at("target").get<float>(),
                                  experiment.at("frames").get<float>(),
                                  experiment.at("easing").get<float>()));
        for (const auto& sample : experiment.at("samples"))
        {
            if (sample.at("step").get<int>() > 0)
                REQUIRE(player.step());
            REQUIRE_THAT(value(player, eye),
                         Catch::Matchers::WithinAbs(sample.at("value").get<double>(), tolerance));
        }
    }
}

TEST_CASE("Unbound and disabled variable controllers apply requests immediately", "[animation]")
{
    Reference reference;
    const auto fixture =
        read(std::filesystem::path(NEKOMOTE_FIXTURES_DIR) / "control_transition_reference.json");
    for (const auto& experiment : fixture.at("cases"))
    {
        const auto name = experiment.at("variable").get<std::string>();
        INFO(name);
        auto asset = reference.asset;
        model::VariableControl control;
        control.variable = name;
        control.kind = model::VariableControlKind::Linear;
        control.enabled = false;
        if (!experiment.at("linear_controller_enabled").is_null())
            asset.variable_controls.push_back(control);
        // A disabled control declares the variable; the unbound case obtains its
        // ID from the asset's variable catalogue, independently of a controller.
        model::VariableDef variable;
        variable.label = name;
        asset.variables.push_back(variable);
        auto geometry = runtime::compile_geometry_scene(asset);
        REQUIRE(geometry.scene);
        auto animation = runtime::compile_animation(asset, *geometry.scene);
        REQUIRE(animation.program);
        runtime::AnimationInstance player(*animation.program);
        const auto id = geometry.scene->find_variable(name);
        const auto& request = experiment.at("request");
        REQUIRE(player.transition(id, request.at("value").get<float>(),
                                  request.at("frames").get<float>(),
                                  request.at("easing").get<float>()));
        REQUIRE(value(player, id) == request.at("value").get<float>());
        for (const auto& sample : experiment.at("samples"))
        {
            if (sample.at("step").get<int>() > 0)
                REQUIRE(player.step());
            REQUIRE_THAT(value(player, id),
                         Catch::Matchers::WithinAbs(sample.at("value").get<double>(),
                                                    fixture.at("value_tolerance").get<double>()));
        }
    }
}

TEST_CASE("Author playback is independent of display partition and pause discards catchup",
          "[animation]")
{
    Reference reference;
    const auto clip = reference.program->find_timeline("待機ループ00");
    auto compiled =
        runtime::compile_playback(reference.asset, runtime::PhysicsMode::DisabledDiagnostic);
    REQUIRE(compiled.program);
    runtime::PlaybackInstance at30(*compiled.program), at60(*compiled.program),
        at144(*compiled.program);
    for (auto* player : {&at30, &at60, &at144})
        REQUIRE(player->play(clip, runtime::TimelinePlayMode::AdditiveParallel));
    for (int i = 0; i < 30; ++i)
        REQUIRE(at30.advance(1.0 / 30));
    for (int i = 0; i < 60; ++i)
        REQUIRE(at60.advance(1.0 / 60));
    for (int i = 0; i < 144; ++i)
        REQUIRE(at144.advance(1.0 / 144));
    REQUIRE(at30.tick_count() == std::uint64_t{60});
    REQUIRE(at60.tick_count() == at30.tick_count());
    REQUIRE(at144.tick_count() == at30.tick_count());
    for (std::size_t i = 0; i < at30.values().size(); ++i)
    {
        REQUIRE(at30.values()[i].value == at60.values()[i].value);
        REQUIRE(at30.values()[i].value == at144.values()[i].value);
    }
    at60.set_playback_state(runtime::PlaybackState::Paused);
    REQUIRE(at60.advance(1000));
    REQUIRE(at60.step());
    REQUIRE(at60.tick_count() == std::uint64_t{60});
    at60.set_playback_state(runtime::PlaybackState::Running);
    REQUIRE(at60.advance(0));
    REQUIRE(at60.tick_count() == std::uint64_t{60});
    REQUIRE(at60.advance(1000));
    REQUIRE(at60.tick_count() == std::uint64_t{75});
    REQUIRE_FALSE(at60.advance(std::numeric_limits<double>::infinity()));
    REQUIRE_FALSE(at60.advance(-1));
    at60.reset();
    REQUIRE(at60.tick_count() == std::uint64_t{0});
    REQUIRE_FALSE(at60.is_playing(clip));
    for (const auto& parameter : at60.values())
        REQUIRE(parameter.value == 0.0f);
}
TEST_CASE("Transition curves consume transient progress before its Float32 save", "[animation]")
{
    Reference reference;
    const auto fixture =
        read(std::filesystem::path(NEKOMOTE_FIXTURES_DIR) / "transform_precision_reference.json")
            .at("transition");
    runtime::AnimationInstance player(*reference.program);
    const auto parameter = reference.scene->find_variable("head_LR");
    REQUIRE(player.transition(parameter, fixture.at("target").get<float>(),
                              fixture.at("frames").get<float>()));
    for (int i = 0; i < fixture.at("step").get<int>(); ++i)
        REQUIRE(player.step());
    REQUIRE(value(player, parameter) == fixture.at("expected").get<float>());
}

TEST_CASE("Animation rejects unknown IDs and selected unsupported controllers", "[animation]")
{
    Reference reference;
    runtime::AnimationInstance player(*reference.program);
    REQUIRE(player.transition({}, 0).error == runtime::AnimationError::InvalidParameter);
    REQUIRE(player.play({}, runtime::TimelinePlayMode::Replace).error ==
            runtime::AnimationError::InvalidTimeline);
    REQUIRE_FALSE(player.transition(reference.scene->find_variable("body_UD"),
                                    std::numeric_limits<float>::quiet_NaN()));
    REQUIRE(player.transition(reference.scene->find_variable("face_eyebrow"), 56, 20, 1).error ==
            runtime::AnimationError::UnsupportedController);
    REQUIRE(player.values()[reference.scene->find_variable("face_eyebrow").value - 1].value ==
            0.0f);
    const auto idle = reference.program->find_timeline("待機ループ00");
    REQUIRE(player.play(idle, runtime::TimelinePlayMode::AdditiveParallel));
    for (int i = 0; i < 70; ++i)
        REQUIRE(player.step());
    REQUIRE(player.stop(idle));
    REQUIRE_FALSE(player.is_playing(idle));
    for (const auto& parameter : player.values())
        REQUIRE(parameter.value == 0.0f);
    const auto main = reference.program->find_timeline("首横振り");
    REQUIRE(player.play(main, runtime::TimelinePlayMode::Parallel));
    for (int i = 0; i < 10; ++i)
        REQUIRE(player.step());
    REQUIRE(player.stop(main));
    for (int i = 0; i < 20; ++i)
        REQUIRE(player.step());
    REQUIRE(value(player, reference.scene->find_variable("head_LR")) == -30.0f);
}

TEST_CASE("Nonlinear expression routes preserve independent curve distance across junctions",
          "[animation]")
{
    Reference reference;
    const auto fixture =
        read(std::filesystem::path(NEKOMOTE_FIXTURES_DIR) / "expression_easing_reference.json");
    const auto parameter =
        reference.scene->find_variable(fixture.at("variable").get<std::string>());
    for (const auto& experiment : fixture.at("cases"))
    {
        INFO(experiment.at("case").get<std::string>());
        runtime::AnimationInstance player(*reference.program);
        for (const auto& [name, initial] : experiment.at("variables").items())
            REQUIRE(player.transition(reference.scene->find_variable(name), initial.get<float>()));
        const auto apply = [&](const Json& transitions)
        {
            for (const auto& [name, transition] : transitions.items())
                REQUIRE(player.transition(
                    reference.scene->find_variable(name), transition.at("value").get<float>(),
                    transition.at("frames").get<float>(), transition.at("easing").get<float>()));
        };
        apply(experiment.at("transitions"));
        for (const auto& sample : experiment.at("samples"))
        {
            INFO(sample.at("step").get<int>());
            for (const auto& event : experiment.at("events"))
                if (event.at("step") == sample.at("step"))
                    apply(event.at("transitions"));
            REQUIRE(player.step());
            REQUIRE_THAT(value(player, parameter),
                         Catch::Matchers::WithinAbs(sample.at("value").get<double>(),
                                                    fixture.at("value_tolerance").get<double>()));
        }
        player.reset();
        REQUIRE(value(player, parameter) == 0.0f);
    }
}

TEST_CASE("Expression transitions report an inverse curve domain failure without NaN output",
          "[animation]")
{
    Reference reference;
    const auto parameter = reference.scene->find_variable("face_eyebrow");
    runtime::AnimationInstance player(*reference.program);
    REQUIRE(player.transition(parameter, 40, 2, 1));
    REQUIRE(player.step());
    REQUIRE(value(player, parameter) == 4.75f);
    const auto failure = player.step();
    REQUIRE(failure.error == runtime::AnimationError::UnsupportedController);
    REQUIRE(failure.parameter == parameter);
    REQUIRE(value(player, parameter) == 4.75f);
    player.reset();
    REQUIRE(player.transition(parameter, 40, 20, 1));
    for (int i = 0; i < 24; ++i)
        REQUIRE(player.step());
    REQUIRE(value(player, parameter) == 40.0f);

    auto compiled =
        runtime::compile_playback(reference.asset, runtime::PhysicsMode::DisabledDiagnostic);
    REQUIRE(compiled.program);
    runtime::PlaybackInstance playback(*compiled.program);
    REQUIRE(playback.transition(parameter, 40, 2, 1));
    REQUIRE(playback.step());
    REQUIRE_FALSE(playback.step());
    REQUIRE_FALSE(playback.prepare_frame());
    playback.reset();
    REQUIRE(playback.transition(parameter, 40, 20, 1));
    REQUIRE(playback.step());
    REQUIRE(playback.tick_count() == std::uint64_t{1});
}

TEST_CASE("Eye expression routes close before switching and reopen from the nearest junction",
          "[animation]")
{
    Reference reference;
    const auto fixture =
        read(std::filesystem::path(NEKOMOTE_FIXTURES_DIR) / "eye_route_reference.json");
    const auto parameter =
        reference.scene->find_variable(fixture.at("variable").get<std::string>());
    for (const auto& experiment : fixture.at("cases"))
    {
        INFO(experiment.at("case").get<std::string>());
        runtime::AnimationInstance player(*reference.program);
        for (const auto& [name, initial] : experiment.at("variables").items())
            REQUIRE(player.transition(reference.scene->find_variable(name), initial.get<float>()));
        for (const auto& [name, transition] : experiment.at("transitions").items())
            REQUIRE(player.transition(
                reference.scene->find_variable(name), transition.at("value").get<float>(),
                transition.at("frames").get<float>(), transition.at("easing").get<float>()));
        for (const auto& sample : experiment.at("samples"))
        {
            INFO(sample.at("step").get<int>());
            REQUIRE(player.step());
            REQUIRE_THAT(value(player, parameter),
                         Catch::Matchers::WithinAbs(sample.at("value").get<double>(),
                                                    fixture.at("value_tolerance").get<double>()));
        }
    }
}

TEST_CASE("Main timelines dispatch nonlinear expression transitions through their control graph",
          "[animation]")
{
    Reference reference;
    model::Timeline clip;
    clip.label = "expression_curve";
    clip.last_time = 21;
    model::Field<float> target, easing;
    target.state = easing.state = model::FieldState::Value;
    target.data = 40;
    easing.data = 1;
    clip.tracks.push_back({"face_eyebrow", {{0, 2, target, easing}, {21, 0, {}, {}}}});
    reference.asset.timelines.push_back(clip);
    auto compiled = runtime::compile_animation(reference.asset, *reference.scene);
    REQUIRE(compiled.program);
    runtime::AnimationInstance player(*compiled.program);
    const auto timeline = compiled.program->find_timeline(clip.label);
    REQUIRE(player.play(timeline, runtime::TimelinePlayMode::Parallel));
    const auto fixture =
        read(std::filesystem::path(NEKOMOTE_FIXTURES_DIR) / "expression_easing_reference.json");
    for (const auto& sample : fixture.at("cases")[0].at("samples"))
    {
        REQUIRE(player.step());
        REQUIRE_THAT(value(player, reference.scene->find_variable("face_eyebrow")),
                     Catch::Matchers::WithinAbs(sample.at("value").get<double>(),
                                                fixture.at("value_tolerance").get<double>()));
    }
    REQUIRE_FALSE(player.is_playing(timeline));
    REQUIRE(player.play(timeline, runtime::TimelinePlayMode::AdditiveParallel).error ==
            runtime::AnimationError::UnsupportedTimeline);
}

TEST_CASE("Automatic Eye blink timing, deterministic seeds and concurrent requests match reference",
          "[animation]")
{
    Reference reference;
    const auto fixture =
        read(std::filesystem::path(NEKOMOTE_FIXTURES_DIR) / "automatic_blink_reference.json");
    const auto parameter =
        reference.scene->find_variable(fixture.at("variable").get<std::string>());
    for (const auto& experiment : fixture.at("cases"))
    {
        INFO(experiment.at("case").get<std::string>());
        auto asset = reference.asset;
        const auto& blink = experiment.at("blink");
        for (auto& control : asset.variable_controls)
            if (control.kind == model::VariableControlKind::Eye)
            {
                control.blink_enabled = true;
                control.begin_frame = blink.at("begin_frame").get<float>();
                control.end_frame = blink.at("end_frame").get<float>();
                control.blink_frames = blink.at("blink_frames").get<float>();
                control.blink_interval_min = blink.at("blink_interval_min").get<float>();
                control.blink_interval_max = blink.at("blink_interval_max").get<float>();
            }
        auto compiled = runtime::compile_animation(asset, *reference.scene);
        REQUIRE(compiled.program);
        runtime::AnimationInstance player(
            *compiled.program,
            runtime::AnimationConfig{experiment.at("random_seed").get<std::uint32_t>()});
        const auto requests = [&](const Json& transitions)
        {
            for (const auto& [name, transition] : transitions.items())
                REQUIRE(player.transition(
                    reference.scene->find_variable(name), transition.at("value").get<float>(),
                    transition.at("frames").get<float>(), transition.at("easing").get<float>()));
        };
        for (const auto& [name, initial] : experiment.at("variables").items())
            REQUIRE(player.transition(reference.scene->find_variable(name), initial.get<float>()));
        requests(experiment.at("transitions"));
        for (const auto& sample : experiment.at("samples"))
        {
            INFO(sample.at("step").get<int>());
            for (const auto& event : experiment.at("events"))
                if (event.at("step") == sample.at("step"))
                {
                    const auto variables = event.value("variables", Json::object());
                    for (const auto& [name, initial] : variables.items())
                        REQUIRE(player.transition(reference.scene->find_variable(name),
                                                  initial.get<float>()));
                    requests(event.value("transitions", Json::object()));
                }
            REQUIRE(player.step());
            REQUIRE_THAT(value(player, parameter),
                         Catch::Matchers::WithinAbs(sample.at("value").get<double>(),
                                                    fixture.at("value_tolerance").get<double>()));
        }
    }
}

TEST_CASE(
    "Blink seeds reset independently and follow the shared clock through pause and display rates",
    "[animation]")
{
    Reference reference;
    auto compiled =
        runtime::compile_playback(reference.asset, runtime::PhysicsMode::DisabledDiagnostic);
    REQUIRE(compiled.program);
    const auto eye = reference.scene->find_variable("face_eye_open");
    runtime::PlaybackInstance at30(*compiled.program, {1}), at60(*compiled.program, {1}),
        at144(*compiled.program, {1}), independent(*compiled.program, {1});
    for (int group = 0; group < 16; ++group)
    {
        for (int i = 0; i < 5; ++i)
            REQUIRE(at30.advance(1.0 / 30));
        for (int i = 0; i < 10; ++i)
            REQUIRE(at60.advance(1.0 / 60));
        for (int i = 0; i < 24; ++i)
            REQUIRE(at144.advance(1.0 / 144));
        REQUIRE(at30.tick_count() == at60.tick_count());
        REQUIRE(at30.tick_count() == at144.tick_count());
        for (std::size_t i = 0; i < at30.values().size(); ++i)
        {
            REQUIRE(at30.values()[i].value == at60.values()[i].value);
            REQUIRE(at30.values()[i].value == at144.values()[i].value);
        }
    }
    REQUIRE(at60.values()[eye.value - 1].value == 9.375f);
    REQUIRE(independent.values()[eye.value - 1].value == 0.0f);
    at60.set_playback_state(runtime::PlaybackState::Paused);
    REQUIRE(at60.advance(20));
    REQUIRE(at60.step());
    REQUIRE(at60.tick_count() == std::uint64_t{160});
    REQUIRE(at60.values()[eye.value - 1].value == 9.375f);
    at60.set_playback_state(runtime::PlaybackState::Running);
    REQUIRE(at60.step());
    REQUIRE(at60.values()[eye.value - 1].value == 10.0f);
    at60.reset();
    REQUIRE(at60.values()[eye.value - 1].value == 0.0f);
    for (int i = 0; i < 160; ++i)
        REQUIRE(at60.step());
    REQUIRE(at60.values()[eye.value - 1].value == 9.375f);

    for (auto& control : reference.asset.variable_controls)
        if (control.kind == model::VariableControlKind::Eye)
            control.blink_frames = 0;
    auto invalid = runtime::compile_animation(reference.asset, *reference.scene);
    REQUIRE_FALSE(invalid.program);
    REQUIRE_FALSE(invalid.errors.empty());
}

TEST_CASE("Selector requests match original deferred choice and independent fade trajectories",
          "[animation]")
{
    Reference reference;
    const auto fixture =
        read(std::filesystem::path(NEKOMOTE_FIXTURES_DIR) / "selector_reference.json");
    auto asset = reference.asset;
    model::VariableControl selector;
    selector.variable = fixture.at("variable").get<std::string>();
    selector.kind = model::VariableControlKind::Selector;
    model::VariableDef choice;
    choice.label = selector.variable;
    asset.variables.push_back(choice);
    for (const auto& option : fixture.at("options"))
    {
        const auto name = option.at("variable").get<std::string>();
        model::VariableDef variable;
        variable.label = name;
        asset.variables.push_back(variable);
        model::VariableControl linear;
        linear.variable = name;
        asset.variable_controls.push_back(linear);
        selector.options.push_back(
            {name, option.at("on_value").get<float>(), option.at("off_value").get<float>()});
    }
    asset.variable_controls.push_back(selector);
    auto geometry = runtime::compile_geometry_scene(asset);
    REQUIRE(geometry.scene);
    auto animation = runtime::compile_animation(asset, *geometry.scene);
    REQUIRE(animation.program);
    for (const auto& experiment : fixture.at("cases"))
    {
        INFO(experiment.at("case").get<std::string>());
        runtime::AnimationInstance player(*animation.program);
        const auto request = [&](const Json& requests)
        {
            for (const auto& [name, transition] : requests.items())
                REQUIRE(player.transition(
                    geometry.scene->find_variable(name), transition.at("value").get<float>(),
                    transition.at("frames").get<float>(), transition.at("easing").get<float>()));
        };
        for (const auto& [name, initial] : experiment.at("initial").items())
            REQUIRE(player.transition(geometry.scene->find_variable(name), initial.get<float>()));
        request(experiment.at("transitions"));
        for (const auto& sample : experiment.at("samples"))
        {
            const int step = sample.at("step").get<int>();
            INFO(step);
            if (step)
            {
                for (const auto& event : experiment.at("events"))
                    if (event.at("step").get<int>() == step)
                    {
                        const auto variables = event.value("variables", Json::object());
                        for (const auto& [name, initial] : variables.items())
                            REQUIRE(player.transition(geometry.scene->find_variable(name),
                                                      initial.get<float>()));
                        request(event.value("transitions", Json::object()));
                    }
                REQUIRE(player.step());
            }
            for (const auto& [name, expected] : sample.at("values").items())
            {
                INFO(name);
                REQUIRE_THAT(value(player, geometry.scene->find_variable(name)),
                             Catch::Matchers::WithinAbs(expected.get<double>(),
                                                        fixture.at("raw_tolerance").get<double>()));
            }
        }
    }
}
