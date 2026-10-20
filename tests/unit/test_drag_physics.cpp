#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/runtime/physics.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace
{
using namespace nekomote;
using namespace nekomote::model;
using namespace nekomote::runtime;

template <typename T>
Field<T> field(T value)
{
    return {FieldState::Value, std::move(value)};
}

ModelAsset drag_rig()
{
    ModelAsset asset;
    asset.base_group = "rig";
    asset.base_motion = "main";
    asset.author_scale = field(1.f);
    for (const auto* name : {"spring_x", "spring_y", "hair_root", "hair_y", "hair_tip",
                             "parts_root", "parts_y", "parts_tip"})
        asset.variables.push_back({name, {}});
    Part main;
    main.group = "rig";
    main.name = "main";
    main.parameter = {{"spring_x", -10, 20, 1, 0, true},  {"spring_y", 40, -20, 1, 0, true},
                      {"hair_root", -30, 30, 1, 0, true}, {"hair_tip", 2, 8, 1, 0, true},
                      {"parts_root", -6, 14, 1, 0, true}, {"parts_y", -12, 4, 1, 0, true},
                      {"parts_tip", -9, 3, 1, 0, true}};
    Layer anchor;
    anchor.label = "anchor";
    anchor.type = 1;
    anchor.export_self = 1;
    anchor.inherit_mask = 0x20007fc;
    anchor.frame_list = {{0, 2, field(FrameContent{})}};
    anchor.parameter = {{"spring_x", 18, -8, 1, 0, true}, {"spring_x", -1, 1, 1, 0, false}};
    main.layers.push_back(std::move(anchor));
    asset.parts.push_back(std::move(main));
    SpringControl spring;
    spring.binding = {"/spring", "spring", "anchor", "standard", "spring_x", "spring_y", true, {}};
    spring.stiffness = .5f;
    spring.friction = .125f;
    spring.gravity = .3f;
    spring.axis_scale = {1, 2};
    spring.position = {2, 5, 0};
    asset.spring_controls.push_back(spring);
    for (const bool parts : {false, true})
    {
        const std::string prefix = parts ? "parts" : "hair";
        ChainControl chain;
        chain.binding = {"/" + prefix,    prefix,        "anchor", "standard",
                         prefix + "_tip", prefix + "_y", true,     {}};
        chain.variable_middle = prefix + "_root";
        chain.kind = parts ? ChainKind::Parts : ChainKind::Hair;
        chain.stretch_stiffness = .00371093745f;
        chain.gravity = .2f;
        chain.normal_velocity_damping = .5f;
        chain.friction = {.03125f, .03125f};
        chain.length = {64, 48};
        chain.scale_x = {.75f, .25f};
        chain.scale_y = {2, 1};
        chain.rest_position = {{{0, 64, 0}, {0, 112, 0}}};
        chain.position = {{{2, 65, 0}, {3, 113, 0}}};
        asset.chain_controls.push_back(std::move(chain));
    }
    return asset;
}

float output(const PhysicsInstance& physics, const GeometryScene& scene, const char* name)
{
    const auto id = scene.find_variable(name);
    for (const auto value : physics.values())
        if (value.id == id)
            return value.value;
    FAIL("missing bound physics output");
    return 0;
}
} // namespace

TEST_CASE("Drag pose uses bound component axes and intersected authored ranges",
          "[runtime][physics][pointer]")
{
    const auto asset = drag_rig();
    auto scene = compile_geometry_scene(asset);
    REQUIRE(scene.scene);
    auto compiled = compile_physics(asset, *scene.scene);
    REQUIRE(compiled.program);
    REQUIRE(compiled.program->controller_count() == 3);
    GeometryInstance geometry(*scene.scene);
    REQUIRE(geometry.evaluate());
    for (const Vec2 response : {Vec2{}, Vec2{1, -1}, Vec2{-1, 1}, Vec2{.5f, -.5f}})
    {
        PhysicsInstance source(*compiled.program), mixed(*compiled.program);
        for (int tick = 0; tick < 5; ++tick)
        {
            REQUIRE(source.step(geometry));
            REQUIRE(mixed.step(geometry, {}, response));
            struct Expected
            {
                const char* name;
                float minimum, maximum, response;
            };
            for (const auto expected : {Expected{"spring_x", -8, 18, response.x},
                                        Expected{"spring_y", -20, 40, response.y},
                                        Expected{"hair_root", -30, 30, response.x},
                                        Expected{"parts_root", -6, 14, response.x},
                                        Expected{"parts_y", -12, 4, response.y},
                                        Expected{"parts_tip", -9, 3, response.x}})
            {
                const auto original = output(source, *scene.scene, expected.name);
                const auto actual = output(mixed, *scene.scene, expected.name);
                const auto weight = std::abs(expected.response);
                const auto endpoint = expected.response < 0 ? expected.minimum : expected.maximum;
                if (weight == 0)
                    REQUIRE(actual == original);
                else if (weight == 1)
                    REQUIRE(actual == endpoint);
                else
                    REQUIRE_THAT(actual, Catch::Matchers::WithinAbs(.5f * original + .5f * endpoint,
                                                                    .000001f));
            }
            // Missing references and ranges excluding zero never receive a drag pose.
            for (const auto* name : {"hair_y", "hair_tip"})
                REQUIRE(output(mixed, *scene.scene, name) == output(source, *scene.scene, name));
        }
        // A nonzero pose did not perturb either source integrator or bend history.
        REQUIRE(source.step(geometry));
        REQUIRE(mixed.step(geometry));
        REQUIRE(source.values().size() == mixed.values().size());
        for (std::size_t i = 0; i < source.values().size(); ++i)
            REQUIRE(source.values()[i].value == mixed.values()[i].value);
    }
}

