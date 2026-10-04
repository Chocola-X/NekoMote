#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/runtime/animation.hpp>
#include <nekomote/runtime/bend.hpp>
#include <nekomote/runtime/chain.hpp>
#include <nekomote/runtime/deformation.hpp>
#include <nekomote/runtime/geometry_scene.hpp>
#include <nekomote/runtime/physics.hpp>
#include <nekomote/runtime/playback.hpp>
#include <nekomote/runtime/spring.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <new>
#include <string>

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
ModelAsset chain_rig()
{
    auto result = spring_rig();
    result.spring_controls.clear();
    result.author_scale = field(1.0f);
    ChainControl chain;
    chain.binding = {
        "/metadata/hairControl/0", "hair", "anchor", "standard", "tip", "vertical", true, {}};
    chain.variable_middle = "root";
    chain.stretch_stiffness = 0.00371093745f;
    chain.gravity = 0.2f;
    chain.normal_velocity_damping = 0.5f;
    chain.friction = {0.03125f, 0.03125f};
    chain.length = {64, 48};
    chain.scale_x = {0.75f, 0.25f};
    chain.scale_y = {2, 1};
    chain.rest_position = {{{0, 64, 0}, {0, 112, 0}}};
    chain.position = {{{0, 117.894691f, 0}, {0, 166.270462f, 0}}};
    chain.velocity = {{{0, 1.36992894e-5f, 0}, {0, 0.375757635f, 0}}};
    chain.vertical_offset = -53.89473f;
    result.chain_controls.push_back(chain);
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
    helper.mesh_sync_child_mask = 8;
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
    root.mesh_sync_child_mask = 8;
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

TEST_CASE("Two-particle state is independent, resettable and allocation free", "[runtime]")
{
    using namespace nekomote::runtime;
    const ChainCoefficients coefficients{
        0.00371093745f, 0.2f, {0.03125f, 0.03125f}, {64, 48}, 0.5f};
    const ChainRest rest{{{{0, 64}, {0, 112}}}};
    const ChainState snapshot{{{{0, 117.894691f}, {0, 166.270462f}}},
                              {{{0, 1.36992894e-5f}, {0, 0.375757635f}}}};
    auto state = snapshot, independent = snapshot;
    const auto initial = advance_chain(state, coefficients, rest, {100, 200}, {100, 200});
    REQUIRE(state.initialized);
    REQUIRE(state.anchor_offset == nekomote::Vec2{});
    REQUIRE(state.velocity == snapshot.velocity);
    REQUIRE(state.position[0] == snapshot.position[0]);
    REQUIRE(state.position[1].y == 165.89468383789062f);
    REQUIRE_FALSE(independent.initialized);
    REQUIRE(independent.position == snapshot.position);
    // Actual saved source state after a +50 vertical external displacement.
    advance_chain(state, coefficients, rest, {100, 200}, {0, 50});
    REQUIRE(state.position[0].y == 118.07445526123047f);
    REQUIRE(state.velocity[0].y == 0.17976197600364685f);
    state = snapshot;
    REQUIRE(advance_chain(state, coefficients, rest, {}, {}).displacement == initial.displacement);
    const ChainOutput mapping{{0.75f, 0.25f}, 2, -53.89473f, 1};
    const BendCoefficients bend_coefficients{0.3926991f, 3};
    BendState bend{0.355883569f, 0};
    for (int i = 0; i < 120; ++i)
        advance_bend(
            bend, bend_coefficients,
            unbent_chain_output(advance_chain(state, coefficients, rest, {}, {}), mapping));
    measured_allocations = 0;
    measure_allocations = true;
    ChainVariables value;
    for (int i = 0; i < 1000; ++i)
        value =
            advance_bend(bend, bend_coefficients,
                         unbent_chain_output(
                             advance_chain(state, coefficients, rest, {}, {0.1f, -0.2f}), mapping));
    measure_allocations = false;
    REQUIRE(measured_allocations == 0);
    REQUIRE(std::isfinite(value.root_x));
    REQUIRE(std::isfinite(value.vertical));
    REQUIRE(std::isfinite(value.tip_x));
    REQUIRE(independent.position == snapshot.position);
    REQUIRE(independent.velocity == snapshot.velocity);
}

TEST_CASE("Empty display frames retain neutral exported origins in selected branches", "[runtime]")
{
    auto asset = chain_rig();
    asset.parts[1].layers.back().frame_list[0].type = 0;
    auto compiled = runtime::compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    runtime::GeometryInstance instance(*compiled.scene);
    REQUIRE(instance.evaluate());
    const auto first = compiled.scene->locators()[0].node;
    const auto second = compiled.scene->locators()[1].node;
    REQUIRE(instance.sample_locator(first).state == runtime::LocatorState::Active);
    near(instance.sample_locator(first).position, {10, 20});
    REQUIRE(instance.sample_locator(second).state == runtime::LocatorState::Inactive);
    REQUIRE(instance.set_parameter(compiled.scene->find_variable("pick"), 1));
    REQUIRE(instance.evaluate());
    REQUIRE(instance.sample_locator(first).state == runtime::LocatorState::Inactive);
    REQUIRE(instance.sample_locator(second).state == runtime::LocatorState::Active);

    asset.parts[1].layers.back().frame_list[0].content.data.coord =
        field(std::array<float, 3>{1, 0, 0});
    compiled = runtime::compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    runtime::GeometryInstance unknown(*compiled.scene);
    REQUIRE(unknown.evaluate());
    REQUIRE(unknown.sample_locator(compiled.scene->locators()[0].node).status.error ==
            runtime::GeometryError::UnsupportedTransform);
}

TEST_CASE("Bound chains preserve isolated trajectories and compile fixed absent anchors",
          "[runtime]")
{
    using namespace nekomote::runtime;
    auto asset = chain_rig();
    SECTION("fixed absent anchor")
    {
        asset.chain_controls[0].binding.anchor = "missing";
    }
    SECTION("parts input routing")
    {
        asset.chain_controls[0].kind = ChainKind::Parts;
    }
    const bool parts = asset.chain_controls[0].kind == ChainKind::Parts;
    auto geometry = compile_geometry_scene(asset);
    REQUIRE(geometry.scene);
    auto compiled = compile_physics(asset, *geometry.scene);
    REQUIRE(compiled.program);
    asset = {}; // compiled programs own coefficients; the scene owns IDs
    GeometryInstance instance(*geometry.scene);
    REQUIRE(instance.evaluate());
    PhysicsInstance player(*compiled.program), independent(*compiled.program);
    REQUIRE(player.step(instance));
    REQUIRE(player.values().size() == 3);
    for (auto v : player.values())
        REQUIRE(v.value == 0.0f);
    PhysicsInputs inputs;
    if (parts)
        inputs.parts_displacement = {0, 50};
    else
        inputs.hair_displacement = {0, 50};
    REQUIRE(player.step(instance, inputs));
    REQUIRE_THAT(player.values()[1].value, Catch::Matchers::WithinAbs(-34.4314079284668, 1e-5));
    REQUIRE(player.values()[0].id == geometry.scene->find_variable("root"));
    REQUIRE(player.values()[1].id == geometry.scene->find_variable("vertical"));
    REQUIRE(player.values()[2].id == geometry.scene->find_variable("tip"));
    REQUIRE(player.step(instance));
    REQUIRE_THAT(player.values()[1].value, Catch::Matchers::WithinAbs(-34.39291000366211, 1e-5));
    REQUIRE(independent.step(instance));
    REQUIRE(independent.values()[1].value == 0.0f);
    player.reset();
    REQUIRE(player.tick_count() == 0);
    REQUIRE(player.values().empty());
    REQUIRE(player.step(instance));
    REQUIRE(player.values()[1].value == 0.0f);
    for (int i = 0; i < 120; ++i)
        REQUIRE(player.step(instance));
    measured_allocations = 0;
    measure_allocations = true;
    bool success = true;
    for (int i = 0; i < 200; ++i)
    {
        success &= static_cast<bool>(instance.evaluate());
        success &= static_cast<bool>(player.step(instance, inputs));
        for (auto v : player.values())
            success &= static_cast<bool>(instance.set_parameter(v.id, v.value));
        success &= static_cast<bool>(instance.evaluate());
        success &= static_cast<bool>(instance.build_frame());
    }
    measure_allocations = false;
    REQUIRE(success);
    REQUIRE(measured_allocations == 0);
}

TEST_CASE("A failed chain step cannot partially advance mixed spring and chain state", "[runtime]")
{
    using namespace nekomote::runtime;
    auto asset = chain_rig();
    auto spring = spring_rig().spring_controls[0];
    asset.spring_controls.push_back(spring);
    auto& chain = asset.chain_controls[0];
    const float maximum = std::numeric_limits<float>::max();
    chain.position = {};
    chain.velocity = {};
    chain.gravity = 0;
    chain.stretch_stiffness = maximum;
    chain.binding.anchor = "fixed-zero";
    auto geometry = compile_geometry_scene(asset);
    REQUIRE(geometry.scene);
    auto compiled = compile_physics(asset, *geometry.scene);
    REQUIRE(compiled.program);
    GeometryInstance instance(*geometry.scene);
    REQUIRE(instance.evaluate());
    PhysicsInstance player(*compiled.program), control(*compiled.program);
    REQUIRE(player.step(instance));
    REQUIRE(control.step(instance));
    PhysicsInputs overflowing;
    overflowing.hair_displacement = {maximum * 0.25f, 0};
    REQUIRE(player.step(instance, overflowing).error == PhysicsError::NonfiniteState);
    REQUIRE(player.tick_count() == 1);
    REQUIRE(player.values().empty());
    PhysicsInputs inputs;
    REQUIRE(player.step(instance, inputs));
    REQUIRE(control.step(instance, inputs));
    REQUIRE(player.tick_count() == 2);
    REQUIRE(player.values().size() == 5);
    for (std::size_t i = 0; i < player.values().size(); ++i)
    {
        REQUIRE(player.values()[i].id == control.values()[i].id);
        REQUIRE(player.values()[i].value == control.values()[i].value);
    }
}

TEST_CASE("Chain compilation rejects unverified snapshots and invalid mapping", "[runtime]")
{
    auto asset = chain_rig();
    auto geometry = runtime::compile_geometry_scene(asset);
    REQUIRE(geometry.scene);
    auto& chain = asset.chain_controls[0];
    SECTION("vertical effect")
    {
        chain.vertical_effect = 1;
    }
    SECTION("saved anchor")
    {
        chain.previous_anchor[0] = 1;
    }
    SECTION("rest geometry")
    {
        chain.rest_position[0][0] = 1;
    }
    SECTION("saved z")
    {
        chain.position[1][2] = 1;
    }
    SECTION("friction")
    {
        chain.friction[1] = 1.1f;
    }
    SECTION("mapping")
    {
        chain.scale_x[1] = std::numeric_limits<float>::quiet_NaN();
    }
    SECTION("bend")
    {
        chain.bend_gain = 1.1f;
    }
    SECTION("unknown field")
    {
        chain.binding.unsupported_fields = {"future"};
    }
    SECTION("shared output")
    {
        chain.variable_middle = chain.binding.variable_x;
    }
    auto compiled = runtime::compile_physics(asset, *geometry.scene);
    REQUIRE_FALSE(compiled.program);
    REQUIRE_FALSE(compiled.errors.empty());
}

TEST_CASE("Placed model origins participate in parent matrix rounding and invalidation",
          "[runtime]")
{
    auto asset = chain_rig();
    auto& root = asset.parts[0].layers[0];
    root.frame_list[0].content.data.coord = field(std::array<float, 3>{0, -409, 4});
    root.frame_list[1].content.data.coord = field(std::array<float, 3>{0, -400, 4});
    auto compiled = runtime::compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    runtime::GeometryInstance instance(*compiled.scene), independent(*compiled.scene);
    REQUIRE(instance.set_origin({0, 400}));
    REQUIRE(instance.evaluate());
    REQUIRE(instance.sample_locator(compiled.scene->locators()[0].node).position == Vec2{0, -9});
    REQUIRE(independent.evaluate());
    REQUIRE(independent.sample_locator(compiled.scene->locators()[0].node).position ==
            Vec2{0, -409});
    REQUIRE(instance.set_origin({7, 405}));
    REQUIRE(instance.sample_locator(compiled.scene->locators()[0].node).status.error ==
            runtime::GeometryError::UnsupportedTransform);
    REQUIRE(instance.frame().vertices.empty());
    REQUIRE(instance.evaluate());
    REQUIRE(instance.sample_locator(compiled.scene->locators()[0].node).position == Vec2{7, -4});
    REQUIRE(instance.build_frame());
    REQUIRE_FALSE(instance.frame().vertices.empty());
    REQUIRE_FALSE(instance.set_origin({std::numeric_limits<float>::infinity(), 0}));
    REQUIRE(instance.sample_locator(compiled.scene->locators()[0].node).position == Vec2{7, -4});
    measured_allocations = 0;
    measure_allocations = true;
    bool success = true;
    for (int i = 0; i < 200; ++i)
    {
        success &= static_cast<bool>(instance.set_origin({0, static_cast<float>(i)}));
        success &= static_cast<bool>(instance.evaluate());
        success &= static_cast<bool>(instance.build_frame());
    }
    measure_allocations = false;
    REQUIRE(success);
    REQUIRE(measured_allocations == 0);
}

TEST_CASE("Absent-anchor chain binding matches original exported-output observations", "[runtime]")
{
    std::ifstream input(std::string(NEKOMOTE_FIXTURES_DIR) + "/chain_binding_reference.json");
    REQUIRE(input.good());
    const auto fixture = nlohmann::json::parse(input);
    for (const auto& experiment : fixture.at("cases"))
    {
        const auto name = experiment.at("case").get<std::string>();
        if (name != "absent-side-enabled" && name != "absent-side-move")
            continue;
        INFO(name);
        auto asset = chain_rig();
        asset.chain_controls[0].binding.anchor = "missing";
        auto geometry = runtime::compile_geometry_scene(asset);
        REQUIRE(geometry.scene);
        auto compiled = runtime::compile_physics(asset, *geometry.scene);
        REQUIRE(compiled.program);
        runtime::GeometryInstance instance(*geometry.scene);
        REQUIRE(instance.set_origin({0, 400}));
        REQUIRE(instance.evaluate());
        runtime::PhysicsInstance player(*compiled.program);
        runtime::PhysicsInputs inputs;
        for (const auto& sample : experiment.at("samples"))
        {
            for (const auto& event : experiment.at("inputs").at("events"))
                if (event.at("step") == sample.at("step") && event.contains("forces"))
                {
                    const auto& force = event.at("forces").at("hair");
                    inputs.hair_displacement = {force.at(0).get<float>(), force.at(1).get<float>()};
                }
            REQUIRE(player.step(instance, inputs));
            const auto& expected = sample.at("values"); // tip, vertical, root in source order
            for (std::size_t i = 0; i < 3; ++i)
                REQUIRE_THAT(
                    player.values()[i].value,
                    Catch::Matchers::WithinAbs(expected.at(2 - i).get<double>(),
                                               fixture.at("value_tolerance").get<double>()));
        }
    }
}

TEST_CASE("Combined playback advances every animation and physics tick at any display rate",
          "[runtime][playback]")
{
    using namespace runtime;
    auto asset = chain_rig();
    asset.spring_controls = spring_rig().spring_controls;
    VariableControl control;
    control.variable = "pick";
    control.end_frame = 1;
    asset.variable_controls.push_back(control);
    auto compiled = compile_playback(asset);
    REQUIRE(compiled.program);
    PlaybackInstance at30(*compiled.program), at60(*compiled.program), at144(*compiled.program);
    const auto pick = compiled.program->scene().find_variable("pick");
    PhysicsInputs force;
    force.hair_displacement = {0.125f, -0.0625f};
    force.bust_displacement = {0.125f, -0.0625f};
    for (auto* player : {&at30, &at60, &at144})
    {
        REQUIRE(player->set_origin({0, 400}));
        REQUIRE(player->transition(pick, 1, 60));
    }
    for (const auto& [player, hz] :
         {std::pair{&at30, 30}, std::pair{&at60, 60}, std::pair{&at144, 144}})
        for (int i = 0; i < hz; ++i)
        {
            REQUIRE(player->advance(1.0 / hz, force));
            REQUIRE(player->prepare_frame());
        }
    REQUIRE(at30.tick_count() == 60);
    REQUIRE(at60.tick_count() == 60);
    REQUIRE(at144.tick_count() == 60);
    for (std::size_t i = 0; i < at30.values().size(); ++i)
    {
        REQUIRE(at30.values()[i].value == at60.values()[i].value);
        REQUIRE(at30.values()[i].value == at144.values()[i].value);
    }
    REQUIRE(at30.values()[pick.value - 1].value == 1.0f);
    REQUIRE(at30.values()[compiled.program->scene().find_variable("root").value - 1].value != 0.0f);
    REQUIRE(at30.frame().vertices.size() == at144.frame().vertices.size());
    for (std::size_t i = 0; i < at30.frame().vertices.size(); ++i)
    {
        REQUIRE(at30.frame().vertices[i].x == at144.frame().vertices[i].x);
        REQUIRE(at30.frame().vertices[i].y == at144.frame().vertices[i].y);
    }
    // Explicit ticks through the public session also match the separately tested
    // modules, using genuine moving anchors rather than a constant input pose.
    auto geometry = compile_geometry_scene(asset);
    REQUIRE(geometry.scene);
    auto animation = compile_animation(asset, *geometry.scene);
    REQUIRE(animation.program);
    auto physics = compile_physics(asset, *geometry.scene);
    REQUIRE(physics.program);
    AnimationInstance animator(*animation.program);
    GeometryInstance anchors(*geometry.scene);
    PhysicsInstance solver(*physics.program);
    REQUIRE(anchors.set_origin({0, 400}));
    REQUIRE(animator.transition(geometry.scene->find_variable("pick"), 1, 60));
    for (int i = 0; i < 60; ++i)
    {
        REQUIRE(animator.step());
        for (const auto parameter : animator.values())
            REQUIRE(anchors.set_parameter(parameter.id, parameter.value));
        REQUIRE(anchors.evaluate());
        REQUIRE(solver.step(anchors, force));
    }
    for (const auto parameter : solver.values())
        REQUIRE(at30.values()[parameter.id.value - 1].value == parameter.value);
    at60.set_playback_state(PlaybackState::Paused);
    REQUIRE(at60.advance(1000, force));
    REQUIRE(at60.step(force));
    REQUIRE(at60.tick_count() == 60);
    for (std::size_t i = 0; i < at30.values().size(); ++i)
        REQUIRE(at30.values()[i].value == at60.values()[i].value);
    at60.set_playback_state(PlaybackState::Running);
    REQUIRE(at60.advance(0, force));
    REQUIRE(at60.tick_count() == 60);
    REQUIRE(at60.advance(1000, force));
    REQUIRE(at60.tick_count() == 75);
    for (int i = 0; i < 15; ++i)
        REQUIRE(at30.step(force));
    for (std::size_t i = 0; i < at30.values().size(); ++i)
        REQUIRE(at30.values()[i].value == at60.values()[i].value);
}
TEST_CASE("Playback owns compilation data and resets independent placed instances",
          "[runtime][playback]")
{
    using namespace runtime;
    auto asset = spring_rig();
    auto compiled = compile_playback(asset);
    REQUIRE(compiled.program);
    asset = {};
    PlaybackInstance first(*compiled.program), independent(*compiled.program);
    REQUIRE(first.set_origin({0, 400}));
    REQUIRE(first.prepare_frame());
    REQUIRE_FALSE(first.frame().vertices.empty());
    const auto initial_y = first.frame().vertices.front().y;
    for (int i = 0; i < 20; ++i)
        REQUIRE(first.step({1, {1, 2}}));
    REQUIRE(first.frame().vertices.empty());
    REQUIRE(independent.tick_count() == 0);
    for (const auto parameter : independent.values())
        REQUIRE(parameter.value == 0.0f);
    first.reset();
    REQUIRE(first.tick_count() == 0);
    REQUIRE(first.prepare_frame());
    REQUIRE(first.frame().vertices.front().y == initial_y);
    for (int i = 0; i < 20; ++i)
    {
        REQUIRE(first.step({1, {1, 2}}));
        REQUIRE(independent.step({1, {1, 2}}));
    }
    for (std::size_t i = 0; i < first.values().size(); ++i)
        REQUIRE(first.values()[i].value == independent.values()[i].value);
}
TEST_CASE("Playback faults hide stale frames and require reset after a failed author tick",
          "[runtime][playback]")
{
    using namespace runtime;
    auto asset = spring_rig();
    auto compiled = compile_playback(asset);
    REQUIRE(compiled.program);
    PlaybackInstance player(*compiled.program);
    REQUIRE(player.prepare_frame());
    REQUIRE_FALSE(player.frame().vertices.empty());
    REQUIRE(player.step());
    REQUIRE(player.prepare_frame());
    REQUIRE(player.step({std::numeric_limits<float>::quiet_NaN(), {}}).error ==
            PlaybackError::Physics);
    REQUIRE(player.tick_count() == 1);
    REQUIRE(player.values().empty());
    REQUIRE(player.frame().vertices.empty());
    REQUIRE(player.step().error == PlaybackError::Faulted);
    REQUIRE(player.advance(1).error == PlaybackError::Faulted);
    REQUIRE(player.prepare_frame().error == PlaybackError::Faulted);
    REQUIRE(player.transition(compiled.program->scene().find_variable("pick"), 1).error ==
            PlaybackError::Faulted);
    player.reset();
    REQUIRE(player.step());
    REQUIRE(player.tick_count() == 1);
    REQUIRE(player.prepare_frame());
    REQUIRE_FALSE(player.values().empty());
    REQUIRE_FALSE(player.frame().vertices.empty());
    auto unsupported = asset;
    unsupported.spring_controls[0].binding.unsupported_fields = {"unknown_force"};
    REQUIRE_FALSE(compile_playback(unsupported).program);
    REQUIRE(compile_playback(unsupported, PhysicsMode::DisabledDiagnostic).program);
    REQUIRE_FALSE(compile_playback(asset, static_cast<PhysicsMode>(100)).program);
}
TEST_CASE("Combined playback reuses frame storage without allocating during catchup",
          "[runtime][playback][allocation]")
{
    using namespace runtime;
    auto asset = chain_rig();
    asset.spring_controls = spring_rig().spring_controls;
    auto compiled = compile_playback(asset);
    REQUIRE(compiled.program);
    PlaybackInstance player(*compiled.program);
    for (int i = 0; i < 120; ++i)
    {
        REQUIRE(player.step());
        REQUIRE(player.prepare_frame());
    }
    bool success = true;
    measured_allocations = 0;
    measure_allocations = true;
    for (int i = 0; i < 200; ++i)
    {
        success &= static_cast<bool>(player.advance(1.0 / 30));
        success &= static_cast<bool>(player.prepare_frame());
        success &= static_cast<bool>(player.prepare_frame());
    }
    measure_allocations = false;
    REQUIRE(success);
    REQUIRE(measured_allocations == 0);
    REQUIRE(player.tick_count() == 520);
}

TEST_CASE("Scene key weights retain the original blend rounding at a patch corner", "[runtime]")
{
    using namespace runtime;
    std::ifstream input(std::string(NEKOMOTE_FIXTURES_DIR) + "/scene_blend_reference.json");
    REQUIRE(input);
    const auto reference = nlohmann::json::parse(input);
    REQUIRE_FALSE(reference.at("source_stores_truncated").get<bool>());
    auto asset = rig();
    asset.parts.resize(1);
    asset.icons["atlas/a"] = {"atlas", 0, 0, 1, 1, 0, 0};
    auto content = sprite();
    MeshData mesh;
    const auto patch = identity_patch();
    std::array<float, 32> points{};
    for (std::size_t i = 0; i < patch.size(); ++i)
    {
        points[i * 2] = patch[i].x;
        points[i * 2 + 1] = patch[i].y;
    }
    points[0] = reference.at("left").get<float>();
    mesh.bp = field(points);
    content.mesh = field(mesh);
    auto warp = layer("warp", 0, content);
    warp.frame_list[0].type = 3;
    warp.parameterize = field(0);
    warp.parameter = {{"blend", 0, 30, 30, 0, true}};
    points[0] = reference.at("right").get<float>();
    mesh.bp = field(points);
    content.mesh = field(mesh);
    warp.frame_list.push_back({30, 2, field(content)});
    asset.parts[0].layers = {warp};
    auto compiled = compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    GeometryInstance instance(*compiled.scene);
    REQUIRE(instance.set_parameter(compiled.scene->find_variable("blend"),
                                   reference.at("parameter").get<float>()));
    REQUIRE(instance.evaluate());
    REQUIRE(instance.primitives().size() == 1);
    const auto sample = instance.sample_atlas_point(instance.primitives()[0].node, {0, 0});
    REQUIRE(sample.x == reference.at("expected").get<float>());
}

TEST_CASE("Rigid angles retain source degree interpolation and wrapped Float32 coefficients",
          "[runtime]")
{
    using namespace runtime;
    std::ifstream input(std::string(NEKOMOTE_FIXTURES_DIR) + "/transform_precision_reference.json");
    REQUIRE(input);
    const auto reference = nlohmann::json::parse(input);
    for (const auto& observation : reference.at("rotation"))
    {
        auto asset = rig();
        asset.parts.resize(1);
        asset.icons["atlas/a"] = {"atlas", 0, 0, 1, 1, 0, 0};
        auto content = sprite();
        content.angle = field(observation.value("degrees", 0.0f));
        auto node = layer("rotation", 0, content);
        if (observation.contains("parameter"))
        {
            node.parameterize = field(0);
            node.parameter = {{"angle", -30, 30, 60, 0, true}};
            node.frame_list = {{30, 3, field(content)}};
            content.angle = field(8.0f);
            node.frame_list.push_back({60, 2, field(content)});
        }
        asset.parts[0].layers = {node};
        auto compiled = compile_geometry_scene(asset);
        REQUIRE(compiled.scene);
        GeometryInstance instance(*compiled.scene);
        if (observation.contains("parameter"))
            REQUIRE(instance.set_parameter(compiled.scene->find_variable("angle"),
                                           observation.at("parameter").get<float>()));
        REQUIRE(instance.evaluate());
        const auto point = instance.sample_atlas_point(instance.primitives()[0].node, {0, 1});
        REQUIRE(point.x == -observation.at("sine").get<float>());
        REQUIRE(point.y == observation.at("cosine").get<float>());
    }
}

TEST_CASE("Combined helpers accumulate from the leaf toward the nearest parent", "[runtime]")
{
    using namespace runtime;
    std::ifstream input(std::string(NEKOMOTE_FIXTURES_DIR) + "/transform_precision_reference.json");
    REQUIRE(input);
    const auto reference = nlohmann::json::parse(input).at("combination");
    auto asset = rig();
    asset.parts.resize(1);
    asset.icons["atlas/a"] = {"atlas", 0, 0, 1, 1, 0, 0};
    auto child = layer("sprite", 0, sprite());
    const auto& controls = reference.at("ancestors_root_to_leaf");
    for (std::size_t i = controls.size(); i-- > 0;)
    {
        auto patch = identity_patch();
        // The same observed scalar arithmetic at a corner is directly visible
        // in a synthetic one-pixel domain, without the original model images.
        patch[12].y = controls[i][1].get<float>();
        std::array<float, 32> points{};
        for (std::size_t j = 0; j < patch.size(); ++j)
        {
            points[j * 2] = patch[j].x;
            points[j * 2 + 1] = patch[j].y;
        }
        MeshData mesh;
        mesh.bp = field(points);
        FrameContent content;
        content.src = field(std::string{"blank"});
        content.icon = field(std::string{"1:1:0:0"});
        content.mesh = field(mesh);
        auto parent = layer("helper", 0, content);
        parent.mesh_combine = i == 0 ? 0 : 1;
        parent.mesh_sync_child_mask = 8;
        parent.children = {child};
        child = std::move(parent);
    }
    asset.parts[0].layers = {child};
    auto compiled = compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    GeometryInstance instance(*compiled.scene);
    REQUIRE(instance.evaluate());
    REQUIRE(instance.primitives().size() == 1);
    const auto point = instance.sample_atlas_point(instance.primitives()[0].node, {0, 1});
    REQUIRE(point.x == 0.0f);
    REQUIRE(point.y == reference.at("expected")[1].get<float>());
}

TEST_CASE("Canonical unit curves preserve the observed world locator rounding", "[runtime]")
{
    using namespace runtime;
    std::ifstream input(std::string(NEKOMOTE_FIXTURES_DIR) + "/transform_precision_reference.json");
    REQUIRE(input);
    const auto reference = nlohmann::json::parse(input).at("identity_domain");
    auto asset = rig();
    asset.parts.resize(1);
    FrameContent content;
    content.src = field(std::string{"blank"});
    content.icon = field(std::string{"928:1123:464:561"});
    content.coord = field(std::array<float, 3>{1, 169, 0});
    MeshData mesh;
    std::array<float, 32> points{};
    const auto patch = identity_patch();
    for (std::size_t i = 0; i < patch.size(); ++i)
    {
        points[i * 2] = patch[i].x;
        points[i * 2 + 1] = patch[i].y;
    }
    mesh.bp = field(points);
    content.mesh = field(mesh);
    auto parent = layer("unit curve", 0, content);
    parent.mesh_sync_child_mask = 8;
    FrameContent marker_content;
    marker_content.coord = field(std::array<float, 3>{
        reference.at("point")[0].get<float>() - 1, reference.at("point")[1].get<float>() - 169, 0});
    auto marker = layer("anchor", 1, marker_content);
    marker.export_self = 1;
    parent.children = {marker};
    asset.parts[0].layers = {parent};
    auto compiled = compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    GeometryInstance instance(*compiled.scene);
    REQUIRE(instance.evaluate());
    REQUIRE(compiled.scene->locators().size() == 1);
    const auto sample = instance.sample_locator(compiled.scene->locators()[0].node);
    REQUIRE(sample.status);
    REQUIRE(sample.position.x == reference.at("expected")[0].get<float>());
    REQUIRE(sample.position.y == reference.at("expected")[1].get<float>());
}

TEST_CASE("Physical anchor differences consume saved absolute author coordinates", "[runtime]")
{
    using namespace runtime;
    std::ifstream input(std::string(NEKOMOTE_FIXTURES_DIR) + "/transform_precision_reference.json");
    REQUIRE(input);
    const auto reference = nlohmann::json::parse(input).at("physics_anchor");
    auto asset = spring_rig();
    asset.author_scale = field(reference.at("author_scale").get<float>());
    auto& frames = asset.parts[0].layers[0].frame_list;
    for (std::size_t i = 0; i < 2; ++i)
    {
        const auto& point = reference.at(i == 0 ? "first" : "second");
        frames[i].content.data.coord =
            field(std::array<float, 3>{point[0].get<float>(), point[1].get<float>(), 0});
    }
    auto geometry = compile_geometry_scene(asset);
    REQUIRE(geometry.scene);
    auto program = compile_physics(asset, *geometry.scene);
    REQUIRE(program.program);
    GeometryInstance anchors(*geometry.scene);
    PhysicsInstance physics(*program.program);
    REQUIRE(anchors.evaluate());
    REQUIRE(physics.step(anchors));
    REQUIRE(anchors.set_parameter(geometry.scene->find_variable("pick"), 1));
    REQUIRE(anchors.evaluate());
    REQUIRE(physics.step(anchors));
    SpringState isolated;
    advance_spring(isolated, {0.5f, 0.125f},
                   {reference.at("delta")[0].get<float>(), reference.at("delta")[1].get<float>()},
                   {});
    const auto expected = spring_output(isolated, {{1, 2}, 0, 1});
    REQUIRE(physics.values()[0].value == expected.x);
    REQUIRE(physics.values()[1].value == expected.y);
    asset.author_scale = field(std::numeric_limits<float>::denorm_min());
    auto extreme = compile_physics(asset, *geometry.scene);
    REQUIRE(extreme.program);
    PhysicsInstance extreme_physics(*extreme.program);
    REQUIRE(anchors.set_scale(0.5f));
    REQUIRE(anchors.evaluate());
    REQUIRE(extreme_physics.step(anchors).error == PhysicsError::InvalidInput);
    REQUIRE(extreme_physics.tick_count() == 0);
    REQUIRE(physics.tick_count() == 2);
}

TEST_CASE("Placed nested curves preserve the observed physical locator coordinates", "[runtime]")
{
    using namespace runtime;
    std::ifstream input(std::string(NEKOMOTE_FIXTURES_DIR) + "/world_patch_reference.json");
    REQUIRE(input);
    const auto reference = nlohmann::json::parse(input);
    auto asset = rig();
    asset.parts.resize(1);
    asset.icons["atlas/a"] = {"atlas", 0, 0, 1, 1, 0, 0};
    const auto domain_layer = [&](std::size_t index)
    {
        const auto& source = reference.at("domains")[index];
        const auto& domain = source.at("domain");
        FrameContent content;
        content.src = field(std::string{"blank"});
        content.icon = field(std::to_string(domain[0].get<int>()) + ":" +
                             std::to_string(domain[1].get<int>()) + ":" +
                             std::to_string(domain[2].get<int>()) + ":" +
                             std::to_string(domain[3].get<int>()));
        content.coord = field(std::array<float, 3>{source.at("translation")[0].get<float>(),
                                                   source.at("translation")[1].get<float>(), 0});
        MeshData mesh;
        std::array<float, 32> points{};
        for (std::size_t i = 0; i < 16; ++i)
        {
            points[i * 2] = source.at("patch")[i][0].get<float>();
            points[i * 2 + 1] = source.at("patch")[i][1].get<float>();
        }
        mesh.bp = field(points);
        content.mesh = field(mesh);
        auto result = layer("domain" + std::to_string(index), 2, content);
        result.mesh_sync_child_mask = 8;
        return result;
    };
    auto outer = domain_layer(0), inner = domain_layer(1);
    auto marker = layer("anchor", 1, FrameContent{});
    marker.export_self = 1;
    inner.children = {marker, layer("pixel", 0, sprite())};
    outer.children = {inner};
    asset.parts[0].layers = {outer};
    auto compiled = compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    REQUIRE(compiled.scene->locators().size() == 1);
    GeometryInstance instance(*compiled.scene);
    REQUIRE(instance.set_origin(
        {reference.at("origin")[0].get<float>(), reference.at("origin")[1].get<float>()}));
    REQUIRE(instance.evaluate());
    const auto sample = instance.sample_locator(compiled.scene->locators()[0].node);
    REQUIRE(sample.status);
    REQUIRE(sample.state == LocatorState::Active);
    REQUIRE(sample.position.x == reference.at("expected")[0].get<float>());
    REQUIRE(sample.position.y == reference.at("expected")[1].get<float>());
    REQUIRE(instance.build_frame());
    REQUIRE_FALSE(instance.frame().vertices.empty());
    REQUIRE(instance.frame().vertices[0].x == sample.position.x);
    REQUIRE(instance.frame().vertices[0].y == sample.position.y);
}

TEST_CASE("Parameterized scene time has one Float32 save before key weighting", "[runtime]")
{
    using namespace runtime;
    std::ifstream input(std::string(NEKOMOTE_FIXTURES_DIR) + "/world_patch_reference.json");
    REQUIRE(input);
    const auto reference = nlohmann::json::parse(input).at("parameter_time");
    auto asset = rig();
    asset.parts.resize(1);
    asset.icons["atlas/a"] = {"atlas", 0, 0, 1, 1, 0, 0};
    auto content = sprite();
    MeshData mesh;
    auto patch = identity_patch();
    std::array<float, 32> points{};
    for (std::size_t i = 0; i < patch.size(); ++i)
    {
        points[i * 2] = patch[i].x;
        points[i * 2 + 1] = patch[i].y;
    }
    mesh.bp = field(points);
    content.mesh = field(mesh);
    auto warp = layer("warp", 0, content);
    warp.frame_list[0].time = reference.at("left_time").get<float>();
    warp.frame_list[0].type = 3;
    warp.parameterize = field(0);
    warp.parameter = {{"blend", -30, 30, 60, 0, true}};
    points[0] = 1;
    mesh.bp = field(points);
    content.mesh = field(mesh);
    warp.frame_list.push_back({reference.at("right_time").get<float>(), 2, field(content)});
    asset.parts[0].layers = {warp};
    auto compiled = compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    GeometryInstance instance(*compiled.scene);
    REQUIRE(instance.set_parameter(compiled.scene->find_variable("blend"),
                                   reference.at("parameter").get<float>()));
    REQUIRE(instance.evaluate());
    REQUIRE(instance.primitives().size() == 1);
    REQUIRE(instance.sample_atlas_point(instance.primitives()[0].node, {0, 0}).x ==
            reference.at("expected_weight").get<float>());
}

TEST_CASE("Model scaling preserves author physics and independent resettable sessions",
          "[runtime][playback]")
{
    using namespace runtime;
    auto asset = chain_rig();
    asset.spring_controls = spring_rig().spring_controls;
    auto compiled = compile_playback(asset);
    REQUIRE(compiled.program);
    PlaybackInstance original(*compiled.program), scaled(*compiled.program);
    REQUIRE(scaled.set_scale(2));
    const auto pick = compiled.program->scene().find_variable("pick");
    for (int tick = 0; tick < 72; ++tick)
    {
        const float target = tick % 6 == 0 ? 1.0f : 0.0f;
        REQUIRE(original.transition(pick, target));
        REQUIRE(scaled.transition(pick, target));
        REQUIRE(original.step());
        REQUIRE(scaled.step());
        REQUIRE(original.values().size() == scaled.values().size());
        for (std::size_t i = 0; i < original.values().size(); ++i)
            REQUIRE(original.values()[i].value == scaled.values()[i].value);
    }
    original.reset();
    scaled.reset();
    REQUIRE(original.prepare_frame());
    REQUIRE(scaled.prepare_frame());
    REQUIRE(original.frame().vertices.size() == scaled.frame().vertices.size());
    for (std::size_t i = 0; i < original.frame().vertices.size(); ++i)
    {
        REQUIRE(scaled.frame().vertices[i].x == original.frame().vertices[i].x * 2);
        REQUIRE(scaled.frame().vertices[i].y == original.frame().vertices[i].y * 2);
    }
    const auto previous = scaled.frame().vertices[0];
    for (const float invalid :
         {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
          std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::denorm_min()})
    {
        REQUIRE(scaled.set_scale(invalid).geometry.error == GeometryError::InvalidScale);
        REQUIRE_FALSE(scaled.frame().vertices.empty());
        REQUIRE(scaled.frame().vertices[0].x == previous.x);
        REQUIRE(scaled.frame().vertices[0].y == previous.y);
    }
    REQUIRE(scaled.set_scale(0.5f));
    REQUIRE(scaled.frame().vertices.empty());
    REQUIRE(scaled.prepare_frame());
    for (std::size_t i = 0; i < original.frame().vertices.size(); ++i)
    {
        REQUIRE(scaled.frame().vertices[i].x == original.frame().vertices[i].x * 0.5f);
        REQUIRE(scaled.frame().vertices[i].y == original.frame().vertices[i].y * 0.5f);
    }
    measured_allocations = 0;
    measure_allocations = true;
    bool success = true;
    for (int i = 0; i < 200; ++i)
    {
        success &= static_cast<bool>(scaled.set_scale(i % 2 == 0 ? 0.5f : 2.0f));
        success &= static_cast<bool>(scaled.step());
        success &= static_cast<bool>(scaled.prepare_frame());
    }
    measure_allocations = false;
    REQUIRE(success);
    REQUIRE(measured_allocations == 0);
}

TEST_CASE("Child synchronization selects rigid origins or full curves through helpers",
          "[runtime][scene]")
{
    // The mask-one face UD/LR capture shifts the LR origin before evaluating its
    // child positions. Mask eight instead adds both normalized patch shapes.
    for (const auto [mode, expected] :
         {std::pair{0, Vec2{2, 3}}, std::pair{1, Vec2{6, 11}}, std::pair{8, Vec2{6, 14}}})
    {
        auto asset = rig();
        asset.parts.resize(1);
        asset.icons.at("atlas/a").origin_x = asset.icons.at("atlas/a").origin_y = 0;
        FrameContent blank;
        blank.src = field(std::string{"blank"});
        blank.icon = field(std::string{"16:16:8:8"});
        auto patch = runtime::identity_patch();
        const auto content = [&](bool stretch)
        {
            auto result = blank;
            MeshData mesh;
            std::array<float, 32> raw{};
            for (std::size_t i = 0; i < patch.size(); ++i)
            {
                raw[i * 2] = patch[i].x + (stretch ? 0 : 0.25f);
                raw[i * 2 + 1] = patch[i].y * (stretch ? 2 : 1);
            }
            mesh.bp = field(raw);
            result.mesh = field(mesh);
            return result;
        };
        auto child_content = sprite();
        child_content.coord = field(std::array<float, 3>{2, 3, 0});
        auto child = layer("child", 0, child_content);
        auto helper = layer("helper", 2, content(false));
        helper.mesh_combine = 1;
        helper.mesh_sync_child_mask = mode;
        helper.children = {child};
        auto root = layer("root", 2, content(true));
        root.mesh_sync_child_mask = mode;
        root.children = {helper};
        asset.parts[0].layers = {root};
        auto scene = runtime::compile_geometry_scene(asset);
        REQUIRE(scene.scene);
        runtime::GeometryInstance instance(*scene.scene);
        REQUIRE(instance.evaluate());
        REQUIRE(instance.build_frame());
        REQUIRE(instance.primitives().size() == 1);
        near(instance.sample_atlas_point(instance.primitives()[0].node, {0, 0}), expected);
        near({instance.frame().vertices[0].x, instance.frame().vertices[0].y}, expected);
    }
}

TEST_CASE("Automatic grids retain both axes for small and slender source sprites",
          "[runtime][scene]")
{
    for (const auto dimensions :
         {std::array{6, 7, 219, 160, 9, 0}, std::array{109, 219, 928, 929, 15, 0},
          std::array{78, 61, 928, 929, 9, 1}})
    {
        auto asset = rig();
        asset.parts.resize(1);
        auto& rect = asset.icons.at("atlas/a");
        rect.width = static_cast<float>(dimensions[0]);
        rect.height = static_cast<float>(dimensions[1]);
        asset.textures[0].width = asset.textures[0].height = 256;
        auto content = sprite();
        content.src = field(std::string{"blank"});
        content.icon =
            field(std::to_string(dimensions[2]) + ":" + std::to_string(dimensions[3]) + ":0:0");
        MeshData mesh;
        std::array<float, 32> raw{};
        auto patch = runtime::identity_patch();
        for (std::size_t i = 0; i < patch.size(); ++i)
        {
            raw[i * 2] = patch[i].x;
            raw[i * 2 + 1] = patch[i].y;
        }
        mesh.bp = field(raw);
        content.mesh = field(mesh);
        auto parent = layer("domain", 2, content);
        parent.mesh_sync_child_mask = 8;
        auto child = layer("sprite", 0, sprite());
        child.mesh_division = dimensions[5];
        child.obj_tri_priority = 2;
        parent.children = {child};
        asset.parts[0].layers = {parent};
        auto scene = runtime::compile_geometry_scene(asset);
        REQUIRE(scene.scene);
        runtime::GeometryInstance instance(*scene.scene);
        REQUIRE(instance.evaluate());
        REQUIRE(instance.build_frame());
        REQUIRE(instance.frame().vertices.size() == static_cast<std::size_t>(dimensions[4]));
    }
}

TEST_CASE("Opacity-zero sprites still supply mask geometry without color draws", "[runtime][scene]")
{
    auto asset = rig();
    asset.parts.resize(1);
    auto hidden = sprite();
    hidden.opa = field(0.0f);
    auto source = layer("hidden mask source", 0, hidden);
    FrameContent blank;
    blank.src = field(std::string{"blank"});
    blank.icon = field(std::string{"32:32:16:16"});
    auto mask = layer("mask", 12, blank);
    mask.inherit_mask = 0x24007fc;
    mask.stencil_type = 5;
    mask.stencil_composite_mask_layers = {"hidden mask source"};
    mask.children = {layer("masked color", 0, sprite())};
    asset.parts[0].layers = {source, mask};
    auto scene = runtime::compile_geometry_scene(asset);
    REQUIRE(scene.scene);
    runtime::GeometryInstance instance(*scene.scene);
    REQUIRE(instance.evaluate());
    REQUIRE(instance.build_frame());
    auto frame = instance.frame();
    REQUIRE(frame.commands.size() == 1);
    REQUIRE(frame.commands[0].mask == MaskId{1});
    REQUIRE(frame.mask_commands.size() == 1);
    REQUIRE(frame.mask_commands[0].index_count == 6);
    REQUIRE(frame.mask_commands[0].first_index != frame.commands[0].first_index);
    REQUIRE(frame.vertices[frame.indices[frame.mask_commands[0].first_index]].color[3] == 0.0f);
    measured_allocations = 0;
    measure_allocations = true;
    bool success = true;
    for (int i = 0; i < 200; ++i)
    {
        success &= static_cast<bool>(instance.evaluate());
        success &= static_cast<bool>(instance.build_frame());
    }
    measure_allocations = false;
    REQUIRE(success);
    REQUIRE(measured_allocations == 0);
    asset.parts[0].layers[1].children[0].frame_list[0].content.data.opa = field(0.0f);
    auto inactive_scene = runtime::compile_geometry_scene(asset);
    REQUIRE(inactive_scene.scene);
    runtime::GeometryInstance inactive(*inactive_scene.scene);
    REQUIRE(inactive.evaluate());
    REQUIRE(inactive.build_frame());
    REQUIRE(inactive.primitives().empty());
    REQUIRE(inactive.frame().commands.empty());
    REQUIRE(inactive.frame().mask_commands.empty());
}
