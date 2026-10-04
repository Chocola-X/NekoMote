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
    REQUIRE(player.transition(reference.scene->find_variable("face_eyebrow"), 40, 20, 1).error ==
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
