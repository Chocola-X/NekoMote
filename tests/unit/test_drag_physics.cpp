#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/runtime/physics.hpp>

#include <algorithm>
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

TEST_CASE("Drag excites initialized particle state and preserves independent rebound",
          "[runtime][physics][pointer]")
{
    const auto asset = drag_rig();
    auto scene = compile_geometry_scene(asset);
    REQUIRE(scene.scene);
    auto compiled = compile_physics(asset, *scene.scene);
    REQUIRE(compiled.program);
    GeometryInstance geometry(*scene.scene);
    REQUIRE(geometry.evaluate());
    PhysicsInstance source(*compiled.program), dragged(*compiled.program);
    REQUIRE(source.step(geometry));
    REQUIRE(dragged.step(geometry, {}, {{1, 0}, {1, 0}}));
    for (std::size_t i = 0; i < source.values().size(); ++i)
        REQUIRE(dragged.values()[i].value == source.values()[i].value);
    for (int tick = 0; tick < 60; ++tick)
    {
        REQUIRE(source.step(geometry));
        REQUIRE(dragged.step(geometry, {}, {{1, 0}, {tick == 0 ? 1.f : 0.f, 0}}));
    }
    REQUIRE(dragged.observe_spring(0)->state.position[0] <
            source.observe_spring(0)->state.position[0]);
    const auto root = output(dragged, *scene.scene, "hair_root");
    REQUIRE(root < -20.f);
    REQUIRE(output(dragged, *scene.scene, "hair_tip") != root);
    REQUIRE(output(dragged, *scene.scene, "hair_tip") < -10.f);
    REQUIRE(std::abs(output(dragged, *scene.scene, "hair_y") -
                     output(source, *scene.scene, "hair_y")) > 1.f);
    REQUIRE(dragged.step(geometry, {}, {{}, {-1, 0}}));
    float opposite_peak = 0;
    for (int tick = 0; tick < 120; ++tick)
    {
        REQUIRE(dragged.step(geometry));
        opposite_peak = std::max(opposite_peak, output(dragged, *scene.scene, "hair_root"));
    }
    REQUIRE(opposite_peak > 20.f);
    for (int tick = 0; tick < 600; ++tick)
        REQUIRE(dragged.step(geometry));
    REQUIRE(std::abs(output(dragged, *scene.scene, "hair_root")) < .1f);
}

TEST_CASE("Zero drag keeps the original spring chain and bend path exact",
          "[runtime][physics][pointer]")
{
    const auto asset = drag_rig();
    auto scene = compile_geometry_scene(asset);
    REQUIRE(scene.scene);
    auto compiled = compile_physics(asset, *scene.scene);
    REQUIRE(compiled.program);
    GeometryInstance geometry(*scene.scene);
    PhysicsInstance implicit(*compiled.program), explicit_zero(*compiled.program);
    for (int tick = 0; tick < 120; ++tick)
    {
        REQUIRE(geometry.set_origin({static_cast<float>(tick), static_cast<float>(tick % 7)}));
        REQUIRE(geometry.evaluate());
        REQUIRE(implicit.step(geometry));
        REQUIRE(explicit_zero.step(geometry, {}, {}));
        for (std::size_t i = 0; i < implicit.values().size(); ++i)
            REQUIRE(implicit.values()[i].value == explicit_zero.values()[i].value);
    }
}

TEST_CASE("Drag bounds added velocity without clipping preexisting source motion",
          "[runtime][physics][pointer]")
{
    for (const float initial : {-200.f, 200.f})
    {
        auto asset = drag_rig();
        asset.spring_controls[0].velocity[0] = initial;
        auto scene = compile_geometry_scene(asset);
        REQUIRE(scene.scene);
        auto compiled = compile_physics(asset, *scene.scene);
        REQUIRE(compiled.program);
        GeometryInstance geometry(*scene.scene);
        REQUIRE(geometry.evaluate());
        PhysicsInstance source(*compiled.program), outward(*compiled.program),
            inward(*compiled.program);
        for (auto* solver : {&source, &outward, &inward})
            REQUIRE(solver->step(geometry));
        const float sign = initial > 0 ? 1.f : -1.f;
        REQUIRE(source.step(geometry));
        REQUIRE(outward.step(geometry, {}, {{-sign, 0}, {-sign, 0}}));
        REQUIRE(inward.step(geometry, {}, {{sign, 0}, {sign, 0}}));
        REQUIRE(outward.observe_spring(0)->state.velocity[0] ==
                source.observe_spring(0)->state.velocity[0]);
        REQUIRE(std::abs(inward.observe_spring(0)->state.velocity[0]) <
                std::abs(source.observe_spring(0)->state.velocity[0]));
    }
}

TEST_CASE("Invalid drag input rejects valid physics instances without advancing state",
          "[runtime][physics][pointer]")
{
    const auto asset = drag_rig();
    auto scene = compile_geometry_scene(asset);
    REQUIRE(scene.scene);
    auto compiled = compile_physics(asset, *scene.scene);
    REQUIRE(compiled.program);
    GeometryInstance geometry(*scene.scene);
    REQUIRE(geometry.evaluate());
    for (const PhysicsDragInput drag :
         {PhysicsDragInput{{1.001f, 0}, {}}, PhysicsDragInput{{.8f, .8f}, {}},
          PhysicsDragInput{{}, {1.5f, 1.5f}},
          PhysicsDragInput{{std::numeric_limits<float>::quiet_NaN(), 0}, {}},
          PhysicsDragInput{{}, {0, std::numeric_limits<float>::infinity()}}})
    {
        PhysicsInstance checked(*compiled.program), reference(*compiled.program);
        REQUIRE(checked.step(geometry));
        REQUIRE(reference.step(geometry));
        const auto status = checked.step(geometry, {}, drag);
        REQUIRE(status.error == PhysicsError::InvalidInput);
        REQUIRE(status.controller == kNoPhysicsController);
        REQUIRE(checked.tick_count() == 1);
        REQUIRE(checked.values().empty());
        REQUIRE(checked.step(geometry));
        REQUIRE(reference.step(geometry));
        for (std::size_t i = 0; i < checked.values().size(); ++i)
            REQUIRE(checked.values()[i].value == reference.values()[i].value);
    }
}
