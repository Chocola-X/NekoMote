#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/runtime/animation.hpp>
#include <nekomote/runtime/deformation.hpp>
#include <nekomote/runtime/geometry_scene.hpp>
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