TEST_CASE("Disjoint authored ranges leave source drag output untouched",
          "[runtime][physics][pointer]")
{
    auto asset = drag_rig();
    asset.parts[0].layers[0].parameter.push_back({"parts_tip", 4, 8, 1, 0, true});
    auto scene = compile_geometry_scene(asset);
    REQUIRE(scene.scene);
    auto compiled = compile_physics(asset, *scene.scene);
    REQUIRE(compiled.program);
    GeometryInstance geometry(*scene.scene);
    REQUIRE(geometry.evaluate());
    PhysicsInstance source(*compiled.program), mixed(*compiled.program);
    REQUIRE(source.step(geometry));
    REQUIRE(mixed.step(geometry, {}, {1, 1}));
    REQUIRE(output(mixed, *scene.scene, "parts_tip") == output(source, *scene.scene, "parts_tip"));
}

TEST_CASE("Drag blending preserves source output beyond the authored range",
          "[runtime][physics][pointer]")
{
    const auto asset = drag_rig();
    auto scene = compile_geometry_scene(asset);
    REQUIRE(scene.scene);
    auto compiled = compile_physics(asset, *scene.scene);
    REQUIRE(compiled.program);
    GeometryInstance geometry(*scene.scene);
    REQUIRE(geometry.evaluate());
    PhysicsInstance source(*compiled.program), mixed(*compiled.program);
    PhysicsInputs inputs;
    inputs.bust_amplitude = 100;
    REQUIRE(source.step(geometry, inputs));
    REQUIRE(mixed.step(geometry, inputs, {.5f, 0}));
    const auto original = output(source, *scene.scene, "spring_x");
    REQUIRE(original > 18.f);
    REQUIRE_THAT(output(mixed, *scene.scene, "spring_x"),
                 Catch::Matchers::WithinAbs(.5f * original + 9, .000001f));
    REQUIRE(output(mixed, *scene.scene, "spring_y") == output(source, *scene.scene, "spring_y"));
}

TEST_CASE("Invalid drag response rejects valid physics instances without advancing state",
          "[runtime][physics][pointer]")
{
    const auto asset = drag_rig();
    auto scene = compile_geometry_scene(asset);
    REQUIRE(scene.scene);
    auto compiled = compile_physics(asset, *scene.scene);
    REQUIRE(compiled.program);
    GeometryInstance geometry(*scene.scene);
    REQUIRE(geometry.evaluate());
    for (const Vec2 response :
         {Vec2{1.001f, 0}, Vec2{0, -1.001f}, Vec2{std::numeric_limits<float>::quiet_NaN(), 0},
          Vec2{0, std::numeric_limits<float>::infinity()}})
    {
        PhysicsInstance checked(*compiled.program), reference(*compiled.program);
        REQUIRE(checked.step(geometry));
        REQUIRE(reference.step(geometry));
        const auto status = checked.step(geometry, {}, response);
        REQUIRE(status.error == PhysicsError::InvalidInput);
        REQUIRE(status.controller == kNoPhysicsController);
        REQUIRE(checked.tick_count() == 1);
        REQUIRE(checked.values().empty());
        REQUIRE(checked.step(geometry));
        REQUIRE(reference.step(geometry));
        REQUIRE(checked.tick_count() == 2);
        for (std::size_t i = 0; i < checked.values().size(); ++i)
            REQUIRE(checked.values()[i].value == reference.values()[i].value);
    }
}
