#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/application/session.hpp>
#include <nekomote/runtime/animation.hpp>
#include <nekomote/runtime/bend.hpp>
#include <nekomote/runtime/chain.hpp>
#include <nekomote/runtime/deformation.hpp>
#include <nekomote/runtime/geometry_scene.hpp>
#include <nekomote/runtime/physics.hpp>
#include <nekomote/runtime/playback.hpp>
#include <nekomote/runtime/spring.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace
{
thread_local bool measure_allocations = false;
thread_local std::size_t measured_allocations = 0;
} // namespace
// Project-owned, ordinary C++ heap allocations only; warmed math/framework and
// diagnostics are excluded. Geometry storage has no over-aligned element types.
// Keep allocation instrumentation opaque to optimizer allocation-pair inference.
[[gnu::noinline]] void* operator new(std::size_t size)
{
    if (measure_allocations)
        ++measured_allocations;
    if (void* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
[[gnu::noinline]] void* operator new[](std::size_t size)
{
    return ::operator new(size);
}
[[gnu::noinline]] void operator delete(void* p) noexcept
{
    std::free(p);
}
[[gnu::noinline]] void operator delete[](void* p) noexcept
{
    std::free(p);
}
[[gnu::noinline]] void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
[[gnu::noinline]] void operator delete[](void* p, std::size_t) noexcept
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
ModelAsset interactive_rig()
{
    auto result = chain_rig();
    result.spring_controls = spring_rig().spring_controls;
    return result;
}
void near(Vec2 actual, Vec2 expected)
{
    REQUIRE_THAT(actual.x, Catch::Matchers::WithinAbs(expected.x, 0.001f));
    REQUIRE_THAT(actual.y, Catch::Matchers::WithinAbs(expected.y, 0.001f));
}
} // namespace

TEST_CASE("Blank domains accept decimal exponents and reject malformed or out-of-range numbers",
          "[runtime][scene]")
{
    const auto compile_domain = [](const char* text)
    {
        auto asset = rig();
        auto& content = asset.parts[1].layers[0].frame_list[0].content.data;
        content.src = field(std::string{"blank"});
        content.icon = field(std::string{text});
        return runtime::compile_geometry_scene(asset);
    };
    REQUIRE(compile_domain("16:8:-2.5:1e0").scene);
    for (const auto* text : {"+16:8:0:0", " 16:8:0:0", "16:8:0,5:0", "16:8:0:0junk", "1e999:8:0:0",
                             "1e-999:8:0:0", "nan:8:0:0"})
    {
        INFO(text);
        const auto compiled = compile_domain(text);
        REQUIRE_FALSE(compiled.scene);
        REQUIRE_FALSE(compiled.errors.empty());
        REQUIRE(compiled.errors.front().find("invalid blank domain") != std::string::npos);
    }
}

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

TEST_CASE("Geometry topology survives repeated builds and resource and visibility changes",
          "[runtime][scene]")
{
    auto input = rig();
    input.icons.emplace("atlas/b", IconRect{"atlas", 8, 0, 4, 8, 1, 3});
    input.icons.emplace("atlas/c", IconRect{"atlas", 16, 0, 8, 4, 3, 1});
    MeshData mesh;
    std::array<float, 32> raw{};
    const auto patch = runtime::identity_patch();
    for (std::size_t i = 0; i < patch.size(); ++i)
    {
        raw[i * 2] = patch[i].x;
        raw[i * 2 + 1] = patch[i].y;
    }
    mesh.bp = field(raw);
    for (std::size_t i = 1; i < input.parts.size(); ++i)
    {
        auto& node = input.parts[i].layers[0];
        node.mesh_division = 3;
        node.frame_list[0].content.data.mesh = field(mesh);
    }
    input.parts[2].layers[0].frame_list[0].content.data.icon = field(std::string{"b"});
    auto same_grid = input.parts[1];
    same_grid.name = "C";
    same_grid.layers[0].frame_list[0].content.data.icon = field(std::string{"c"});
    input.parts.push_back(same_grid);
    auto& selector = input.parts[0].layers[0];
    selector.parameter[0].range_end = 3;
    selector.parameter[0].division = 3;
    auto hidden = selector.frame_list.back();
    hidden.time = 2;
    hidden.content.data.opa = field(0.0f);
    selector.frame_list.push_back(hidden);
    hidden.time = 3;
    hidden.content.data.icon = field(std::string{"C"});
    hidden.content.data.opa = field(1.0f);
    selector.frame_list.push_back(hidden);
    auto compiled = runtime::compile_geometry_scene(input);
    REQUIRE(compiled.scene);
    runtime::GeometryInstance reused(*compiled.scene);
    const auto parameter = compiled.scene->find_variable("pick");
    std::vector<std::uint32_t> initial;
    for (const float value : {0.0f, 0.0f, 3.0f, 3.0f, 1.0f, 1.0f, 2.0f, 2.0f, 0.0f})
    {
        CAPTURE(value);
        REQUIRE(reused.set_parameter(parameter, value));
        REQUIRE(reused.evaluate());
        REQUIRE(reused.build_frame());
        REQUIRE(reused.build_frame()); // No evaluate between these calls.
        runtime::GeometryInstance fresh(*compiled.scene);
        REQUIRE(fresh.set_parameter(parameter, value));
        REQUIRE(fresh.evaluate());
        REQUIRE(fresh.build_frame());
        const auto actual = reused.frame(), expected = fresh.frame();
        REQUIRE(std::equal(actual.indices.begin(), actual.indices.end(), expected.indices.begin(),
                           expected.indices.end()));
        REQUIRE(actual.vertices.size() == expected.vertices.size());
        REQUIRE(actual.commands.size() == expected.commands.size());
        for (std::size_t i = 0; i < actual.vertices.size(); ++i)
        {
            const auto& a = actual.vertices[i];
            const auto& b = expected.vertices[i];
            REQUIRE(std::array{a.x, a.y, a.u, a.v} == std::array{b.x, b.y, b.u, b.v});
            REQUIRE(a.color == b.color);
        }
        if (value == 0)
            initial.assign(actual.indices.begin(), actual.indices.end());
        else if (value == 1)
        {
            REQUIRE(actual.indices.size() == initial.size());
            REQUIRE_FALSE(std::equal(initial.begin(), initial.end(), actual.indices.begin()));
        }
        else if (value == 2)
            REQUIRE(actual.indices.empty());
        else
            REQUIRE(std::equal(initial.begin(), initial.end(), actual.indices.begin()));
    }
}

TEST_CASE("Automatic priority-two meshes match original size boundary captures", "[runtime][scene]")
{
    // Captured PSB grids at both sides of the 64 and 128 pixel boundaries.
    for (const auto& [size, points] : {std::pair{63, 20}, std::pair{64, 25}, std::pair{127, 56},
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
        parent.mesh_sync_child_mask = 8;
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

TEST_CASE("Parameterized nested motion roots retain their own time despite reference offsets",
          "[runtime]")
{
    auto asset = rig();
    auto& root = asset.parts[1].layers[0];
    root.parameterize = field(0);
    root.parameter = {{"pose", 0, 1, 1, 0, true}};
    auto baseline = runtime::compile_geometry_scene(asset);
    REQUIRE(baseline.scene);
    runtime::GeometryInstance original(*baseline.scene);
    REQUIRE(original.evaluate());
    REQUIRE(original.build_frame());
    const auto expected = original.frame();
    auto& reference = asset.parts[0].layers[0].frame_list[0].content.data;
    MotionContent offset;
    offset.mask = field(std::uint64_t{0});
    offset.time_offset = field(40.0f);
    reference.motion = field(offset);
    auto compiled = runtime::compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    runtime::GeometryInstance instance(*compiled.scene);
    REQUIRE(instance.evaluate());
    REQUIRE(instance.build_frame());
    REQUIRE(instance.frame().vertices.size() == expected.vertices.size());
    for (std::size_t i = 0; i < expected.vertices.size(); ++i)
    {
        REQUIRE(instance.frame().vertices[i].x == expected.vertices[i].x);
        REQUIRE(instance.frame().vertices[i].y == expected.vertices[i].y);
    }
    root.parameterize = {};
    auto unverified = runtime::compile_geometry_scene(asset);
    REQUIRE(unverified.scene);
    runtime::GeometryInstance free_clock(*unverified.scene);
    REQUIRE(free_clock.evaluate().error == runtime::GeometryError::UnsupportedTransform);
}

TEST_CASE("Warmed animation, expression and Eye routing allocate no project heap",
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
    VariableControl eye;
    eye.kind = VariableControlKind::Eye;
    eye.blink_enabled = true;
    eye.blink_frames = 16;
    eye.blink_interval_min = eye.blink_interval_max = 30;
    eye.variable = "eye";
    eye.begin_frame = 0;
    eye.end_frame = 10;
    eye.edges = {{{-10, 20}}, {{30, 30}}};
    eye.junctions = {{10, 30, 0}};
    asset.variable_controls.push_back(std::move(eye));
    auto compiled = runtime::compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    auto animation = runtime::compile_animation(asset, *compiled.scene);
    REQUIRE(animation.program);
    runtime::AnimationInstance player(*animation.program);
    runtime::GeometryInstance geometry(*compiled.scene);
    const auto cycle = animation.program->find_timeline("cycle");
    const auto parameter = compiled.scene->find_variable("expression");
    const auto eye_parameter = compiled.scene->find_variable("eye");
    REQUIRE(player.play(cycle, runtime::TimelinePlayMode::Parallel));
    const auto update = [&](int frame)
    {
        if (frame % 20 == 0 && !player.transition(parameter, frame % 40 == 0 ? 3.0f : 0.0f, 10,
                                                  frame % 40 == 0 ? 1.0f : -0.5f))
            return false;
        if (frame % 20 == 0 && !player.transition(eye_parameter, frame % 40 == 0 ? 30.0f : 5.0f, 10,
                                                  frame % 40 == 0 ? 1.0f : -1.0f))
            return false;
        if (frame % 20 == 10 && (!player.transition(parameter, 1.5f, 8, 1) ||
                                 !player.transition(eye_parameter, 24.04f, 8, -1)))
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
        const auto deformation = advance_spring(first, coefficients, {}, {0.1f, 0});
        observed = spring_output(deformation, output);
    }
    measure_allocations = false;
    REQUIRE(measured_allocations == 0);
    REQUIRE(std::isfinite(observed.x));
    REQUIRE(std::isfinite(observed.y));
    REQUIRE(second.position == std::array<float, 2>{});
    REQUIRE(second.velocity == std::array<float, 2>{});
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

TEST_CASE("Mirrored physical locators integrate in presented coordinates", "[runtime][physics]")
{
    using namespace runtime;
    auto asset = spring_rig();
    const auto check = [&](bool mirror)
    {
        asset.mirror = mirror;
        auto scene = compile_geometry_scene(asset);
        REQUIRE(scene.scene);
        auto program = compile_physics(asset, *scene.scene);
        REQUIRE(program.program);
        GeometryInstance geometry(*scene.scene);
        REQUIRE(geometry.set_origin({10, 20}));
        REQUIRE(geometry.set_scale(0.5f));
        PhysicsInstance physics(*program.program);
        REQUIRE(geometry.evaluate());
        REQUIRE(physics.step(geometry));
        const float sign = mirror ? -1.0f : 1.0f;
        REQUIRE(physics.observe_spring(0)->anchor.x == 10.0f + sign * 5.0f);
        REQUIRE(geometry.set_parameter(scene.scene->find_variable("pick"), 1));
        REQUIRE(geometry.evaluate());
        REQUIRE(physics.step(geometry));
        REQUIRE_THAT(
            physics.values()[0].value,
            Catch::Matchers::WithinAbs(static_cast<double>(sign) * -0.6467359066009521, 1e-5));
        // External author displacement keeps its direction, independent of
        // mirroring. Reinitialization discards the previous physical state.
        physics.reset();
        REQUIRE(physics.step(geometry));
        REQUIRE(physics.step(geometry, {1, {1, 0}}));
        REQUIRE_THAT(physics.values()[0].value,
                     Catch::Matchers::WithinAbs(-0.6467359066009521, 1e-5));
    };
    check(false);
    check(true);
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

TEST_CASE("Nonfinite physical state is transactional and requires reset", "[runtime]")
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
    // Float32 particle storage overflows before the output map. Changing the
    // amplitude cannot repair an integrator overflow or partially advance it.
    REQUIRE(physics.step(anchors, {0, {}}).error == PhysicsError::NonfiniteState);
    REQUIRE(physics.tick_count() == last_tick);
    physics.reset();
    REQUIRE(physics.step(anchors, {0, {}}));
    REQUIRE(physics.tick_count() == 1);
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

TEST_CASE("Interactive drag retains source inertia without accumulating screen travel",
          "[runtime][playback][pointer]")
{
    using namespace runtime;
    for (const bool mirror : {false, true})
        for (const float scale : {0.5f, 2.0f})
        {
            auto asset = interactive_rig();
            asset.mirror = mirror;
            auto compiled = compile_playback(asset);
            REQUIRE(compiled.program);
            PlaybackInstance dragged(*compiled.program), reference(*compiled.program);
            for (auto* player : {&dragged, &reference})
            {
                REQUIRE(player->set_origin({100, 400}));
                REQUIRE(player->set_scale(scale));
                REQUIRE(player->step());
            }
            const auto id = compiled.program->scene().find_variable("root");
            const auto value = [&] { return dragged.values()[id.value - 1].value; };
            REQUIRE(dragged.drag_origin({104, 400}));
            REQUIRE(dragged.advance(1.0 / 120));
            REQUIRE(dragged.prepare_frame());
            REQUIRE(dragged.tick_count() == 1);
            REQUIRE(dragged.drag_origin({120, 400}));
            REQUIRE(dragged.advance(1.0 / 120));
            REQUIRE(reference.step());
            REQUIRE(value() < -1.f); // first tick, before release
            for (int tick = 2; tick <= 120; ++tick)
            {
                REQUIRE(dragged.drag_origin({100.f + 20.f * static_cast<float>(tick), 400}));
                REQUIRE(dragged.step());
                REQUIRE(reference.step());
                near(dragged.observe_spring(0)->anchor, reference.observe_spring(0)->anchor);
                if (tick == 4)
                    REQUIRE(value() < -20.f);    // prompt excitation before release
                REQUIRE(std::isfinite(value())); // coupled chains may oscillate while held
            }
            const float held = value();
            REQUIRE(dragged.step());
            REQUIRE(reference.step());
            REQUIRE(value() > held); // stop impulse reacts on the next tick
            for (int tick = 0; tick < 600; ++tick)
            {
                REQUIRE(dragged.step());
                REQUIRE(reference.step());
            }
            for (std::size_t i = 0; i < dragged.values().size(); ++i)
                REQUIRE_THAT(dragged.values()[i].value,
                             Catch::Matchers::WithinAbs(reference.values()[i].value, .1f));
            REQUIRE(dragged.prepare_frame());
            REQUIRE(reference.prepare_frame());
            const auto a = dragged.frame().vertices, b = reference.frame().vertices;
            REQUIRE(a.size() == b.size());
            for (std::size_t i = 0; i < a.size(); ++i)
                near({a[i].x - (mirror ? -2400.f : 2400.f), a[i].y}, {b[i].x, b[i].y});
        }
}

TEST_CASE("Interactive drag input scales linearly with screen speed and saturates at 800",
          "[runtime][playback][pointer]")
{
    using namespace runtime;
    for (const bool mirror : {false, true})
        for (const float scale : {0.5f, 2.0f})
        {
            auto asset = interactive_rig();
            asset.mirror = mirror;
            auto compiled = compile_playback(asset);
            REQUIRE(compiled.program);
            for (const Vec2 direction : {Vec2{1, 0}, Vec2{0, -1}, Vec2{.6f, .8f}})
            {
                Vec2 full_impulse{};
                for (const float speed : {800.f, 1600.f, 400.f, 200.f, .01f, 0.f})
                {
                    PlaybackInstance player(*compiled.program), source(*compiled.program);
                    for (auto* p : {&player, &source})
                    {
                        REQUIRE(p->set_scale(scale));
                        REQUIRE(p->step());
                    }
                    REQUIRE(player.drag_origin(direction * (speed / 60.f)));
                    REQUIRE(player.step());
                    REQUIRE(source.step());
                    const auto a = player.observe_spring(0)->state.velocity;
                    const auto b = source.observe_spring(0)->state.velocity;
                    const Vec2 impulse{a[0] - b[0], a[1] - b[1]};
                    if (speed == 800.f)
                        full_impulse = impulse;
                    const float weight = std::min(speed / 800.f, 1.f);
                    near(impulse, full_impulse * weight);
                }
            }
            PlaybackInstance extreme(*compiled.program);
            REQUIRE(extreme.set_scale(scale));
            REQUIRE(extreme.step());
            const auto baseline = extreme.observe_spring(0)->anchor;
            const auto maximum = std::numeric_limits<float>::max();
            for (const float sign : {1.f, -1.f, 1.f})
            {
                REQUIRE(extreme.drag_origin({sign * maximum, sign * maximum}));
                REQUIRE(extreme.advance(1.0 / 30));
                near(extreme.observe_spring(0)->anchor, baseline);
                for (const auto parameter : extreme.values())
                    REQUIRE(std::isfinite(parameter.value));
            }
        }
    auto unbound = rig();
    unbound.author_scale = field(std::numeric_limits<float>::quiet_NaN());
    auto compiled = compile_playback(unbound);
    REQUIRE(compiled.program);
    PlaybackInstance player(*compiled.program);
    REQUIRE(player.step());
    REQUIRE(player.drag_origin({10, 20}));
    REQUIRE(player.step());
    REQUIRE(player.prepare_frame());
}

TEST_CASE("Interactive drag releases into a large coupled rebound and decays",
          "[runtime][playback][pointer]")
{
    using namespace runtime;
    auto compiled = compile_playback(interactive_rig());
    REQUIRE(compiled.program);
    PlaybackInstance player(*compiled.program);
    const auto root = compiled.program->scene().find_variable("root");
    const auto tip = compiled.program->scene().find_variable("tip");
    const auto value = [&](ParamId id) { return player.values()[id.value - 1].value; };
    REQUIRE(player.step());
    for (int tick = 1; tick <= 60; ++tick)
    {
        REQUIRE(player.drag_origin({static_cast<float>(tick) * (800.f / 60.f), 0}));
        REQUIRE(player.step());
    }
    REQUIRE(value(root) < -25.f);
    const float held = value(root);
    REQUIRE(player.step());
    REQUIRE(value(root) > held);
    float root_peak = 0, tip_peak = 0;
    bool different_phase = false;
    for (int tick = 0; tick < 120; ++tick)
    {
        REQUIRE(player.step());
        root_peak = std::max(root_peak, value(root));
        tip_peak = std::max(tip_peak, value(tip));
        different_phase |= value(root) * value(tip) < -1.f;
    }
    REQUIRE(root_peak > 25.f);
    REQUIRE(tip_peak > 20.f);
    REQUIRE(different_phase);
    for (int tick = 0; tick < 600; ++tick)
        REQUIRE(player.step());
    REQUIRE(std::abs(value(root)) < .1f);
    REQUIRE(std::abs(value(tip)) < .1f);
}

TEST_CASE("Interactive placement distributes motion over catchup ticks", "[runtime][playback]")
{
    using namespace runtime;
    auto asset = interactive_rig();
    auto compiled = compile_playback(asset);
    REQUIRE(compiled.program);
    PlaybackInstance at30(*compiled.program), at60(*compiled.program);
    REQUIRE(at30.step());
    REQUIRE(at60.step());
    for (int frame = 1; frame <= 60; ++frame)
    {
        REQUIRE(at30.drag_origin({static_cast<float>(frame * 20), 0}));
        REQUIRE(at30.advance(1.0 / 30));
        REQUIRE(at60.drag_origin({static_cast<float>(frame * 20 - 10), 0}));
        REQUIRE(at60.advance(1.0 / 60));
        REQUIRE(at60.drag_origin({static_cast<float>(frame * 20), 0}));
        REQUIRE(at60.advance(1.0 / 60));
        for (std::size_t i = 0; i < at30.values().size(); ++i)
            REQUIRE(at30.values()[i].value == at60.values()[i].value);
    }
}

TEST_CASE("Pausing an active drag rebases input without injecting a resume impulse",
          "[runtime][playback][pointer]")
{
    using namespace runtime;
    auto compiled = compile_playback(interactive_rig());
    REQUIRE(compiled.program);
    PlaybackInstance paused(*compiled.program), reference(*compiled.program);
    for (auto* player : {&paused, &reference})
    {
        REQUIRE(player->step());
        for (int tick = 1; tick <= 30; ++tick)
        {
            REQUIRE(player->drag_origin({static_cast<float>(tick * 20), 0}));
            REQUIRE(player->step());
        }
    }
    paused.set_playback_state(PlaybackState::Paused);
    REQUIRE(paused.drag_origin({4000, 3000}));
    paused.set_playback_state(PlaybackState::Running);
    REQUIRE(reference.set_origin({4000, 3000}));
    for (int tick = 0; tick < 120; ++tick)
    {
        REQUIRE(paused.step());
        REQUIRE(reference.step());
        for (std::size_t i = 0; i < paused.values().size(); ++i)
            REQUIRE(paused.values()[i].value == reference.values()[i].value);
    }
}

TEST_CASE("Interactive placement handles initialization, reconfiguration and failed ticks",
          "[runtime][playback][pointer]")
{
    using namespace runtime;
    auto asset = interactive_rig();
    auto compiled = compile_playback(asset);
    REQUIRE(compiled.program);
    PlaybackInstance dragged(*compiled.program), reference(*compiled.program);
    REQUIRE(dragged.set_origin({100, 400}));
    REQUIRE(reference.set_origin({100, 400}));
    REQUIRE(dragged.drag_origin({120, 400})); // before the first physics tick
    const auto compare = [&]
    {
        for (std::size_t i = 0; i < dragged.values().size(); ++i)
            REQUIRE(dragged.values()[i].value == reference.values()[i].value);
    };
    for (int tick = 0; tick < 3; ++tick)
    {
        REQUIRE(dragged.step());
        REQUIRE(reference.step());
        compare(); // initialization cannot leave an opposite velocity pulse
    }
    REQUIRE(dragged.drag_origin({140, 410}));
    REQUIRE(dragged.set_origin({900, 800})); // explicit placement drops pending input
    REQUIRE(dragged.set_scale(2));
    REQUIRE(reference.set_scale(2));
    REQUIRE(dragged.step());
    REQUIRE(reference.step());
    compare();
    REQUIRE(dragged.prepare_frame());
    REQUIRE(dragged.drag_origin({920, 810}));
    PhysicsInputs invalid;
    invalid.bust_amplitude = std::numeric_limits<float>::quiet_NaN();
    const auto failed = dragged.step(invalid);
    REQUIRE(failed.error == PlaybackError::Physics);
    REQUIRE(failed.physics.error == PhysicsError::InvalidInput);
    REQUIRE(dragged.frame().vertices.empty());
    dragged.reset();
    reference.reset();
    REQUIRE(reference.set_origin({920, 810}));
    REQUIRE(dragged.step());
    REQUIRE(reference.step());
    REQUIRE(dragged.prepare_frame());
    REQUIRE(reference.prepare_frame());
    compare();
    REQUIRE(dragged.frame().vertices.size() == reference.frame().vertices.size());
    for (std::size_t i = 0; i < dragged.frame().vertices.size(); ++i)
        near({dragged.frame().vertices[i].x, dragged.frame().vertices[i].y},
             {reference.frame().vertices[i].x, reference.frame().vertices[i].y});
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
    auto asset = interactive_rig();
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
        success &= static_cast<bool>(player.drag_origin({static_cast<float>(i), 400}));
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
    advance_spring(isolated, {0.5f, 0.125f}, {}, {});
    const auto deformation = advance_spring(
        isolated, {0.5f, 0.125f},
        {reference.at("delta")[0].get<float>(), reference.at("delta")[1].get<float>()}, {});
    const auto expected = spring_output(deformation, {{1, 2}, 0, 1});
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
    for (const auto& [mode, expected] :
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
    for (const auto& dimensions :
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

TEST_CASE("Referenced motions sharing a stencil retain independent content clips",
          "[runtime][scene]")
{
    auto asset = rig();
    asset.parts[1].layers.push_back(layer("small highlight", 0, sprite()));
    FrameContent blank;
    blank.src = field(std::string{"blank"});
    blank.icon = field(std::string{"32:32:16:16"});
    auto mask = layer("mask", 12, blank);
    mask.inherit_mask = 0x24007fc;
    mask.stencil_type = 5;
    mask.stencil_composite_mask_layers = {"source"};
    FrameContent reference;
    reference.src = field(std::string{"rig"});
    reference.icon = field(std::string{"A"});
    mask.children.push_back(layer("pupil", 3, reference));
    reference.icon = field(std::string{"B"});
    mask.children.push_back(layer("separate highlight", 3, reference));
    asset.parts[0].layers = {layer("source", 0, sprite()), mask};
    auto compiled = runtime::compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    runtime::GeometryInstance instance(*compiled.scene);
    REQUIRE(instance.evaluate());
    REQUIRE(instance.build_frame());
    std::vector<MaskId> masked;
    for (const auto& command : instance.frame().commands)
        if (command.mask.valid())
            masked.push_back(command.mask);
    REQUIRE(masked.size() == 3);
    REQUIRE(masked[0] == masked[1]);
    REQUIRE(masked[0] != masked[2]);
    REQUIRE(instance.frame().mask_commands.size() == 2);
}

TEST_CASE("Blank mask sources produce empty coverage while retaining their visible target",
          "[runtime][scene]")
{
    auto asset = rig();
    asset.parts.resize(1);
    FrameContent blank;
    blank.src = field(std::string{"blank"});
    blank.icon = field(std::string{"32:32:16:16"});
    auto source = layer("blank source", 0, blank);
    auto mask = layer("mask", 12, blank);
    mask.inherit_mask = 0x24007fc;
    mask.stencil_type = 5;
    mask.stencil_composite_mask_layers = {"blank source"};
    mask.children = {layer("target", 0, sprite())};
    asset.parts[0].layers = {source, mask};
    auto compiled = runtime::compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    runtime::GeometryInstance instance(*compiled.scene);
    REQUIRE(instance.evaluate());
    REQUIRE(instance.build_frame());
    const auto frame = instance.frame();
    REQUIRE(frame.commands.size() == 1);
    REQUIRE(frame.commands[0].mask == MaskId{1});
    REQUIRE(frame.mask_commands.empty());
    REQUIRE(frame.masks.size() == 1);
    REQUIRE(frame.masks[0].command_count == 0);
}

TEST_CASE("Selector state is independent, resettable and allocation free in playback",
          "[runtime][playback]")
{
    auto asset = rig();
    asset.variables = {{"choice", {}}, {"first_option", {}}, {"second_option", {}}};
    VariableControl first, second, selector;
    first.variable = "first_option";
    second.variable = "second_option";
    selector.kind = VariableControlKind::Selector;
    selector.variable = "choice";
    selector.options = {{"first_option", 0, 1}, {"second_option", 0, 1}};
    asset.variable_controls = {first, second, selector};
    auto compiled = runtime::compile_playback(asset);
    REQUIRE(compiled.program);
    const auto choice = compiled.program->scene().find_variable("choice");
    const auto option = compiled.program->scene().find_variable("second_option");
    runtime::PlaybackInstance player(*compiled.program), independent(*compiled.program);
    REQUIRE(player.transition(choice, 1.5f, 10));
    REQUIRE(player.step());
    REQUIRE(player.values()[choice.value - 1].value == 1.0f);
    REQUIRE(player.values()[option.value - 1].value == 0.8999999761581421f);
    REQUIRE(independent.tick_count() == 0);
    REQUIRE(independent.values()[option.value - 1].value == 0.0f);
    player.reset();
    REQUIRE(player.values()[option.value - 1].value == 0.0f);
    REQUIRE(player.step());
    REQUIRE(player.values()[option.value - 1].value == 1.0f);
    for (int i = 0; i < 120; ++i)
    {
        REQUIRE(player.step());
        REQUIRE(player.prepare_frame());
    }
    measured_allocations = 0;
    measure_allocations = true;
    bool success = true;
    for (int i = 0; i < 200; ++i)
    {
        success &= static_cast<bool>(player.transition(choice, static_cast<float>(i % 2), 10, 1));
        success &= static_cast<bool>(player.step());
        success &= static_cast<bool>(player.prepare_frame());
    }
    measure_allocations = false;
    REQUIRE(success);
    REQUIRE(measured_allocations == 0);
    REQUIRE(independent.tick_count() == 0);
    REQUIRE(independent.values()[option.value - 1].value == 0.0f);

    SECTION("Repeated output assignment is rejected")
    {
        asset.variable_controls.back().options.push_back(selector.options[0]);
        REQUIRE_FALSE(runtime::compile_playback(asset).program);
    }
    SECTION("Selector output cycles are rejected")
    {
        asset.variable_controls.back().options[0].variable = "choice";
        REQUIRE_FALSE(runtime::compile_playback(asset).program);
    }
    SECTION("Unverified on/off ranges are rejected")
    {
        asset.variable_controls.back().options[0].on_value = 0.5f;
        REQUIRE_FALSE(runtime::compile_playback(asset).program);
    }
}

TEST_CASE(
    "Weighted difference playback remains independent and allocation free through pause and replay",
    "[runtime][playback][allocation]")
{
    auto asset = rig();
    VariableControl control;
    control.variable = "pick";
    control.kind = VariableControlKind::Linear;
    asset.variable_controls = {control};
    Timeline clip;
    clip.label = "difference";
    clip.loop_begin = 0;
    clip.loop_end = 30;
    clip.tracks = {
        {"pick",
         {{0, 2, field(1.0f), field(0.0f)}, {15, 2, field(0.0f), field(0.0f)}, {30, 0, {}, {}}}}};
    asset.timelines = {clip};
    auto compiled = runtime::compile_playback(asset);
    REQUIRE(compiled.program);
    const auto id = compiled.program->animation().find_timeline("difference");
    runtime::PlaybackInstance player(*compiled.program), other(*compiled.program);
    REQUIRE(player.play(id, runtime::TimelinePlayMode::AdditiveParallel));
    REQUIRE(player.set_timeline_weight(id, 0, 10, 1));
    for (int i = 0; i < 4; ++i)
        REQUIRE(player.step());
    const float saved = player.timeline_weight(id);
    player.set_playback_state(runtime::PlaybackState::Paused);
    REQUIRE(player.advance(20));
    REQUIRE(player.step());
    REQUIRE(player.timeline_weight(id) == saved);
    REQUIRE(player.tick_count() == 4);
    REQUIRE(other.timeline_weight(id) == 1.0f);
    REQUIRE_FALSE(other.is_playing(id));
    REQUIRE_FALSE(player.set_timeline_weight({}, 0));
    REQUIRE_FALSE(player.set_timeline_weight(id, std::numeric_limits<float>::quiet_NaN()));
    REQUIRE_FALSE(player.set_timeline_weight(id, 0, -1));
    REQUIRE(player.timeline_weight(id) == saved);
    player.set_playback_state(runtime::PlaybackState::Running);
    player.reset();
    REQUIRE(player.timeline_weight(id) == 1.0f);
    REQUIRE_FALSE(player.is_playing(id));
    for (int i = 0; i < 120; ++i)
        REQUIRE(player.step());
    measured_allocations = 0;
    measure_allocations = true;
    bool success = true;
    for (int i = 0; i < 500; ++i)
    {
        if (i % 13 == 0)
            success &=
                static_cast<bool>(player.play(id, runtime::TimelinePlayMode::AdditiveParallel));
        if (i % 7 == 0)
            success &= static_cast<bool>(player.fade_out(id, 6, -1));
        if (i % 9 == 0)
            success &= static_cast<bool>(player.fade_in(id, 10, 1));
        success &= static_cast<bool>(player.step());
        success &= static_cast<bool>(player.prepare_frame());
    }
    measure_allocations = false;
    REQUIRE(success);
    REQUIRE(measured_allocations == 0);
    REQUIRE(other.tick_count() == 0);
}

TEST_CASE("Weighted difference accumulation preserves observed transient products",
          "[runtime][animation]")
{
    auto asset = rig();
    VariableControl control;
    control.variable = "pick";
    control.kind = VariableControlKind::Linear;
    asset.variable_controls = {control};
    for (const auto& [label, target] :
         {std::pair{"first", 0.12857143580913544f}, std::pair{"second", -0.9574467539787292f}})
    {
        Timeline clip;
        clip.label = label;
        clip.tracks = {{"pick", {{0, 2, field(target), field(0.0f)}, {2, 0, {}, {}}}}};
        asset.timelines.push_back(clip);
    }
    auto compiled = runtime::compile_playback(asset);
    REQUIRE(compiled.program);
    runtime::PlaybackInstance player(*compiled.program);
    const auto first = compiled.program->animation().find_timeline("first");
    const auto second = compiled.program->animation().find_timeline("second");
    REQUIRE(player.play(first, runtime::TimelinePlayMode::AdditiveParallel));
    REQUIRE(player.play(second, runtime::TimelinePlayMode::AdditiveParallel));
    REQUIRE(player.set_timeline_weight(first, 0.6000000238418579f));
    REQUIRE(player.set_timeline_weight(second, 0.30000001192092896f));
    REQUIRE(player.step());
    const auto pick = compiled.program->scene().find_variable("pick");
    REQUIRE(player.values()[pick.value - 1].value == -0.21009117364883423f);
}

TEST_CASE("External contributions preserve source activation order before the base channel",
          "[runtime][animation]")
{
    auto asset = rig();
    const std::array<runtime::VariableSourceDefinition, 3> sources{
        runtime::VariableSourceDefinition{"alpha", {"pick"}},
        runtime::VariableSourceDefinition{"beta", {"pick"}},
        runtime::VariableSourceDefinition{"zeta", {"pick"}}};
    auto compiled = runtime::compile_playback(asset, runtime::PhysicsMode::Enabled, sources);
    REQUIRE(compiled.program);
    const auto parameter = compiled.program->scene().find_variable("pick");
    const auto alpha = compiled.program->animation().find_variable_source("alpha");
    const auto beta = compiled.program->animation().find_variable_source("beta");
    const auto zeta = compiled.program->animation().find_variable_source("zeta");
    runtime::PlaybackInstance player(*compiled.program);
    REQUIRE(player.transition_difference(alpha, parameter, 1));
    REQUIRE(player.transition_difference(beta, parameter, 16777216));
    REQUIRE(player.transition_difference(zeta, parameter, -16777216));
    REQUIRE(player.values()[parameter.value - 1].value == 1.0f);
    player.reset();
    REQUIRE(player.transition_difference(zeta, parameter, -16777216));
    REQUIRE(player.transition_difference(beta, parameter, 16777216));
    REQUIRE(player.transition_difference(alpha, parameter, 1));
    REQUIRE(player.values()[parameter.value - 1].value == 0.0f);
    // Updating a source does not change its first activation priority.
    REQUIRE(player.transition_difference(zeta, parameter, -16777216));
    REQUIRE(player.values()[parameter.value - 1].value == 0.0f);
    player.reset();
    REQUIRE(player.transition(parameter, 1));
    REQUIRE(player.transition_difference(alpha, parameter, 16777216));
    REQUIRE(player.transition_difference(zeta, parameter, -16777216));
    REQUIRE(player.values()[parameter.value - 1].value == 1.0f);
    player.reset();
    REQUIRE(player.transition(parameter, 1));
    REQUIRE(player.transition_difference(zeta, parameter, -16777216));
    REQUIRE(player.transition_difference(alpha, parameter, 16777216));
    REQUIRE(player.values()[parameter.value - 1].value == 1.0f);
}

TEST_CASE(
    "External channels validate bindings and reuse storage during paused independent playback",
    "[runtime][playback][allocation]")
{
    auto asset = rig();
    const std::array<runtime::VariableSourceDefinition, 2> sources{
        runtime::VariableSourceDefinition{"gaze", {"pick"}},
        runtime::VariableSourceDefinition{"voice", {"pick"}}};
    auto compiled = runtime::compile_playback(asset, runtime::PhysicsMode::Enabled, sources);
    REQUIRE(compiled.program);
    const auto parameter = compiled.program->scene().find_variable("pick");
    const auto gaze = compiled.program->animation().find_variable_source("gaze");
    const auto voice = compiled.program->animation().find_variable_source("voice");
    runtime::PlaybackInstance player(*compiled.program), other(*compiled.program);
    REQUIRE(player.transition_difference(gaze, parameter, 1, 10, -1));
    REQUIRE(player.transition_difference(voice, parameter, -0.25f));
    REQUIRE(player.step());
    const float saved = player.difference_value(gaze, parameter);
    player.set_playback_state(runtime::PlaybackState::Paused);
    REQUIRE(player.advance(1));
    REQUIRE(player.step());
    REQUIRE(player.difference_value(gaze, parameter) == saved);
    REQUIRE(other.difference_value(gaze, parameter) == 0.0f);
    REQUIRE_FALSE(player.transition_difference({}, parameter, 1));
    REQUIRE_FALSE(player.transition_difference(gaze, {}, 1));
    REQUIRE_FALSE(player.transition_difference(gaze, parameter, 1, -1));
    REQUIRE_FALSE(
        player.transition_difference(gaze, parameter, std::numeric_limits<float>::quiet_NaN()));
    REQUIRE(player.difference_value(gaze, parameter) == saved);
    player.reset();
    REQUIRE(player.difference_value(gaze, parameter) == 0.0f);
    REQUIRE(player.difference_value(voice, parameter) == 0.0f);
    player.set_playback_state(runtime::PlaybackState::Running);
    measured_allocations = 0;
    measure_allocations = true;
    bool success = true;
    for (int i = 0; i < 500; ++i)
    {
        success &= static_cast<bool>(player.transition_difference(gaze, parameter, 0.5f, 30, -1));
        success &=
            static_cast<bool>(player.transition_difference(voice, parameter, -0.25f, 4.2f, -1));
        success &= static_cast<bool>(player.step());
        success &= static_cast<bool>(player.prepare_frame());
    }
    measure_allocations = false;
    REQUIRE(success);
    REQUIRE(measured_allocations == 0);
    REQUIRE(other.tick_count() == 0);
    REQUIRE(other.difference_value(voice, parameter) == 0.0f);
    auto bad = sources;
    bad[1].label = "gaze";
    REQUIRE_FALSE(runtime::compile_playback(asset, runtime::PhysicsMode::Enabled, bad).program);
    bad = sources;
    bad[1].variables = {"missing"};
    REQUIRE_FALSE(runtime::compile_playback(asset, runtime::PhysicsMode::Enabled, bad).program);
    bad[1].variables = {"pick", "pick"};
    REQUIRE_FALSE(runtime::compile_playback(asset, runtime::PhysicsMode::Enabled, bad).program);
}

TEST_CASE("Mixed selector and numeric difference tracks stay allocation free through fades",
          "[runtime][playback][allocation]")
{
    auto asset = rig();
    asset.variables = {{"offset", {}}, {"left", {}}, {"right", {}}};
    VariableControl selector;
    selector.variable = "pick";
    selector.kind = VariableControlKind::Selector;
    selector.options = {{"left", 0, 1}, {"right", 0, 1}};
    asset.variable_controls.push_back(selector);
    for (const auto* name : {"offset", "left", "right"})
    {
        VariableControl control;
        control.variable = name;
        control.kind = VariableControlKind::Linear;
        asset.variable_controls.push_back(control);
    }
    Timeline clip;
    clip.label = "mixed";
    clip.loop_begin = 0;
    clip.loop_end = 30;
    clip.tracks = {
        {"pick",
         {{0, 2, field(1.0f), field(0.0f)}, {15, 2, field(0.0f), field(0.0f)}, {30, 0, {}, {}}}},
        {"offset",
         {{0, 2, field(3.0f), field(0.0f)}, {15, 2, field(0.0f), field(0.0f)}, {30, 0, {}, {}}}}};
    asset.timelines.push_back(clip);
    auto compiled = runtime::compile_playback(asset);
    REQUIRE(compiled.program);
    runtime::PlaybackInstance player(*compiled.program);
    const auto id = compiled.program->animation().find_timeline("mixed");
    REQUIRE(player.play(id, runtime::TimelinePlayMode::AdditiveParallel));
    REQUIRE(player.set_timeline_weight(id, 0));
    REQUIRE(player.step());
    const auto pick = compiled.program->scene().find_variable("pick");
    const auto offset = compiled.program->scene().find_variable("offset");
    REQUIRE(player.values()[pick.value - 1].value == 1.0f);
    REQUIRE(player.values()[offset.value - 1].value == 0.0f);
    measured_allocations = 0;
    measure_allocations = true;
    bool success = true;
    for (int i = 0; i < 500; ++i)
    {
        if (i % 17 == 0)
            success &=
                static_cast<bool>(player.play(id, runtime::TimelinePlayMode::AdditiveParallel));
        if (i % 7 == 0)
            success &= static_cast<bool>(player.fade_out(id, 6, -1));
        if (i % 11 == 0)
            success &= static_cast<bool>(player.fade_in(id, 10, 1));
        success &= static_cast<bool>(player.step());
        success &= static_cast<bool>(player.prepare_frame());
    }
    measure_allocations = false;
    REQUIRE(success);
    REQUIRE(measured_allocations == 0);
}

TEST_CASE("Application gaze, reaction and lip scheduling reuse storage in normal playback",
          "[application][allocation]")
{
    auto asset = rig();
    for (const auto* name : {"face_eye_LR", "face_eye_UD", "head_slant", "head_LR", "head_UD",
                             "body_slant", "body_LR", "body_UD", "face_talk"})
    {
        asset.variables.push_back({name, {}});
        VariableControl control;
        control.variable = name;
        control.kind = VariableControlKind::Linear;
        asset.variable_controls.push_back(control);
    }
    SessionAsset definition;
    definition.interaction.eye = 0;
    definition.interaction.regions[static_cast<std::size_t>(TouchZone::Head)] =
        TouchEllipse{{0, 0}, {20, 20}};
    ReactionAsset reaction;
    reaction.label = "touch";
    reaction.zones = {TouchZone::Head};
    reaction.action.variables = {{"pick", 1, 5, 0}};
    reaction.recovery.variables = {{"pick", 0, 5, 0}};
    reaction.duration_ticks = 15;
    reaction.recovery_ticks = 10;
    reaction.audio_path = "voice.wav";
    definition.reactions.push_back(reaction);
    auto compiled = application::compile_session(asset, definition);
    REQUIRE(compiled.program);
    application::SessionInstance player(
        *compiled.program,
        {{1}, 1, application::PointerScope::LocalSurface, application::AudioMode::Device});
    REQUIRE(player.start());
    measured_allocations = 0;
    measure_allocations = true;
    bool success = true;
    for (int i = 0; i < 500; ++i)
    {
        if (player.reaction_phase() == application::ReactionPhase::Idle)
            success &= static_cast<bool>(player.touch({0, 0}));
        success &= static_cast<bool>(player.pointer_move({i % 2 == 0 ? 150.0f : -150.0f, 20}));
        success &= static_cast<bool>(player.set_audio_level(.1f));
        success &= static_cast<bool>(player.advance(1.0 / 30));
        success &= static_cast<bool>(player.prepare_frame());
        while (player.pop_audio_command())
        {
        }
    }
    measure_allocations = false;
    REQUIRE(success);
    REQUIRE(measured_allocations == 0);
    REQUIRE(player.playback().tick_count() == std::uint64_t{1000});
}

TEST_CASE("Tessellation retains local doubles until the observed world Float32 boundary",
          "[runtime][scene]")
{
    using namespace runtime;
    std::ifstream file(std::string(NEKOMOTE_FIXTURES_DIR) +
                       "/tessellation_precision_reference.json");
    const auto r = nlohmann::json::parse(file);
    auto asset = rig();
    asset.parts.resize(1);
    const auto sprite_size = r.at("sprite_size").get<std::array<float, 2>>();
    const auto origin = r.at("sprite_origin").get<std::array<float, 2>>();
    asset.textures[0].width = 4096;
    asset.textures[0].height = 2048;
    asset.icons["atlas/a"] = {"atlas",        1514,      1,        sprite_size[0],
                              sprite_size[1], origin[0], origin[1]};
    auto child = layer("sprite", 0, sprite());
    child.mesh_division = 20;
    child.obj_tri_priority = 2;
    std::array<float, 32> controls{};
    for (std::size_t i = 0; i < 16; ++i)
    {
        controls[i * 2] = r.at("unit_patch")[i][0].get<float>();
        controls[i * 2 + 1] = r.at("unit_patch")[i][1].get<float>();
    }
    FrameContent content;
    content.src = field(std::string{"blank"});
    content.icon = field(std::string{"496:1730:248:865"});
    const auto world = r.at("world").get<std::array<float, 6>>();
    content.coord = field(std::array<float, 3>{world[4], world[5], 0});
    MeshData mesh;
    mesh.bp = field(controls);
    content.mesh = field(mesh);
    auto parent = layer("domain", 0, content);
    parent.mesh_sync_child_mask = 8;
    parent.children = {child};
    asset.parts[0].layers = {parent};
    auto compiled = compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    GeometryInstance instance(*compiled.scene);
    REQUIRE_FALSE(instance.observe_node(NodeId{1}));
    REQUIRE(instance.evaluate());
    REQUIRE(instance.build_frame());
    const auto frame = instance.frame();
    REQUIRE(frame.vertices.size() == r.at("expected").size());
    for (std::size_t i = 0; i < frame.vertices.size(); ++i)
    {
        INFO(i);
        REQUIRE(frame.vertices[i].x == r.at("expected")[i][0].get<float>());
        REQUIRE(frame.vertices[i].y == r.at("expected")[i][1].get<float>());
        REQUIRE(frame.vertices[i].u == r.at("expected_uv")[i][0].get<float>());
        REQUIRE(frame.vertices[i].v == r.at("expected_uv")[i][1].get<float>());
    }
    REQUIRE_FALSE(instance.observe_node({}));
    const auto observed = instance.observe_node(NodeId{1});
    REQUIRE(observed);
    REQUIRE(observed->mesh);
    for (std::size_t i = 0; i < observed->world_patch.size(); ++i)
    {
        REQUIRE(observed->world_patch[i].x == r.at("world_patch")[i][0].get<float>());
        REQUIRE(observed->world_patch[i].y == r.at("world_patch")[i][1].get<float>());
    }
    REQUIRE(instance.set_origin({10, 20}));
    REQUIRE_FALSE(instance.observe_node(NodeId{1}));
    REQUIRE(instance.evaluate());
    REQUIRE(instance.observe_node(NodeId{1}));
    // Copies remain valid independently of the next instance mutation.
    REQUIRE(observed->world[4] == world[4]);
}

TEST_CASE("Paired clamp preserves transient values until each binding time is saved", "[runtime]")
{
    std::ifstream input(std::string(NEKOMOTE_FIXTURES_DIR) + "/paired_binding_reference.json");
    REQUIRE(input.good());
    const auto reference = nlohmann::json::parse(input);
    auto asset = spring_rig();
    asset.spring_controls.clear();
    asset.variables = {{"body_x", {}}, {"body_y", {}}};
    asset.paired_clamps = {{"body", "body_x", "body_y", -30, 30, 0, true}};
    FrameContent content;
    content.coord = field(std::array<float, 3>{0, 0, 0});
    auto marker = layer("paired marker", 1, content);
    marker.export_self = 1;
    marker.parameterize = field(0);
    marker.parameter = {{"body_y", -30, 30, 60, 0, true}};
    marker.frame_list[0].type = 3;
    content.coord = field(std::array<float, 3>{60, 0, 0});
    marker.frame_list.push_back({60, 3, field(content)});
    asset.parts[0].layers = {marker};
    const auto compiled = runtime::compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    runtime::GeometryInstance instance(*compiled.scene);
    REQUIRE(instance.set_parameter(compiled.scene->find_variable("body_x"),
                                   reference.at("input")[0].get<float>()));
    REQUIRE(instance.set_parameter(compiled.scene->find_variable("body_y"),
                                   reference.at("input")[1].get<float>()));
    REQUIRE(instance.evaluate());
    const auto sample = instance.sample_locator(compiled.scene->locators()[0].node);
    REQUIRE(sample.status);
    REQUIRE(sample.position.x == reference.at("binding_stores")[0].at("stored").get<float>());
}

TEST_CASE("Saved world corners and propagated identity surfaces match submitted reference grids",
          "[runtime][scene]")
{
    std::ifstream input(std::string(NEKOMOTE_FIXTURES_DIR) + "/rigid_grid_reference.json");
    const auto reference = nlohmann::json::parse(input);
    auto asset = rig();
    asset.parts.resize(1);
    asset.textures[0].width = 4096;
    asset.textures[0].height = 2048;
    const auto size = reference.at("sprite_size").get<std::array<float, 2>>();
    const auto origin = reference.at("sprite_origin").get<std::array<float, 2>>();
    asset.icons["atlas/a"] = {"atlas", 800, 1, size[0], size[1], origin[0], origin[1]};
    auto child_content = sprite();
    const auto world = reference.at("world").get<std::array<float, 6>>();
    child_content.coord = field(std::array<float, 3>{world[4], world[5], 0});
    child_content.angle = field(reference.at("angle_degrees").get<float>());
    auto child = layer("sprite", 0, child_content);
    child.mesh_division = 16;
    FrameContent content;
    content.src = field(std::string{"blank"});
    content.icon = field(std::string{"100:100:0:0"});
    MeshData mesh;
    std::array<float, 32> controls{};
    const auto identity = runtime::identity_patch();
    for (std::size_t i = 0; i < identity.size(); ++i)
    {
        controls[2 * i] = identity[i].x;
        controls[2 * i + 1] = identity[i].y;
    }
    mesh.bp = field(controls);
    content.mesh = field(mesh);
    auto parent = layer("mesh_path", 0, content);
    parent.mesh_sync_child_mask = 8;
    parent.children = {child};
    asset.parts[0].layers = {parent};
    auto compiled = runtime::compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    runtime::GeometryInstance instance(*compiled.scene);
    REQUIRE(instance.evaluate());
    REQUIRE(instance.build_frame());
    const auto vertices = instance.frame().vertices;
    REQUIRE(vertices.size() == reference.at("expected").size());
    const auto observed = instance.observe_node(instance.primitives()[0].node);
    REQUIRE(observed);
    REQUIRE(observed->world == world);
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        INFO(i);
        REQUIRE_THAT(vertices[i].x, Catch::Matchers::WithinAbs(
                                        reference.at("expected")[i][0].get<float>(), 0.001));
        REQUIRE_THAT(vertices[i].y, Catch::Matchers::WithinAbs(
                                        reference.at("expected")[i][1].get<float>(), 0.001));
    }
}

TEST_CASE("Single shape exclusion preserves identity curves and rejects unknown masks",
          "[runtime][scene]")
{
    auto build_rig = [](std::uint64_t reference_mask, int child_sync = 0)
    {
        auto asset = rig();
        asset.parts.resize(1);
        asset.textures[0].width = 64;
        asset.textures[0].height = 32;
        asset.icons["atlas/a"] = {"atlas", 0, 0, 8, 4, 3, 1};
        auto child_content = sprite();
        auto child = layer("sprite", 0, child_content);
        child.mesh_division = 4;
        FrameContent reference_content;
        reference_content.src = field(std::string{"blank"});
        reference_content.icon = field(std::string{"100:100:0:0"});
        auto reference_node = layer("ignore", 2, reference_content);
        reference_node.inherit_mask = reference_mask;
        reference_node.mesh_division = 0;
        reference_node.children = {child};
        FrameContent parent_content;
        parent_content.src = field(std::string{"blank"});
        parent_content.icon = field(std::string{"100:100:0:0"});
        MeshData mesh;
        std::array<float, 32> controls{};
        const auto identity = runtime::identity_patch();
        for (std::size_t i = 0; i < identity.size(); ++i)
        {
            controls[2 * i] = identity[i].x;
            controls[2 * i + 1] = identity[i].y;
        }
        mesh.bp = field(controls);
        parent_content.mesh = field(mesh);
        auto parent = layer("mesh_parent", 0, parent_content);
        parent.mesh_division = 4;
        parent.mesh_sync_child_mask = child_sync;
        parent.children = {reference_node};
        asset.parts[0].layers = {parent};
        return asset;
    };
    for (const int child_sync : {0, 8})
    {
        auto compiled = runtime::compile_geometry_scene(build_rig(0x7fc, child_sync));
        REQUIRE(compiled.scene);
        runtime::GeometryInstance instance(*compiled.scene);
        REQUIRE(instance.evaluate());
        REQUIRE(instance.build_frame());
        near(instance.sample_atlas_point(instance.primitives()[0].node, {3, 1}), {0, 0});
        near(instance.sample_atlas_point(instance.primitives()[0].node, {3, 3}), {0, 2});
    }
    {
        auto compiled = runtime::compile_geometry_scene(build_rig(0x7fd, 8));
        REQUIRE(compiled.scene);
        runtime::GeometryInstance instance(*compiled.scene);
        REQUIRE(instance.evaluate().error == runtime::GeometryError::UnsupportedTransform);
        REQUIRE(instance.frame().vertices.empty());
        REQUIRE_FALSE(instance.build_frame());
    }
    // inheritShape=1: subtree inherits mesh_path, child renders a tessellated grid.
    {
        auto compiled = runtime::compile_geometry_scene(build_rig(0x20007fc, 8));
        REQUIRE(compiled.scene);
        runtime::GeometryInstance instance(*compiled.scene);
        REQUIRE(instance.evaluate());
        REQUIRE(instance.build_frame());
        REQUIRE(instance.frame().vertices.size() == 16);
    }
}

TEST_CASE("Verified source multiply is explicit and unknown blend modes fail", "[runtime][scene]")
{
    auto build_bm_rig = [](int bm_value)
    {
        auto asset = rig();
        asset.parts.resize(1);
        asset.textures[0].width = 64;
        asset.textures[0].height = 32;
        asset.icons["atlas/a"] = {"atlas", 0, 0, 8, 4, 3, 1};
        asset.icons["atlas/a"].attr = 1;
        auto content = sprite();
        content.bm = field(bm_value);
        auto sprite_layer = layer("bm_sprite", 0, content);
        FrameContent root_content;
        root_content.src = field(std::string{"blank"});
        root_content.icon = field(std::string{"100:100:0:0"});
        auto root = layer("root", 0, root_content);
        root.children = {sprite_layer};
        asset.parts[0].layers = {root};
        return asset;
    };
    for (int bm : {0, 16, 19, 20})
    {
        auto compiled = runtime::compile_geometry_scene(build_bm_rig(bm));
        REQUIRE(compiled.scene);
        runtime::GeometryInstance instance(*compiled.scene);
        const auto status = instance.evaluate();
        if (bm == 0 || bm == 19)
        {
            REQUIRE(status);
            REQUIRE(instance.build_frame());
            REQUIRE(instance.frame().commands.size() == 1);
            REQUIRE(instance.frame().commands[0].blend ==
                    (bm == 19 ? BlendMode::Multiply : BlendMode::Over));
            REQUIRE(instance.frame().commands[0].protection ==
                    (bm == 19 ? ColorProtection::None : ColorProtection::PreserveTranslucent));
        }
        else
        {
            REQUIRE(status.error == runtime::GeometryError::UnsupportedTransform);
            REQUIRE(instance.frame().vertices.empty());
        }
    }
}

TEST_CASE("Stencil unions preserve real sources and omit absent optional labels",
          "[runtime][scene]")
{
    // E-mote models like fraise-casual-a have a real sprite layer named "add_mask"
    // referenced in stencil_composite_mask_layers alongside "shirome".
    // When the real layer is present, it must be resolved as a geometry mask source.
    // V7 observes absent optional template labels contributing no coverage.
    // Empty unions and sources outside the current scope remain unverified.
    auto asset = rig();
    asset.parts.resize(1);
    asset.textures[0].width = 64;
    asset.textures[0].height = 32;
    asset.icons["atlas/a"] = {"atlas", 0, 0, 8, 4, 3, 1};
    asset.icons.emplace("atlas/mask", IconRect{"atlas", 0, 0, 8, 4, 3, 1});

    auto mask_content = sprite();
    mask_content.src = field(std::string{"atlas"});
    mask_content.icon = field(std::string{"mask"});
    auto shirome = layer("shirome", 0, mask_content);

    auto eye_content = sprite();
    auto eye = layer("eye", 0, eye_content);

    FrameContent stencil_content;
    stencil_content.src = field(std::string{"blank"});
    stencil_content.icon = field(std::string{"100:100:0:0"});
    auto stencil = layer("ステンシル(shirome)", 12, stencil_content);
    stencil.inherit_mask = 0x24007fc;
    stencil.stencil_type = 5;
    stencil.stencil_composite_mask_layers = {"shirome", "add_mask"};
    stencil.children = {eye};

    FrameContent root_content;
    root_content.src = field(std::string{"blank"});
    root_content.icon = field(std::string{"100:100:0:0"});

    SECTION("absent optional label leaves the existing source intact")
    {
        auto root = layer("root", 0, root_content);
        root.children = {shirome, stencil};
        asset.parts[0].layers = {root};
        auto compiled = runtime::compile_geometry_scene(asset);
        REQUIRE(compiled.scene);
        runtime::GeometryInstance instance(*compiled.scene);
        const auto status = instance.evaluate();
        REQUIRE(status);
        REQUIRE(instance.build_frame());
        REQUIRE(instance.frame().mask_commands.size() == 1);
    }

    SECTION("resolves real add_mask layer as geometry source")
    {
        auto add_mask_layer = layer("add_mask", 0, mask_content);
        auto root = layer("root", 0, root_content);
        root.children = {shirome, add_mask_layer, stencil};
        asset.parts[0].layers = {root};
        auto compiled = runtime::compile_geometry_scene(asset);
        REQUIRE(compiled.scene);
        runtime::GeometryInstance instance(*compiled.scene);
        const auto status = instance.evaluate();
        REQUIRE(status);
        REQUIRE(instance.build_frame());
        REQUIRE(instance.frame().mask_commands.size() == 2);
    }
    SECTION("all missing labels remain rejected")
    {
        stencil.stencil_composite_mask_layers = {"absent", "also_absent"};
        auto root = layer("root", 0, root_content);
        root.children = {shirome, stencil};
        asset.parts[0].layers = {root};
        auto compiled = runtime::compile_geometry_scene(asset);
        REQUIRE(compiled.scene);
        runtime::GeometryInstance instance(*compiled.scene);
        REQUIRE(instance.evaluate());
        REQUIRE(instance.build_frame().error == runtime::GeometryError::UnsupportedMask);
    }
    SECTION("a label declared in an unexpanded motion is not an optional slot")
    {
        auto root = layer("root", 0, root_content);
        root.children = {shirome, stencil};
        asset.parts[0].layers = {root};
        Part unused;
        unused.group = "rig";
        unused.name = "unused";
        unused.layers = {layer("add_mask", 0, mask_content)};
        asset.parts.push_back(unused);
        auto compiled = runtime::compile_geometry_scene(asset);
        REQUIRE(compiled.scene);
        runtime::GeometryInstance instance(*compiled.scene);
        REQUIRE(instance.evaluate());
        REQUIRE(instance.build_frame().error == runtime::GeometryError::UnsupportedMask);
    }
}

TEST_CASE("Mirrored geometry reverses normalized parameters once without changing inputs",
          "[runtime][scene]")
{
    auto asset = rig();
    auto normal = runtime::compile_geometry_scene(asset);
    REQUIRE(normal.scene);
    asset.mirror = true;
    asset.mirrored_parameters = {"pick"};
    auto mirrored = runtime::compile_geometry_scene(asset);
    REQUIRE(mirrored.scene);
    runtime::GeometryInstance a(*normal.scene), b(*mirrored.scene);
    REQUIRE(a.set_parameter(normal.scene->find_variable("pick"), 0.75f));
    REQUIRE(b.set_parameter(mirrored.scene->find_variable("pick"), -0.75f));
    REQUIRE(a.evaluate());
    REQUIRE(a.build_frame());
    for (int step = 0; step < 3; ++step)
    {
        REQUIRE(b.evaluate());
        REQUIRE(b.build_frame());
        REQUIRE(a.frame().vertices.size() == b.frame().vertices.size());
        for (std::size_t i = 0; i < a.frame().vertices.size(); ++i)
        {
            REQUIRE(a.frame().vertices[i].x == b.frame().vertices[i].x);
            REQUIRE(a.frame().vertices[i].y == b.frame().vertices[i].y);
        }
    }
    measured_allocations = 0;
    measure_allocations = true;
    for (int step = 0; step < 100; ++step)
    {
        (void)b.evaluate();
        (void)b.build_frame();
    }
    measure_allocations = false;
    REQUIRE(measured_allocations == 0);
}

TEST_CASE("Mirrored parameter compilation rejects invalid normalized references",
          "[runtime][scene]")
{
    auto asset = rig();
    asset.mirror = true;
    asset.mirrored_parameters = {"pick"};
    SECTION("unknown parameter")
    {
        asset.mirrored_parameters = {"plick"};
    }
    SECTION("duplicate parameter")
    {
        asset.mirrored_parameters.push_back("pick");
    }
    SECTION("unmirrored model")
    {
        asset.mirror = false;
    }
    const auto compiled = runtime::compile_geometry_scene(asset);
    REQUIRE_FALSE(compiled.scene);
    REQUIRE_FALSE(compiled.errors.empty());
}

TEST_CASE("Observed attr2 uses ordinary alpha and unknown protection flags fail",
          "[runtime][scene]")
{
    for (int attr : {0, 1, 2, 3})
    {
        auto asset = rig();
        asset.icons["atlas/a"].attr = attr;
        auto compiled = runtime::compile_geometry_scene(asset);
        REQUIRE(compiled.scene);
        runtime::GeometryInstance instance(*compiled.scene);
        if (attr == 3)
        {
            REQUIRE(instance.evaluate().error == runtime::GeometryError::UnsupportedTransform);
            REQUIRE(instance.frame().vertices.empty());
        }
        else
        {
            REQUIRE(instance.evaluate());
            REQUIRE(instance.build_frame());
            REQUIRE(instance.frame().commands[0].protection ==
                    (attr == 1 ? ColorProtection::PreserveTranslucent : ColorProtection::None));
        }
    }
}

TEST_CASE("Shape exclusion moves the resolved child origin without stretching vertices",
          "[runtime][scene][inheritance]")
{
    using namespace runtime;
    auto asset = rig();
    asset.parts.resize(1);
    asset.variables = {{"offset", {}}};
    const auto curve = [](bool outer)
    {
        FrameContent content;
        content.src = field(std::string{"blank"});
        content.icon = field(std::string{"16:16:0:0"});
        const auto patch = identity_patch();
        std::array<float, 32> points{};
        // Outer surface: x'=x+y*y/32, y'=y. Inner surface: y'=y+2.
        // These Bernstein coefficients encode the polynomial independently of
        // the geometry implementation; the expected positions below are analytic.
        constexpr std::array<float, 4> squared{0, 0, 1.0f / 3.0f, 1};
        for (std::size_t i = 0; i < patch.size(); ++i)
        {
            points[2 * i] = patch[i].x + (outer ? 0.5f * squared[i / 4] : 0);
            points[2 * i + 1] = patch[i].y + (outer ? 0 : 0.125f);
        }
        MeshData mesh;
        mesh.bp = field(points);
        content.mesh = field(mesh);
        return content;
    };
    auto inner_content = curve(false);
    inner_content.coord = field(std::array<float, 3>{2, 3, 0});
    auto inner = layer("own curve", 0, inner_content);
    inner.mesh_sync_child_mask = 8;
    inner.parameterize = field(0);
    inner.parameter = {{"offset", 0, 1, 1, 0, true}};
    inner_content.coord = field(std::array<float, 3>{2, 4, 0});
    inner.frame_list.push_back({1, 2, field(inner_content)});
    inner.children = {layer("sprite", 0, sprite())};
    FrameContent group_content;
    group_content.coord = field(std::array<float, 3>{1, 2, 0});
    auto group = layer("shape excluded", 0, group_content);
    group.inherit_mask = 0x7fc;
    group.children = {inner};
    auto outer = layer("outer curve", 0, curve(true));
    outer.mesh_sync_child_mask = 8;
    outer.children = {group};
    asset.parts[0].layers = {outer};
    std::array<Vec2, 5> expected{{{4.53125f, 7}, {4.53125f, 9}, {1.53125f, 6}, {5, 8}, {20, 12}}};
    SECTION("Vertex-synchronized parent")
    {
    }
    SECTION("Position-synchronized parent only bends the direct child origin")
    {
        asset.parts[0].layers[0].mesh_sync_child_mask = 1;
        expected = {{{3.125f, 7}, {3.125f, 9}, {0.125f, 6}, {3.125f, 8}, {16.25f, 12}}};
    }
    SECTION("Rigid rotation applies to the curve and its resolved attachment")
    {
        asset.parts[0].layers[0].frame_list[0].content.data.angle = field(90.0f);
        expected = {{{-7, 4.53125f}, {-9, 4.53125f}, {-6, 1.53125f}, {-8, 5}, {-6, 6}}};
    }
    SECTION("Combined own-domain helpers retain their displacement")
    {
        auto helper = layer("combined own curve", 0, curve(false));
        helper.mesh_combine = 1;
        helper.mesh_sync_child_mask = 8;
        helper.children = {layer("sprite", 0, sprite())};
        asset.parts[0].layers[0].children[0].children[0].children = {helper};
        expected = {{{5.53125f, 9}, {5.53125f, 11}, {2.53125f, 8}, {6.125f, 10}, {22.25f, 16}}};
    }
    const auto compiled = compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    GeometryInstance instance(*compiled.scene);
    REQUIRE(instance.evaluate());
    REQUIRE(instance.primitives().size() == 1);
    const auto node = instance.primitives()[0].node;
    // Base attachment is (3,7); sections exercise independently derived
    // positional synchronization, rigid rotation and combined own-curve variants.
    near(instance.sample_atlas_point(node, {3, 1}), expected[0]);
    near(instance.sample_atlas_point(node, {3, 3}), expected[1]);
    REQUIRE(instance.build_frame());
    near({instance.frame().vertices[0].x, instance.frame().vertices[0].y}, expected[2]);

    REQUIRE(instance.set_parameter(compiled.scene->find_variable("offset"), 1));
    REQUIRE(instance.evaluate());
    near(instance.sample_atlas_point(node, {3, 1}), expected[3]);
    instance.set_origin({10, -4});
    instance.set_scale(2);
    REQUIRE(instance.evaluate());
    near(instance.sample_atlas_point(node, {3, 1}), expected[4]);

    // Repeated changes must refresh attachment caches without steady allocations.
    for (int i = 0; i < 20; ++i)
    {
        REQUIRE(instance.evaluate());
        REQUIRE(instance.build_frame());
    }
    const auto offset = compiled.scene->find_variable("offset");
    measured_allocations = 0;
    measure_allocations = true;
    for (int i = 0; i < 20; ++i)
    {
        (void)instance.set_parameter(offset, static_cast<float>(i % 2));
        (void)instance.evaluate();
        (void)instance.build_frame();
    }
    measure_allocations = false;
    REQUIRE(measured_allocations == 0);
}

TEST_CASE("Nested exclusions resolve every parent curve at the current attachment origin",
          "[runtime][inheritance]")
{
    using namespace runtime;
    auto asset = rig();
    asset.parts.resize(1);
    asset.variables = {{"offset", {}}};
    const auto curve = [](bool outer)
    {
        auto result = FrameContent{};
        result.src = field(std::string{"blank"});
        result.icon = field(std::string{"16:16:0:0"});
        std::array<float, 32> controls{};
        const auto identity = identity_patch();
        constexpr std::array<float, 4> squared{0, 0, 1.0f / 3.0f, 1};
        for (std::size_t i = 0; i < identity.size(); ++i)
        {
            // Independent analytic surfaces: x += y*y/32; y += x*x/64.
            controls[2 * i] = identity[i].x + (outer ? 0.5f * squared[i / 4] : 0);
            controls[2 * i + 1] = identity[i].y + (outer ? 0 : 0.25f * squared[i % 4]);
        }
        MeshData mesh;
        mesh.bp = field(controls);
        result.mesh = field(mesh);
        return result;
    };
    const auto translated = [](float x, float y)
    {
        FrameContent result;
        result.coord = field(std::array<float, 3>{x, y, 0});
        return result;
    };
    auto tile_content = sprite();
    tile_content.coord = field(std::array<float, 3>{6, 8, 0});
    auto inner = layer("inner exclusion", 0, translated(4, 1));
    inner.inherit_mask = 0x7fc;
    inner.mesh_sync_child_mask = 8;
    inner.parameterize = field(0);
    inner.parameter = {{"offset", 0, 1, 1, 0, true}};
    inner.frame_list.push_back({1, 2, field(translated(4, 2))});
    inner.children = {layer("sprite", 0, tile_content)};
    auto mid_content = curve(false);
    mid_content.coord = field(std::array<float, 3>{2, 3, 0});
    auto middle = layer("middle curve", 0, mid_content);
    middle.mesh_sync_child_mask = 8;
    middle.children = {inner};
    auto outer = layer("outer exclusion", 0, translated(1, 2));
    outer.inherit_mask = 0x7fc;
    outer.mesh_sync_child_mask = 8;
    outer.children = {middle};
    auto root = layer("outer curve", 0, curve(true));
    root.mesh_sync_child_mask = 8;
    root.children = {outer};
    asset.parts[0].layers = {root};
    bool positional = false;
    SECTION("Both curves propagate to vertex chains")
    {
    }
    SECTION("Two position synchronizations leave a rigid quad")
    {
        asset.parts[0].layers[0].mesh_sync_child_mask = 1;
        asset.parts[0].layers[0].children[0].children[0].mesh_sync_child_mask = 1;
        positional = true;
    }
    const auto compiled = compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    GeometryInstance instance(*compiled.scene);
    const auto offset = compiled.scene->find_variable("offset");
    const auto check_pose = [&](float value, Vec2 origin, float scale)
    {
        REQUIRE(instance.set_parameter(offset, value));
        REQUIRE(instance.set_origin(origin));
        REQUIRE(instance.set_scale(scale));
        REQUIRE(instance.evaluate());
        REQUIRE(instance.build_frame());
        REQUIRE(instance.primitives().size() == 1);
        const auto node = instance.primitives()[0].node;
        const double y = 14 + static_cast<double>(value) + (positional ? 0.25 : 100.0 / 64.0);
        const double x = positional ? 13.125 : 13 + y * y / 32;
        const Vec2 expected{origin.x + scale * static_cast<float>(x),
                            origin.y + scale * static_cast<float>(y)};
        near(instance.sample_atlas_point(node, {3, 1}), expected);
        near(instance.sample_atlas_point(node, {5, 3}),
             {expected.x + 2 * scale, expected.y + 2 * scale});
        if (positional)
        {
            REQUIRE(instance.frame().vertices.size() == 4);
            REQUIRE(instance.frame().indices.size() == 6);
        }
    };
    check_pose(0, {}, 1);
    check_pose(1, {}, 1);
    check_pose(0, {10, -4}, 2);
    measured_allocations = 0;
    measure_allocations = true;
    for (int i = 0; i < 20; ++i)
    {
        (void)instance.set_parameter(offset, static_cast<float>(i % 2));
        (void)instance.evaluate();
        (void)instance.build_frame();
    }
    measure_allocations = false;
    REQUIRE(measured_allocations == 0);
}

TEST_CASE("Combined nested surfaces match submitted reference corner positions", "[runtime][scene]")
{
    std::ifstream input(std::string(NEKOMOTE_FIXTURES_DIR) + "/nested_combination_reference.json");
    REQUIRE(input.good());
    const auto reference = nlohmann::json::parse(input);
    for (const auto& sample : reference.at("cases"))
    {
        auto asset = rig();
        asset.parts.resize(1);
        asset.textures[0].width = 32;
        asset.textures[0].height = 32;
        asset.icons["atlas/a"] = {"atlas", 0, 0, 32, 32, 0, 0};
        const auto content = [](float x, float y, bool mesh_enabled, bool axis_x, float amount)
        {
            FrameContent result;
            result.src = field(std::string{"blank"});
            result.icon = field(std::string{"64:64:0:0"});
            result.coord = field(std::array<float, 3>{x, y, 0});
            if (mesh_enabled)
            {
                MeshData mesh;
                std::array<float, 32> controls{};
                const std::array<double, 4> squared{0, 0, 1.0 / 3.0, 1};
                for (std::size_t i = 0; i < 16; ++i)
                {
                    const auto row = i / 4, col = i % 4;
                    controls[i * 2] = static_cast<float>(
                        static_cast<double>(col) / 3.0 +
                        (axis_x ? static_cast<double>(amount) * squared[row] : 0));
                    controls[i * 2 + 1] = static_cast<float>(
                        static_cast<double>(row) / 3.0 +
                        (axis_x ? 0 : static_cast<double>(amount) * squared[col]));
                }
                mesh.bp = field(controls);
                result.mesh = field(mesh);
            }
            return result;
        };
        auto tile_content = sprite();
        tile_content.coord = field(std::array<float, 3>{7, 11, 0});
        auto tile = layer("tile", 0, tile_content);
        auto helper = layer("combined helper", 0, content(0, 0, true, false, -0.1f));
        helper.mesh_combine = 1;
        helper.mesh_sync_child_mask = 8;
        helper.children = {tile};
        auto inner = layer("inner boundary", 0,
                           content(5, 6, sample.at("own_curve").get<bool>(), true, 0.15f));
        inner.inherit_mask = 0x7fc;
        inner.mesh_sync_child_mask = 8;
        inner.children = {helper};
        auto middle = layer("middle curve", 0, content(3, 4, true, false, 0.3f));
        middle.mesh_sync_child_mask = 8;
        middle.children = {inner};
        auto outer = layer("outer boundary", 0, content(2, 3, false, true, 0));
        outer.inherit_mask = 0x7fc;
        outer.mesh_sync_child_mask = 8;
        outer.children = {middle};
        auto root_content = content(0, 0, true, true, 0.4f);
        root_content.angle = field(sample.at("root_angle").get<float>());
        auto root = layer("root curve", 0, root_content);
        root.mesh_sync_child_mask = 8;
        root.children = {outer};
        asset.parts[0].layers = {root};
        const auto compiled = runtime::compile_geometry_scene(asset);
        REQUIRE(compiled.scene);
        runtime::GeometryInstance instance(*compiled.scene);
        REQUIRE(instance.evaluate());
        REQUIRE(instance.primitives().size() == 1);
        for (const auto& point : sample.at("samples"))
        {
            const auto actual = instance.sample_atlas_point(
                instance.primitives()[0].node,
                {point.at("atlas")[0].get<float>(), point.at("atlas")[1].get<float>()});
            REQUIRE_THAT(actual.x,
                         Catch::Matchers::WithinAbs(point.at("expected")[0].get<float>(), 0.001));
            REQUIRE_THAT(actual.y,
                         Catch::Matchers::WithinAbs(point.at("expected")[1].get<float>(), 0.001));
        }
    }
}

TEST_CASE("Adjacent exclusions retain attachments through unsynchronized rigid groups",
          "[runtime][scene]")
{
    for (int sync : {0, 1, 8})
    {
        auto asset = rig();
        asset.parts.resize(1);
        asset.icons["atlas/a"] = {"atlas", 0, 0, 8, 4, 0, 0};
        FrameContent surface;
        surface.src = field(std::string{"blank"});
        surface.icon = field(std::string{"64:64:0:0"});
        MeshData mesh;
        auto patch = runtime::identity_patch();
        const std::array<float, 4> squared{0, 0, 1.0f / 3.0f, 1};
        std::array<float, 32> controls{};
        for (std::size_t i = 0; i < patch.size(); ++i)
        {
            controls[i * 2] = patch[i].x + 0.4f * squared[i / 4];
            controls[i * 2 + 1] = patch[i].y;
        }
        mesh.bp = field(controls);
        auto blank = surface;
        surface.mesh = field(mesh);
        auto tile_content = sprite();
        tile_content.coord = field(std::array<float, 3>{7, 11, 0});
        auto tile = layer("tile", 0, tile_content);
        blank.coord = field(std::array<float, 3>{5, 6, 0});
        auto inner = layer("inner exclusion", 0, blank);
        inner.inherit_mask = 0x7fc;
        inner.mesh_sync_child_mask = 8;
        inner.children = {tile};
        blank.coord = field(std::array<float, 3>{2, 3, 0});
        auto outer = layer("outer exclusion", 0, blank);
        outer.inherit_mask = 0x7fc;
        outer.mesh_sync_child_mask = sync;
        outer.children = {inner};
        auto root = layer("surface", 0, surface);
        root.mesh_sync_child_mask = 8;
        root.children = {outer};
        asset.parts[0].layers = {root};
        const auto compiled = runtime::compile_geometry_scene(asset);
        REQUIRE(compiled.scene);
        runtime::GeometryInstance instance(*compiled.scene);
        REQUIRE(instance.evaluate());
        REQUIRE(instance.primitives().size() == 1);
        // Current attachment (14,20) receives x += .4 * 20^2 / 64.
        const auto node = instance.primitives()[0].node;
        near(instance.sample_atlas_point(node, {0, 0}), {16.5f, 20});
        near(instance.sample_atlas_point(node, {2, 3}), {18.5f, 23});
    }
}

TEST_CASE("Discrete parameter mode one truncates before clipping fractional ranges",
          "[runtime][scene][parameter]")
{
    using namespace runtime;
    auto asset = rig();
    asset.parts.resize(1);
    asset.variables = {{"p", {}}};
    auto first = sprite();
    first.coord = field(std::array<float, 3>{0, 0, 0});
    auto last = first;
    last.coord = field(std::array<float, 3>{60, 0, 0});
    auto tile = layer("discrete tile", 0, first);
    tile.parameterize = field(0);
    tile.parameter = {{"p", -2.5f, 3.5f, 60, 1, true}};
    tile.frame_list = {{0, 3, field(first)}, {60, 2, field(last)}};
    asset.parts[0].layers = {tile};
    const auto compiled = compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    GeometryInstance instance(*compiled.scene);
    const auto parameter = compiled.scene->find_variable("p");
    // Independent reference observations, including values that distinguish
    // truncation from rounding/floor and clipping from truncating first.
    constexpr std::array<std::array<float, 2>, 10> cases{{{-3.0f, 0},
                                                          {-2.49f, 5},
                                                          {-1.9f, 15},
                                                          {-1.01f, 15},
                                                          {-0.4f, 25},
                                                          {0.49f, 25},
                                                          {0.99f, 25},
                                                          {1.01f, 35},
                                                          {3.49f, 55},
                                                          {4.0f, 60}}};
    for (const auto& sample : cases)
    {
        CAPTURE(sample[0]);
        REQUIRE(instance.set_parameter(parameter, sample[0]));
        REQUIRE(instance.evaluate());
        REQUIRE(instance.build_frame());
        near(instance.sample_atlas_point(instance.primitives()[0].node, {3, 1}), {sample[1], 0});
    }
    measured_allocations = 0;
    measure_allocations = true;
    for (const auto& sample : cases)
    {
        (void)instance.set_parameter(parameter, sample[0]);
        (void)instance.evaluate();
        (void)instance.build_frame();
    }
    measure_allocations = false;
    REQUIRE(measured_allocations == 0);

    // Observing selected mode-two inputs is insufficient to admit every mode.
    asset.parts[0].layers[0].parameter[0].discretization = 2;
    const auto unknown = compile_geometry_scene(asset);
    REQUIRE(unknown.scene);
    GeometryInstance unsupported(*unknown.scene);
    REQUIRE(unsupported.set_parameter(unknown.scene->find_variable("p"), 1));
    REQUIRE(unsupported.evaluate().error == GeometryError::UnsupportedDiscretization);
    REQUIRE(unsupported.frame().commands.empty());
}

TEST_CASE("Geometry capacity covers changing atlas aspect ratios without frame allocation",
          "[runtime][scene][capacity]")
{
    auto asset = rig();
    asset.parts.resize(1);
    asset.textures[0].width = asset.textures[0].height = 128;
    asset.icons["atlas/a"] = {"atlas", 0, 0, 64, 4, 0, 0};
    asset.icons["atlas/b"] = {"atlas", 0, 16, 64, 64, 0, 0};
    auto content = sprite();
    MeshData mesh;
    std::array<float, 32> points{};
    const auto patch = runtime::identity_patch();
    for (std::size_t i = 0; i < patch.size(); ++i)
    {
        points[i * 2] = patch[i].x;
        points[i * 2 + 1] = patch[i].y;
    }
    mesh.bp = field(points);
    content.mesh = field(mesh);
    auto node = layer("sprite", 0, content);
    node.parameterize = field(0);
    node.parameter = {{"pick", 0, 1, 1, 0, true}};
    content.icon = field(std::string{"b"});
    node.frame_list.push_back({1, 2, field(content)});
    asset.parts[0].layers = {node};
    auto compiled = runtime::compile_geometry_scene(asset);
    REQUIRE(compiled.scene);
    const auto capacity = compiled.scene->frame_capacity();
    REQUIRE(capacity.vertices < 300); // Preserve a useful bound, not a 22x22 square.
    runtime::GeometryInstance instance(*compiled.scene);
    const auto parameter = compiled.scene->find_variable("pick");
    bool valid = true;
    measured_allocations = 0;
    measure_allocations = true;
    for (int i = 0; i < 100; ++i)
    {
        valid &= static_cast<bool>(instance.set_parameter(parameter, static_cast<float>(i % 2)));
        valid &= static_cast<bool>(instance.evaluate());
        valid &= static_cast<bool>(instance.build_frame());
        valid &= instance.frame().vertices.size() <= capacity.vertices;
        valid &= instance.frame().indices.size() <= capacity.indices;
    }
    measure_allocations = false;
    REQUIRE(valid);
    REQUIRE(measured_allocations == 0);

    // Automatic density can be zero while an inherited curve still uses the
    // minimum 2x2 cells (nine vertices); a division-squared bound misses it.
    FrameContent parent_content;
    parent_content.src = field(std::string{"blank"});
    parent_content.icon = field(std::string{"64:64:0:0"});
    parent_content.mesh = field(mesh);
    auto parent = layer("curve", 0, parent_content);
    parent.mesh_division = 0;
    parent.mesh_sync_child_mask = 8;
    auto child = layer("auto", 0, sprite());
    child.mesh_division = 0;
    child.obj_tri_priority = 2;
    parent.children = {child};
    asset.parts[0].layers = {parent};
    auto automatic = runtime::compile_geometry_scene(asset);
    REQUIRE(automatic.scene);
    REQUIRE(automatic.scene->frame_capacity().vertices >= 9);
    runtime::GeometryInstance automatic_instance(*automatic.scene);
    measured_allocations = 0;
    measure_allocations = true;
    valid = static_cast<bool>(automatic_instance.evaluate());
    valid &= static_cast<bool>(automatic_instance.build_frame());
    measure_allocations = false;
    REQUIRE(valid);
    REQUIRE(automatic_instance.frame().vertices.size() == 9);
    REQUIRE(measured_allocations == 0);
}
