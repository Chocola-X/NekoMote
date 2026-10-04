#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/runtime/animation.hpp>
#include <nekomote/runtime/bend.hpp>
#include <nekomote/runtime/deformation.hpp>
#include <nekomote/runtime/geometry_scene.hpp>
#include <nekomote/runtime/physics.hpp>
#include <nekomote/runtime/spring.hpp>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <new>

namespace
{
thread_local bool measure_allocations = false;
thread_local std::size_t measured_allocations = 0;
} // namespace
// Project-owned, ordinary C++ heap allocations only; warmed math/framework and
// diagnostics are excluded. Geometry storage has no over-aligned element types.
void* operator new(std::size_t size)
{
    if (measure_allocations)
        ++measured_allocations;
    if (void* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size)
{
    return ::operator new(size);
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}

namespace
{
using namespace nekomote;
using namespace nekomote::model;
template <typename T>
Field<T> field(T value)
{
    return {FieldState::Value, std::move(value)};
}
Layer layer(std::string label, int type, FrameContent content)
{
    Layer result;
    result.label = std::move(label);
    result.type = type;
    result.inherit_mask = 0x20007fc;
    result.mesh_division = 20;
    result.frame_list = {{0, 2, field(std::move(content))}};
    return result;
}
FrameContent sprite()
{
    FrameContent content;
    content.src = field(std::string{"atlas"});
    content.icon = field(std::string{"a"});
    return content;
}
ModelAsset rig()
{
    ModelAsset result;
    result.base_group = "rig";
    result.base_motion = "main";
    result.textures.push_back({"atlas", "textures/0.png", "RGBA8", 64, 32});
    result.icons.emplace("atlas/a", IconRect{"atlas", 0, 0, 8, 4, 3, 1});
    FrameContent reference;
    reference.src = field(std::string{"rig"});
    reference.icon = field(std::string{"A"});
    reference.coord = field(std::array<float, 3>{10, 20, 4});
    auto root = layer("reference", 3, reference);
    root.parameterize = field(0);
    root.parameter = {{"pick", 0, 1, 1, 0, true}};
    reference.icon = field(std::string{"B"});
    root.frame_list.push_back({1, 2, field(reference)});
    Part main;
    main.group = "rig";
    main.name = "main";
    main.layers.push_back(std::move(root));
    Part a, b;
    a.group = b.group = "rig";
    a.name = "A";
    b.name = "B";
    auto content = sprite();
    content.coord = field(std::array<float, 3>{2, 3, 2});
    a.layers.push_back(layer("sprite", 0, content));
    content.coord = field(std::array<float, 3>{-5, -6, 6});
    b.layers.push_back(layer("sprite", 0, content));
    result.parts = {std::move(main), std::move(a), std::move(b)};
    return result;
}
ModelAsset spring_rig()
{
    auto result = rig();
    result.author_scale = field(2.0f);
    result.variables = {{"spring_x", {}}, {"spring_y", {}}};
    result.parts[0].layers[0].frame_list[1].content.data.coord =
        field(std::array<float, 3>{12, 20, 4});
    auto marker = layer("anchor", 1, FrameContent{});
    marker.inherit_mask = 0x200020c;
    result.parts[1].layers.push_back(marker);
    result.parts[2].layers.push_back(marker);
    SpringControl spring;
    spring.binding.source_path = "/metadata/bustControl/0";
    spring.binding.label = "spring";
    spring.binding.anchor = "anchor";
    spring.binding.variable_x = "spring_x";
    spring.binding.variable_y = "spring_y";
    spring.stiffness = 0.5f;
    spring.friction = 0.125f;
    spring.axis_scale = {1, 2};
    result.spring_controls.push_back(spring);
    return result;
}
void near(Vec2 actual, Vec2 expected)
{
    REQUIRE_THAT(actual.x, Catch::Matchers::WithinAbs(expected.x, 0.001f));
    REQUIRE_THAT(actual.y, Catch::Matchers::WithinAbs(expected.y, 0.001f));
}
} // namespace

TEST_CASE("Reusable motion branches switch without sharing instance state", "[runtime][scene]")
{
    auto input = rig();
    auto compiled = runtime::compile_geometry_scene(input);
    REQUIRE(compiled.scene);
    input = {}; // the compiled scene owns every binding/key/resource it needs
    const auto& scene = *compiled.scene;
    runtime::GeometryInstance first(scene), second(scene);
    REQUIRE(first.evaluate());
    REQUIRE(second.evaluate());
    REQUIRE(first.primitives().size() == 1);
    const auto id = first.primitives()[0].node;
    near(first.sample_atlas_point(id, {0, 0}), {9, 22});
    REQUIRE(first.primitives()[0].depth == 6.0f);
    const auto pick = scene.find_variable("pick");
    REQUIRE(first.set_parameter(pick, 0.5f));
    REQUIRE(first.evaluate());
    near(first.sample_atlas_point(first.primitives()[0].node, {0, 0}), {9, 22});
    REQUIRE(first.set_parameter(pick, 1));
    REQUIRE(first.evaluate());
    REQUIRE(first.primitives().size() == 1);
    near(first.sample_atlas_point(first.primitives()[0].node, {0, 0}), {2, 13});
    REQUIRE(first.primitives()[0].depth == 10.0f);
    near(second.sample_atlas_point(second.primitives()[0].node, {0, 0}), {9, 22});
    REQUIRE(first.build_frame());
    REQUIRE(first.frame().commands.size() == 1);
    REQUIRE(first.frame().vertices.size() == 4);
    near({first.frame().vertices[0].x, first.frame().vertices[0].y}, {2, 13});
    REQUIRE(first.set_parameter({}, 0).error == runtime::GeometryError::InvalidParameter);
    REQUIRE(first.set_parameter(pick, std::numeric_limits<float>::quiet_NaN()).error ==
            runtime::GeometryError::NonfiniteParameter);
}
TEST_CASE("Same-domain helpers combine before the parent's rotation", "[runtime][scene]")
{
    auto input = rig();
    input.parts.resize(1);
    auto content = sprite();
    auto child = layer("sprite", 0, content);
    FrameContent blank;
    blank.src = field(std::string{"blank"});
    blank.icon = field(std::string{"16:16:8:8"});
    MeshData mesh;
    auto patch = runtime::identity_patch();
    std::array<float, 32> raw{};
    for (std::size_t i = 0; i < patch.size(); ++i)
    {
        raw[i * 2] = patch[i].x;
        raw[i * 2 + 1] = patch[i].y + 0.5f;
    }
    mesh.bp = field(raw);
    blank.mesh = field(mesh);
    auto helper = layer("helper", 0, blank);
    helper.mesh_combine = 1;
    helper.children.push_back(child);
    for (std::size_t i = 0; i < patch.size(); ++i)
    {
        raw[i * 2] = patch[i].x + 0.25f;
        raw[i * 2 + 1] = patch[i].y;
    }
    mesh.bp = field(raw);
    blank.mesh = field(mesh);
    blank.angle = field(90.0f);
    blank.coord = field(std::array<float, 3>{10, 20, 0});
    auto root = layer("base patch", 0, blank);
    root.children.push_back(helper);
    input.parts[0].layers = {root};
    auto compiled = runtime::compile_geometry_scene(input);
    REQUIRE(compiled.scene);
    runtime::GeometryInstance instance(*compiled.scene);
    REQUIRE(instance.evaluate());
    REQUIRE(instance.primitives().size() == 1);
    near(instance.sample_atlas_point(instance.primitives()[0].node, {0, 0}), {3, 21});
    REQUIRE(instance.build_frame());
    REQUIRE(instance.frame().vertices.size() == 15 * 9); // D=20, rectangle aspect=2
    input.parts[0].layers[0].children[0].children[0].mesh_division = 0;
    auto automatic = runtime::compile_geometry_scene(input);
    REQUIRE(automatic.scene);
    runtime::GeometryInstance unsupported(*automatic.scene);
    REQUIRE(unsupported.evaluate());
    REQUIRE(unsupported.build_frame().error == runtime::GeometryError::UnsupportedTessellation);
    REQUIRE(unsupported.frame().commands.empty());
}
TEST_CASE("Cyclic motion graphs fail during compilation with a source path", "[runtime][scene]")
{
    auto input = rig();
    input.parts[1].layers[0].frame_list[0].content.data.src = field(std::string{"rig"});
    input.parts[1].layers[0].frame_list[0].content.data.icon = field(std::string{"main"});
    const auto compiled = runtime::compile_geometry_scene(input);
    REQUIRE_FALSE(compiled.scene);
    REQUIRE(compiled.errors.size() == 1);
    REQUIRE(compiled.errors[0].find("rig/main/reference") != std::string::npos);
}
TEST_CASE("Unverified active ancestors and frame types cannot silently affect sprites",
          "[runtime][scene]")
{
    auto input = rig();
    // Unsupported alternate frames may remain in an immutable asset, but a
    // selected frame must fail; successful previous frame data must expire.
    input.parts[2].layers[0].frame_list[0].type = 1;
    auto compiled = runtime::compile_geometry_scene(input);
    REQUIRE(compiled.scene);
    runtime::GeometryInstance instance(*compiled.scene);
    REQUIRE(instance.evaluate());
    REQUIRE(instance.build_frame());
    REQUIRE_FALSE(instance.frame().commands.empty());
    REQUIRE(instance.set_parameter(compiled.scene->find_variable("pick"), 1));
    REQUIRE(instance.evaluate().error == runtime::GeometryError::UnsupportedTransform);
    REQUIRE(instance.primitives().empty());
    REQUIRE(instance.frame().commands.empty());

    input = rig();
    input.parts[0].layers[0].frame_list[0].content.data.zx = field(2.0f);
    compiled = runtime::compile_geometry_scene(input);
    REQUIRE(compiled.scene);
    runtime::GeometryInstance ancestor(*compiled.scene);
    REQUIRE(ancestor.evaluate().error == runtime::GeometryError::UnsupportedTransform);
    REQUIRE(ancestor.frame().commands.empty());
}

TEST_CASE("Warmed geometry evaluation and frame building allocate no project heap",
          "[runtime][scene]")
{
    auto compiled = runtime::compile_geometry_scene(rig());
    REQUIRE(compiled.scene);
    runtime::GeometryInstance instance(*compiled.scene);
    const auto parameter = compiled.scene->find_variable("pick");
    for (int i = 0; i < 120; ++i)
    {
        REQUIRE(instance.set_parameter(parameter, static_cast<float>(i % 2)));
        REQUIRE(instance.evaluate());
        REQUIRE(instance.build_frame());
    }
    bool success = true;
    measured_allocations = 0;
    measure_allocations = true;
    for (int i = 0; i < 200; ++i)
    {
        success &= static_cast<bool>(instance.set_parameter(parameter, static_cast<float>(i % 2)));
        success &= static_cast<bool>(instance.evaluate());
        success &= static_cast<bool>(instance.build_frame());
    }
    measure_allocations = false;
    REQUIRE(success);
    REQUIRE(measured_allocations == 0);
}

TEST_CASE("Automatic priority-two meshes match original size boundary captures", "[runtime][scene]")
{
    // Captured PSB grids at both sides of the 64 and 128 pixel boundaries.
    for (const auto [size, points] : {std::pair{63, 20}, std::pair{64, 25}, std::pair{127, 56},
                                      std::pair{128, 64}, std::pair{213, 144}})
    {
        auto input = rig();
        input.parts.resize(1);
        input.textures[0].width = input.textures[0].height = 256;
        auto& rect = input.icons.at("atlas/a");
        rect.width = rect.height = static_cast<float>(size);
        FrameContent content;
        content.src = field(std::string{"blank"});
        content.icon = field(std::string{"304:122:152:61"});
        MeshData mesh;
        std::array<float, 32> raw{};
        const auto patch = runtime::identity_patch();
        for (std::size_t i = 0; i < patch.size(); ++i)
        {
            raw[i * 2] = patch[i].x;
            raw[i * 2 + 1] = patch[i].y;
        }
        mesh.bp = field(raw);
        content.mesh = field(mesh);
        auto node = layer("automatic", 0, sprite());
        node.mesh_division = 0;
        node.obj_tri_priority = 2;
        auto parent = layer("parent domain", 0, content);
        parent.children.push_back(node);
        input.parts[0].layers = {parent};
        auto compiled = runtime::compile_geometry_scene(input);
        REQUIRE(compiled.scene);
        runtime::GeometryInstance instance(*compiled.scene);
        REQUIRE(instance.evaluate());
        REQUIRE(instance.build_frame());
        REQUIRE(instance.frame().vertices.size() == static_cast<std::size_t>(points));
    }
}

TEST_CASE(
    "Mask labels bind within reusable motion instances and required failures publish no frame",
    "[runtime][scene]")
{
    auto input = rig();
    for (std::size_t i = 1; i < input.parts.size(); ++i)
    {
        auto& part = input.parts[i];
        part.layers[0].label = "white";
        FrameContent blank;
        blank.src = field(std::string{"blank"});
        blank.icon = field(std::string{"32:32:16:16"});
        auto mask = layer("mask", 12, blank);
        mask.inherit_mask = 0x24007fc;
        mask.stencil_type = 5;
        mask.stencil_composite_mask_layers = {"white"};
        auto pupil = sprite();
        pupil.coord = field(std::array<float, 3>{0, 0, 7});
        mask.children.push_back(layer("pupil", 0, pupil));
        part.layers.push_back(mask);
    }
    auto compiled = runtime::compile_geometry_scene(input);
    REQUIRE(compiled.scene);
    runtime::GeometryInstance instance(*compiled.scene);
    REQUIRE(instance.evaluate());
    REQUIRE(instance.build_frame());
    auto frame = instance.frame();
    REQUIRE(frame.commands.size() == 2);
    REQUIRE(frame.commands[0].mask == MaskId{});
    REQUIRE(frame.commands[1].mask == MaskId{1});
    REQUIRE(frame.mask_commands.size() == 1);
    REQUIRE(frame.mask_commands[0].first_index == frame.commands[0].first_index);
    REQUIRE(instance.set_parameter(compiled.scene->find_variable("pick"), 1));
    REQUIRE(instance.frame().commands.empty());
    REQUIRE(instance.evaluate());
    REQUIRE(instance.build_frame());
    frame = instance.frame();
    REQUIRE(frame.commands[1].mask == MaskId{2});
    REQUIRE(frame.masks[0].command_count == 0);
    REQUIRE(frame.masks[1].command_count == 1);
    REQUIRE(frame.mask_commands[0].first_index == frame.commands[0].first_index);
    input.parts[1].layers[1].stencil_composite_mask_layers = {"missing source"};
    auto invalid = runtime::compile_geometry_scene(input);
    REQUIRE(invalid.scene);
    REQUIRE_FALSE(invalid.scene->diagnostics().empty());
    runtime::GeometryInstance rejected(*invalid.scene);
    REQUIRE(rejected.evaluate());
    REQUIRE(rejected.build_frame().error == runtime::GeometryError::UnsupportedMask);
    REQUIRE(rejected.frame().vertices.empty());
    REQUIRE(rejected.frame().commands.empty());
}

TEST_CASE("Atlas packing changes UV without changing deformed model geometry", "[runtime][scene]")
{
    auto input = rig();
    input.parts.resize(1);
    input.textures[0].width = input.textures[0].height = 4096;
    auto& rect = input.icons.at("atlas/a");
    rect.width = 71;
    rect.height = 60;
    auto content = sprite();
    MeshData mesh;
    std::array<float, 32> raw{};
    const auto patch = runtime::identity_patch();
    for (std::size_t i = 0; i < patch.size(); ++i)
    {
        raw[i * 2] = patch[i].x;
        raw[i * 2 + 1] = patch[i].y;
    }
    mesh.bp = field(raw);
    content.mesh = field(mesh);
    input.parts[0].layers = {layer("sprite", 0, content)};
    auto first = runtime::compile_geometry_scene(input);
    REQUIRE(first.scene);
    rect.left = 3072;
    rect.top = 2048;
    auto second = runtime::compile_geometry_scene(input);
    REQUIRE(second.scene);
    runtime::GeometryInstance a(*first.scene), b(*second.scene);
    REQUIRE(a.evaluate());
    REQUIRE(b.evaluate());
    REQUIRE(a.build_frame());
    REQUIRE(b.build_frame());
    const auto va = a.frame().vertices, vb = b.frame().vertices;
    REQUIRE(va.size() == vb.size());
    for (std::size_t i = 0; i < va.size(); ++i)
    {
        REQUIRE(va[i].x == vb[i].x);
        REQUIRE(va[i].y == vb[i].y);
        REQUIRE(vb[i].u > va[i].u);
        REQUIRE(vb[i].v > va[i].v);
    }
}

TEST_CASE("Warmed animation, expression routing and geometry allocate no project heap",
          "[runtime][animation][allocation]")
{
    auto asset = rig();
    Timeline clip;
    clip.label = "cycle";
    clip.loop_begin = 0;
    clip.loop_end = 17;
    clip.tracks.push_back(
        {"pick",
         {{0, 2, field(1.0f), field(0.0f)}, {9, 2, field(0.0f), field(0.0f)}, {17, 0, {}, {}}}});
    asset.timelines.push_back(std::move(clip));
    VariableControl expression;
    expression.kind = VariableControlKind::Expression;
    expression.variable = "expression";
    expression.edges = {{{0, 1}}, {{2, 3}}};
    expression.junctions = {{1, 2}};
    asset.variable_controls.push_back(std::move(expression));
    auto compiled = runtime::compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    auto animation = runtime::compile_animation(asset, *compiled.scene);
    REQUIRE(animation.program);
    runtime::AnimationInstance player(*animation.program);
    runtime::GeometryInstance geometry(*compiled.scene);
    const auto cycle = animation.program->find_timeline("cycle");
    const auto parameter = compiled.scene->find_variable("expression");
    REQUIRE(player.play(cycle, runtime::TimelinePlayMode::Parallel));
    const auto update = [&](int frame)
    {
        if (frame % 20 == 0 && !player.transition(parameter, frame % 40 == 0 ? 3.0f : 0.0f, 10))
            return false;
        if (!player.step())
            return false;
        for (const auto& value : player.values())
            if (!geometry.set_parameter(value.id, value.value))
                return false;
        return static_cast<bool>(geometry.evaluate()) && static_cast<bool>(geometry.build_frame());
    };
    for (int i = 0; i < 120; ++i)
        REQUIRE(update(i));
    bool succeeded = true;
    measured_allocations = 0;
    measure_allocations = true;
    for (int i = 120; i < 620; ++i)
        succeeded = update(i) && succeeded;
    measure_allocations = false;
    REQUIRE(succeeded);
    REQUIRE(measured_allocations == 0);
}
TEST_CASE("Spring advancement has independent state and no project heap", "[runtime][allocation]")
{
    using namespace nekomote::runtime;
    SpringState first, second;
    const SpringCoefficients coefficients{0.03125f, 0.125f, 0.3f};
    const SpringOutput output{{1, 2}, 9.6f, 1};
    for (int i = 0; i < 120; ++i)
        advance_spring(first, coefficients, {}, {0.1f, 0});
    const auto snapshot = first;
    Vec2 observed;
    measured_allocations = 0;
    measure_allocations = true;
    for (int i = 0; i < 1000; ++i)
    {
        advance_spring(first, coefficients, {}, {0.1f, 0});
        observed = spring_output(first, output);
    }
    measure_allocations = false;
    REQUIRE(measured_allocations == 0);
    REQUIRE(std::isfinite(observed.x));
    REQUIRE(std::isfinite(observed.y));
    REQUIRE(second.position == std::array<double, 2>{});
    REQUIRE(second.velocity == std::array<double, 2>{});
    second = snapshot;
    for (int i = 0; i < 1000; ++i)
        advance_spring(second, coefficients, {}, {0.1f, 0});
    REQUIRE(first.position == second.position);
    REQUIRE(first.velocity == second.velocity);
}
TEST_CASE("Exported locators report inactive branches and reject unknown transforms", "[runtime]")
{
    auto asset = rig();
    auto marker = layer("anchor", 1, FrameContent{});
    marker.inherit_mask = 0x200020c;
    asset.parts[1].layers.push_back(marker);
    asset.parts[2].layers.push_back(marker);
    auto compiled = runtime::compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    const auto& scene = *compiled.scene;
    REQUIRE(scene.locators().size() == 2);
    runtime::GeometryInstance instance(scene);
    const auto first = scene.locators()[0].node, second = scene.locators()[1].node;
    REQUIRE(instance.sample_locator({}).status.error == runtime::GeometryError::InvalidLocator);
    REQUIRE(instance.sample_locator(first).status.error ==
            runtime::GeometryError::UnsupportedTransform);
    REQUIRE(instance.evaluate());
    REQUIRE(instance.sample_locator(first).status);
    REQUIRE(instance.sample_locator(first).state == runtime::LocatorState::Active);
    near(instance.sample_locator(first).position, {10, 20});
    REQUIRE(instance.sample_locator(second).state == runtime::LocatorState::Inactive);
    REQUIRE(instance.set_parameter(scene.find_variable("pick"), 1));
    REQUIRE(instance.sample_locator(first).status.error ==
            runtime::GeometryError::UnsupportedTransform);
    REQUIRE(instance.evaluate());
    REQUIRE(instance.sample_locator(first).state == runtime::LocatorState::Inactive);
    REQUIRE(instance.sample_locator(second).status);
    near(instance.sample_locator(second).position, {10, 20});

    const auto pick = scene.find_variable("pick");
    bool success = true;
    measured_allocations = 0;
    measure_allocations = true;
    for (int i = 0; i < 200; ++i)
    {
        success &= static_cast<bool>(instance.set_parameter(pick, static_cast<float>(i % 2)));
        success &= static_cast<bool>(instance.evaluate());
        success &= static_cast<bool>(instance.sample_locator(first).status);
        success &= static_cast<bool>(instance.sample_locator(second).status);
    }
    measure_allocations = false;
    REQUIRE(success);
    REQUIRE(measured_allocations == 0);

    // A zero local origin is the verified partial-inheritance scope. Do not
    // generalize it to positioned markers or other unknown inheritance bits.
    asset.parts[1].layers.back().frame_list[0].content.data.coord =
        field(std::array<float, 3>{1, 0, 0});
    compiled = runtime::compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    runtime::GeometryInstance positioned(*compiled.scene);
    REQUIRE(positioned.evaluate());
    REQUIRE(positioned.sample_locator(compiled.scene->locators()[0].node).status.error ==
            runtime::GeometryError::UnsupportedTransform);

    auto unknown_parent = layer("unknown", 2, FrameContent{});
    unknown_parent.inherit_mask = 0;
    unknown_parent.children.push_back(marker);
    asset.parts[1].layers.pop_back();
    asset.parts[1].layers.push_back(std::move(unknown_parent));
    compiled = runtime::compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    runtime::GeometryInstance unknown(*compiled.scene);
    REQUIRE(unknown.evaluate());
    REQUIRE(unknown.sample_locator(compiled.scene->locators()[0].node).status.error ==
            runtime::GeometryError::UnsupportedTransform);
}

TEST_CASE("Warmed bending output retains independent resettable state without heap", "[runtime]")
{
    using namespace nekomote::runtime;
    BendState first{0.355883569f, 0}, second;
    const BendCoefficients coefficients{0.3926991f, 3};
    const ChainVariables unbent{1, 35, -1};
    for (int i = 0; i < 120; ++i)
        advance_bend(first, coefficients, unbent);
    const auto snapshot = first;
    ChainVariables observed;
    measured_allocations = 0;
    measure_allocations = true;
    for (int i = 0; i < 1000; ++i)
        observed = advance_bend(first, coefficients, unbent);
    measure_allocations = false;
    REQUIRE(measured_allocations == 0);
    REQUIRE(std::isfinite(observed.root_x));
    REQUIRE(std::isfinite(observed.vertical));
    REQUIRE(std::isfinite(observed.tip_x));
    REQUIRE(second.phase == 0.0f);
    REQUIRE(second.gain == 0.0f);
    second = snapshot;
    for (int i = 0; i < 1000; ++i)
        advance_bend(second, coefficients, unbent);
    REQUIRE(first.phase == second.phase);
    REQUIRE(first.gain == second.gain);
}

TEST_CASE("Bound physical instances normalize anchors and preserve state on failed steps",
          "[runtime]")
{
    using namespace nekomote::runtime;
    auto input = spring_rig();
    auto geometry = compile_geometry_scene(input);
    REQUIRE(geometry.scene);
    auto compiled = compile_physics(input, *geometry.scene);
    REQUIRE(compiled.program);
    input = {}; // both programs own normalized data; scene owns borrowed IDs
    const auto& program = *compiled.program;
    REQUIRE(program.controller_count() == 1);
    REQUIRE(program.controller_source(0) == "/metadata/bustControl/0");
    REQUIRE(program.controller_source(1).empty());
    GeometryInstance anchors(*geometry.scene);
    PhysicsInstance physics(program), independent(program);
    REQUIRE(physics.values().empty());
    REQUIRE(physics.step(anchors).error == PhysicsError::InvalidAnchor);
    REQUIRE(physics.tick_count() == 0);
    REQUIRE(anchors.evaluate());
    // Initial anchor initialization skips even a large external displacement.
    REQUIRE(physics.step(anchors, {1, {100, 200}}));
    REQUIRE(physics.values().size() == 2);
    REQUIRE(physics.values()[0].value == 0.0f);
    REQUIRE(physics.values()[1].value == 0.0f);
    const auto pick = geometry.scene->find_variable("pick");
    REQUIRE(anchors.set_parameter(pick, 1));
    REQUIRE(physics.step(anchors).error == PhysicsError::InvalidAnchor);
    REQUIRE(physics.values().empty());
    REQUIRE(physics.tick_count() == 1);
    REQUIRE(anchors.evaluate());
    REQUIRE(physics.step(anchors));
    // 2 model pixels / author_scale 2 = 1 unit; isolated SDK half-spring step 2.
    REQUIRE_THAT(physics.values()[0].value, Catch::Matchers::WithinAbs(-0.6467359066009521, 1e-5));
    REQUIRE(physics.step(anchors));
    REQUIRE_THAT(physics.values()[0].value, Catch::Matchers::WithinAbs(0.07636695355176926, 1e-5));
    REQUIRE(physics.tick_count() == 3);
    REQUIRE(physics.step(anchors, {std::numeric_limits<float>::quiet_NaN(), {}}).error ==
            PhysicsError::InvalidInput);
    REQUIRE(physics.values().empty());
    REQUIRE(physics.tick_count() == 3);
    REQUIRE(physics.step(anchors));
    REQUIRE_THAT(physics.values()[0].value, Catch::Matchers::WithinAbs(0.6756348609924316, 1e-5));
    REQUIRE(independent.tick_count() == 0);
    REQUIRE(independent.values().empty());
    REQUIRE(independent.step(anchors));
    REQUIRE(independent.values()[0].value == 0.0f);
    // SDK external displacements already use author units, independent of scale.
    REQUIRE(independent.step(anchors, {1, {1, 0}}));
    REQUIRE_THAT(independent.values()[0].value,
                 Catch::Matchers::WithinAbs(-0.6467359066009521, 1e-5));
    physics.reset();
    REQUIRE(physics.tick_count() == 0);
    REQUIRE(physics.values().empty());
    REQUIRE(physics.step(anchors));
    REQUIRE(physics.values()[0].value == 0.0f);

    auto foreign = compile_geometry_scene(spring_rig());
    REQUIRE(foreign.scene);
    GeometryInstance foreign_geometry(*foreign.scene);
    REQUIRE(foreign_geometry.evaluate());
    REQUIRE(physics.step(foreign_geometry).error == PhysicsError::ForeignScene);
    REQUIRE(physics.tick_count() == 1);
    REQUIRE(physics.values().empty());
}

TEST_CASE("Physical compilation rejects unsupported or unresolved selected semantics", "[runtime]")
{
    using namespace nekomote::runtime;
    auto input = spring_rig();
    auto geometry = compile_geometry_scene(input);
    REQUIRE(geometry.scene);
    auto& spring = input.spring_controls[0];
    SECTION("enabled chain")
    {
        ChainControl chain;
        chain.binding.source_path = "/metadata/hairControl/0";
        input.chain_controls.push_back(chain);
    }
    SECTION("unknown physical field")
    {
        spring.binding.unsupported_fields = {"/metadata/bustControl/0/newForce"};
    }
    SECTION("unknown saved anchor")
    {
        spring.previous_anchor[0] = 1;
    }
    SECTION("unverified z")
    {
        spring.position[2] = 1;
    }
    SECTION("nonfinite snapshot")
    {
        spring.velocity[0] = std::numeric_limits<float>::infinity();
    }
    SECTION("missing anchor")
    {
        spring.binding.anchor = "missing";
    }
    SECTION("missing output")
    {
        spring.binding.variable_x = "missing";
    }
    SECTION("shared output")
    {
        auto second = spring;
        second.binding.source_path = "/metadata/bustControl/1";
        input.spring_controls.push_back(second);
    }
    SECTION("invalid author scale")
    {
        input.author_scale.data = 0;
    }
    auto compiled = compile_physics(input, *geometry.scene);
    REQUIRE_FALSE(compiled.program);
    REQUIRE_FALSE(compiled.errors.empty());
    REQUIRE(compiled.errors[0].find("/metadata/") != std::string::npos);
}

TEST_CASE("Physical anchor failure cannot partially advance earlier controllers", "[runtime]")
{
    using namespace nekomote::runtime;
    auto input = spring_rig();
    input.variables.push_back({"second_x", {}});
    input.variables.push_back({"second_y", {}});
    auto second = input.spring_controls[0];
    second.binding.source_path = "/metadata/bustControl/1";
    second.binding.variable_x = "second_x";
    second.binding.variable_y = "second_y";
    second.binding.anchor = "second_anchor";
    input.spring_controls.push_back(second);
    auto marker = layer("second_anchor", 1, FrameContent{});
    input.parts[1].layers.push_back(marker);
    marker.inherit_mask = 0; // only branch B has the unverified transform
    input.parts[2].layers.push_back(marker);
    auto geometry = compile_geometry_scene(input);
    REQUIRE(geometry.scene);
    auto compiled = compile_physics(input, *geometry.scene);
    REQUIRE(compiled.program);
    GeometryInstance anchors(*geometry.scene);
    PhysicsInstance physics(*compiled.program);
    REQUIRE(anchors.evaluate());
    REQUIRE(physics.step(anchors));
    const auto pick = geometry.scene->find_variable("pick");
    REQUIRE(anchors.set_parameter(pick, 1));
    REQUIRE(anchors.evaluate());
    auto status = physics.step(anchors);
    REQUIRE(status.error == PhysicsError::InvalidAnchor);
    REQUIRE(status.controller == 1);
    REQUIRE(status.geometry.error == GeometryError::UnsupportedTransform);
    REQUIRE(physics.tick_count() == 1);
    REQUIRE(physics.values().empty());
    REQUIRE(anchors.set_parameter(pick, 0));
    REQUIRE(anchors.evaluate());
    REQUIRE(physics.step(anchors));
    REQUIRE(physics.tick_count() == 2);
    REQUIRE(physics.values()[0].value == 0.0f);
    REQUIRE(physics.values()[1].value == 0.0f);
}

TEST_CASE("Physical anchor selection requires exactly one active branch", "[runtime]")
{
    using namespace nekomote::runtime;
    auto input = spring_rig();
    PhysicsError expected = PhysicsError::None;
    SECTION("missing active anchor")
    {
        input.parts[2].layers.pop_back();
        expected = PhysicsError::InactiveAnchor;
    }
    SECTION("ambiguous active anchors")
    {
        input.parts[2].layers.push_back(input.parts[2].layers.back());
        expected = PhysicsError::AmbiguousAnchor;
    }
    auto geometry = compile_geometry_scene(input);
    REQUIRE(geometry.scene);
    auto compiled = compile_physics(input, *geometry.scene);
    REQUIRE(compiled.program);
    GeometryInstance anchors(*geometry.scene);
    PhysicsInstance physics(*compiled.program);
    REQUIRE(anchors.evaluate());
    REQUIRE(physics.step(anchors));
    REQUIRE(anchors.set_parameter(geometry.scene->find_variable("pick"), 1));
    REQUIRE(anchors.evaluate());
    auto status = physics.step(anchors);
    REQUIRE(status.error == expected);
    REQUIRE(status.controller == 0);
    REQUIRE(physics.tick_count() == 1);
    REQUIRE(physics.values().empty());
}

TEST_CASE("Nonfinite physical output leaves the last valid state intact", "[runtime]")
{
    using namespace nekomote::runtime;
    auto input = spring_rig();
    auto& spring = input.spring_controls[0];
    spring.stiffness = std::numeric_limits<float>::max();
    spring.position[0] = 1;
    spring.axis_scale[0] = 0;
    auto geometry = compile_geometry_scene(input);
    REQUIRE(geometry.scene);
    auto compiled = compile_physics(input, *geometry.scene);
    REQUIRE(compiled.program);
    GeometryInstance anchors(*geometry.scene);
    PhysicsInstance physics(*compiled.program);
    REQUIRE(anchors.evaluate());
    const PhysicsInputs inputs{std::numeric_limits<float>::max(), {}};
    PhysicsStatus status;
    std::uint64_t last_tick = 0;
    for (int i = 0; i < 16; ++i)
    {
        last_tick = physics.tick_count();
        status = physics.step(anchors, inputs);
        if (!status)
            break;
    }
    REQUIRE(status.error == PhysicsError::NonfiniteState);
    REQUIRE(status.controller == 0);
    REQUIRE(physics.tick_count() == last_tick);
    REQUIRE(physics.values().empty());
    // The failed overflow was in output mapping, not in the integrator. A new
    // amplitude can retry that step from the last successfully committed state.
    REQUIRE(physics.step(anchors, {0, {}}));
    REQUIRE(physics.tick_count() == last_tick + 1);
    REQUIRE(physics.values()[0].value == 0.0f);
}

TEST_CASE("Warmed physical binding and anchor sampling allocate no project heap", "[runtime]")
{
    using namespace nekomote::runtime;
    auto input = spring_rig();
    auto geometry = compile_geometry_scene(input);
    REQUIRE(geometry.scene);
    auto compiled = compile_physics(input, *geometry.scene);
    REQUIRE(compiled.program);
    GeometryInstance anchors(*geometry.scene);
    PhysicsInstance physics(*compiled.program);
    const auto pick = geometry.scene->find_variable("pick");
    for (int i = 0; i < 120; ++i)
    {
        REQUIRE(anchors.set_parameter(pick, static_cast<float>(i % 2)));
        REQUIRE(anchors.evaluate());
        REQUIRE(physics.step(anchors));
    }
    bool success = true;
    measured_allocations = 0;
    measure_allocations = true;
    for (int i = 0; i < 200; ++i)
    {
        success &= static_cast<bool>(anchors.set_parameter(pick, static_cast<float>(i % 2)));
        success &= static_cast<bool>(anchors.evaluate());
        success &= static_cast<bool>(physics.step(anchors));
        for (const auto value : physics.values())
            success &= static_cast<bool>(anchors.set_parameter(value.id, value.value));
        success &= static_cast<bool>(anchors.evaluate());
        success &= static_cast<bool>(anchors.build_frame());
    }
    measure_allocations = false;
    REQUIRE(success);
    REQUIRE(measured_allocations == 0);
}
