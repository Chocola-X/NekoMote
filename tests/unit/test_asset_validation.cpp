#include <catch2/catch_test_macros.hpp>
#include <nekomote/asset_io/asset_file.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
using Json = nlohmann::json;
namespace fs = std::filesystem;
struct Workspace
{
    fs::path path = fs::temp_directory_path() /
                    ("nekomote-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Workspace()
    {
        fs::create_directory(path);
    }
    ~Workspace()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;
};
Json read(const fs::path& path)
{
    std::ifstream f(path);
    Json j;
    f >> j;
    return j;
}
void write(const fs::path& path, const Json& j)
{
    std::ofstream f(path);
    f << j;
}
nekomote::asset_io::ImportResult import_modified(Workspace& w, const Json& source)
{
    write(w.path / "model.json", source);
    return nekomote::asset_io::import_emote(
        {w.path / "model.json", fs::path(NEKOMOTE_FIXTURES_DIR) / "minimal.resx.json", {}, {}});
}
Json physics_source()
{
    auto source = read(fs::path(NEKOMOTE_FIXTURES_DIR) / "minimal.json");
    auto& md = source["metadata"];
    const auto vector = [](float x, float y, float z)
    { return Json{{"x", x}, {"y", y}, {"z", z}}; };
    md["scale"] = 2;
    const Json binding{{"baseLayer", "anchor"}, {"label", "spring"}, {"parameter", "preset"},
                       {"var_lr", "sx"},        {"var_ud", "sy"},    {"enabled", 1}};
    auto spring = binding;
    spring.update(Json{{"friction", 0.125},
                       {"gravity", 0.3},
                       {"spring", 0.03125},
                       {"scale_x", 1},
                       {"scale_y", 2},
                       {"param",
                        {{"p", vector(0, 3, 4)},
                         {"pv", vector(1, 2, 3)},
                         {"op", vector(6, 7, 8)},
                         {"ofs", 2.5}}}});
    md["bustControl"] = Json::array({spring});
    auto chain = binding;
    chain.update(Json{{"label", "chain"},
                      {"var_lr", "cx"},
                      {"var_lrm", "middle"},
                      {"var_ud", "cy"},
                      {"b_rate", 0.01},
                      {"gravity", 0.2},
                      {"v_bound", 2},
                      {"friction_x", 0.03125},
                      {"friction_y", 0.125},
                      {"length", {8, 7}},
                      {"scale_x", {0.75, 0.25}},
                      {"scale_y", {2, 3}},
                      {"bend_spd", 0.2},
                      {"bend_vol", 3},
                      {"ud_eft", 0},
                      {"param",
                       {{"p", {vector(0, 9, 1), vector(3, 16, 2)}},
                        {"pv", {vector(0, 1, 2), vector(3, 4, 5)}},
                        {"bp", {vector(0, 8, 1), vector(3, 15, 2)}},
                        {"op", vector(6, 7, 8)},
                        {"ofs", -1},
                        {"bendR", 0.5},
                        {"bendS", 0.25}}}});
    md["hairControl"] = Json::array({chain});
    chain["enabled"] = 0;
    md["partsControl"] = Json::array({chain});
    return source;
}
} // namespace
TEST_CASE("Invalid mesh, numeric types and resource references fail with a field path", "[import]")
{
    Workspace w;
    auto source = read(fs::path(NEKOMOTE_FIXTURES_DIR) / "minimal.json");
    auto& layer = source["object"]["body_parts"]["motion"]["torso"]["layer"][0];
    SECTION("short bp")
    {
        layer["frameList"][0]["content"]["mesh"]["bp"] = {1, 2};
    }
    SECTION("negative mask")
    {
        layer["frameList"][0]["content"]["mask"] = -1;
    }
    SECTION("float mask")
    {
        layer["frameList"][0]["content"]["mask"] = 1.5;
    }
    SECTION("wrong coord")
    {
        layer["frameList"][0]["content"]["coord"] = {1, 2};
    }
    SECTION("unresolved icon")
    {
        layer["frameList"][0]["content"]["icon"] = "missing";
    }
    SECTION("invalid parameter index")
    {
        layer["parameterize"] = 1;
    }
    SECTION("nonmonotonic times")
    {
        layer["frameList"][1]["time"] = -1;
    }
    SECTION("unsupported spec")
    {
        source["spec"] = "unknown";
    }
    auto result = import_modified(w, source);
    REQUIRE_FALSE(result.ok);
    REQUIRE_FALSE(result.report.errors.empty());
    REQUIRE(result.report.errors[0].find('/') != std::string::npos);
}
TEST_CASE("Missing and explicit null are preserved separately from zero", "[import]")
{
    Workspace w;
    auto j = read(fs::path(NEKOMOTE_FIXTURES_DIR) / "minimal.json");
    auto& layer = j["object"]["body_parts"]["motion"]["torso"]["layer"][0];
    layer["parameterize"] = nullptr;
    layer["frameList"][0]["content"]["angle"] = 0;
    layer["frameList"][0]["content"]["bm"] = nullptr;
    layer["frameList"][1].erase("content");
    auto result = import_modified(w, j);
    REQUIRE(result.ok);
    const auto& l = result.asset.parts[0].layers[0];
    REQUIRE(l.parameterize.state == nekomote::model::FieldState::Null);
    REQUIRE(l.frame_list[0].content.data.bm.state == nekomote::model::FieldState::Null);
    REQUIRE(l.frame_list[0].content.data.angle.has_value());
    REQUIRE(l.frame_list[0].content.data.angle.data == 0.0f);
    REQUIRE(l.frame_list[1].content.state == nekomote::model::FieldState::Missing);
}
TEST_CASE("Motion parameter table and unknown fields survive import", "[import]")
{
    Workspace w;
    auto j = read(fs::path(NEKOMOTE_FIXTURES_DIR) / "minimal.json");
    auto& m = j["object"]["body_parts"]["motion"]["torso"];
    m["parameter"] = m["layer"][0]["parameter"];
    m["layer"][0].erase("parameter");
    m["layer"][0]["futureSemantics"] = 42;
    auto result = import_modified(w, j);
    REQUIRE(result.ok);
    REQUIRE(result.asset.parts[0].parameter.size() == 1);
    REQUIRE_FALSE(result.report.unsupported_fields.empty());
    REQUIRE(Json::parse(result.source_archive) == j);
}
TEST_CASE("Missing or invalid resx is an error", "[import]")
{
    Workspace w;
    write(w.path / "broken.resx.json", Json{{"Resources", Json::object()}});
    auto result = nekomote::asset_io::import_emote(
        {fs::path(NEKOMOTE_FIXTURES_DIR) / "minimal.json", w.path / "broken.resx.json", {}, {}});
    REQUIRE_FALSE(result.ok);
    REQUIRE_FALSE(result.report.errors.empty());
}
TEST_CASE("Bundle is portable, refuses overwrite and rejects corrupted assets", "[asset]")
{
    Workspace w;
    const fs::path root{NEKOMOTE_FIXTURES_DIR};
    auto r = nekomote::asset_io::import_emote(
        {root / "minimal.json", root / "minimal.resx.json", root, {}});
    REQUIRE(r.ok);
    const auto bundle = w.path / "bundle";
    REQUIRE(nekomote::asset_io::write_bundle(r, root, bundle).empty());
    auto loaded = nekomote::asset_io::load_asset(bundle / "model.nkm.json");
    REQUIRE(loaded.ok);
    REQUIRE(loaded.asset.textures[0].path == "textures/0.png");
    REQUIRE(fs::exists(bundle / loaded.asset.textures[0].path));
    REQUIRE(loaded.asset.total_frames == 2);
    REQUIRE(loaded.asset.parts[0].layers[0].frame_list[1].content.data.mesh.data.bp.state ==
            nekomote::model::FieldState::Null);
    REQUIRE_FALSE(nekomote::asset_io::write_bundle(r, root, bundle).empty());
    auto j = read(bundle / "model.nkm.json");
    SECTION("schema")
    {
        j["schema"] = 999;
    }
    SECTION("bounds")
    {
        j["model"]["icons"]["tex/0000"]["width"] = 999;
    }
    SECTION("reference")
    {
        j["model"]["parts"][0]["layers"][0]["frame_list"][0]["content"]["value"]["icon"]["value"] =
            "bad";
    }
    write(bundle / "model.nkm.json", j);
    loaded = nekomote::asset_io::load_asset(bundle / "model.nkm.json");
    REQUIRE_FALSE(loaded.ok);
    REQUIRE_FALSE(loaded.errors.empty());
}
TEST_CASE("Paired clamp data survives compilation and rejects invalid ranges", "[asset][import]")
{
    Workspace w;
    const fs::path root{NEKOMOTE_FIXTURES_DIR};
    auto j = read(root / "minimal.json");
    j["metadata"]["clampControl"] = Json::array({{{"label", "position"},
                                                  {"var_lr", "move_LR"},
                                                  {"var_ud", "move_UD"},
                                                  {"min", -30},
                                                  {"max", 30},
                                                  {"enabled", 1},
                                                  {"type", 0}}});
    SECTION("valid")
    {
        const auto result = import_modified(w, j);
        REQUIRE(result.ok);
        REQUIRE(result.asset.paired_clamps.size() == 1);
        const auto bundle = w.path / "bundle";
        REQUIRE(nekomote::asset_io::write_bundle(result, root, bundle).empty());
        const auto loaded = nekomote::asset_io::load_asset(bundle / "model.nkm.json");
        REQUIRE(loaded.ok);
        const auto& clamp = loaded.asset.paired_clamps[0];
        REQUIRE(clamp.variable_x == "move_LR");
        REQUIRE(clamp.variable_y == "move_UD");
        REQUIRE(clamp.range_begin == -30.0f);
        REQUIRE(clamp.range_end == 30.0f);
        REQUIRE(clamp.enabled);
        REQUIRE(clamp.type == 0);
    }
    SECTION("invalid range")
    {
        j["metadata"]["clampControl"][0]["max"] = -30;
        const auto result = import_modified(w, j);
        REQUIRE_FALSE(result.ok);
        REQUIRE(result.report.errors[0].find("/metadata/clampControl/0") != std::string::npos);
    }
    SECTION("invalid enabled flag")
    {
        j["metadata"]["clampControl"][0]["enabled"] = 2;
        REQUIRE_FALSE(import_modified(w, j).ok);
    }
}

TEST_CASE("Expression and selector rules survive offline compilation", "[asset][import]")
{
    Workspace w;
    const fs::path root{NEKOMOTE_FIXTURES_DIR};
    auto source = read(root / "minimal.json");
    source["metadata"]["eyebrowControl"] = Json::array({{{"label", "expression"},
                                                         {"enabled", 1},
                                                         {"beginFrame", 0},
                                                         {"edge", {{0, 10}, {11, 20}}},
                                                         {"node", {{10, 11}}}}});
    source["metadata"]["selectorControl"] = Json::array(
        {{{"label", "arm"},
          {"enabled", 1},
          {"optionList", Json::array({{{"label", "fade"}, {"onValue", 0}, {"offValue", 1}}})}}});
    SECTION("portable roundtrip")
    {
        const auto imported = import_modified(w, source);
        REQUIRE(imported.ok);
        REQUIRE(nekomote::asset_io::write_bundle(imported, root, w.path / "bundle").empty());
        const auto loaded = nekomote::asset_io::load_asset(w.path / "bundle/model.nkm.json");
        REQUIRE(loaded.ok);
        REQUIRE(loaded.asset.variable_controls.size() == 2);
        const auto& expression = loaded.asset.variable_controls[0];
        REQUIRE(expression.kind == nekomote::model::VariableControlKind::Expression);
        REQUIRE(expression.edges.size() == 2);
        REQUIRE(expression.junctions[0][1] == 11.0f);
        REQUIRE(loaded.asset.variable_controls[1].options[0].off_value == 1.0f);
        auto compiled = read(w.path / "bundle/model.nkm.json");
        compiled["model"]["variable_controls"][0]["kind"] = 100;
        write(w.path / "bundle/model.nkm.json", compiled);
        REQUIRE_FALSE(nekomote::asset_io::load_asset(w.path / "bundle/model.nkm.json").ok);
    }
    SECTION("invalid edge")
    {
        source["metadata"]["eyebrowControl"][0]["edge"][0] = {10, 0};
        const auto imported = import_modified(w, source);
        REQUIRE_FALSE(imported.ok);
        REQUIRE(imported.report.errors[0].find("/metadata/eyebrowControl/0/edge") !=
                std::string::npos);
    }
    SECTION("invalid flag")
    {
        source["metadata"]["selectorControl"][0]["enabled"] = 2;
        REQUIRE_FALSE(import_modified(w, source).ok);
    }
}

TEST_CASE("Physical snapshots and categories survive portable offline compilation",
          "[asset][import]")
{
    Workspace w;
    const fs::path root{NEKOMOTE_FIXTURES_DIR};
    auto source = physics_source();
    source["metadata"]["hairControl"][0]["futureConstraint"] = 9;
    const auto imported = import_modified(w, source);
    REQUIRE(imported.ok);
    REQUIRE(nekomote::asset_io::write_bundle(imported, root, w.path / "bundle").empty());
    const auto file = w.path / "bundle/model.nkm.json";
    const auto loaded = nekomote::asset_io::load_asset(file);
    REQUIRE(loaded.ok);
    const auto& model = loaded.asset;
    REQUIRE(model.author_scale.has_value());
    REQUIRE(model.author_scale.data == 2.0f);
    REQUIRE(model.spring_controls.size() == 1);
    REQUIRE(model.chain_controls.size() == 2);
    const auto& spring = model.spring_controls[0];
    REQUIRE(spring.binding.source_path == "/metadata/bustControl/0");
    REQUIRE(spring.binding.anchor == "anchor");
    REQUIRE(spring.binding.preset == "preset");
    REQUIRE(spring.position[2] == 4.0f);
    REQUIRE(spring.velocity[1] == 2.0f);
    REQUIRE(spring.previous_anchor[0] == 6.0f);
    REQUIRE(spring.vertical_offset == 2.5f);
    REQUIRE(spring.stiffness == 0.03125f);
    const auto& chain = model.chain_controls[0];
    REQUIRE(chain.kind == nekomote::model::ChainKind::Hair);
    REQUIRE(chain.binding.enabled);
    REQUIRE(chain.binding.variable_x == "cx");
    REQUIRE(chain.variable_middle == "middle");
    REQUIRE(chain.velocity_retention == 2.0f);
    REQUIRE(chain.friction[1] == 0.125f);
    REQUIRE(chain.length[1] == 7.0f);
    REQUIRE(chain.rest_position[1][2] == 2.0f);
    REQUIRE(chain.position[1][1] == 16.0f);
    REQUIRE(chain.velocity[1][2] == 5.0f);
    REQUIRE(chain.bend_gain == 0.25f);
    REQUIRE(chain.binding.unsupported_fields ==
            std::vector<std::string>{"/metadata/hairControl/0/futureConstraint"});
    REQUIRE(model.chain_controls[1].kind == nekomote::model::ChainKind::Parts);
    REQUIRE_FALSE(model.chain_controls[1].binding.enabled);

    auto compiled = read(file);
    SECTION("unsupported old schema")
    {
        compiled["schema"] = 3;
    }
    SECTION("invalid category")
    {
        compiled["model"]["chain_controls"][0]["kind"] = 2;
    }
    SECTION("invalid snapshot length")
    {
        compiled["model"]["chain_controls"][0]["position"] = {{0, 0, 0}};
    }
    SECTION("invalid coefficient")
    {
        compiled["model"]["spring_controls"][0]["friction"] = 1.5;
    }
    write(file, compiled);
    REQUIRE_FALSE(nekomote::asset_io::load_asset(file).ok);
}

TEST_CASE("Malformed physical source data fails with the original field path", "[import]")
{
    Workspace w;
    auto source = physics_source();
    std::string expected;
    SECTION("missing required coefficient")
    {
        source["metadata"]["bustControl"][0].erase("spring");
        expected = "/metadata/bustControl/0/spring";
    }
    SECTION("null saved snapshot")
    {
        source["metadata"]["hairControl"][0]["param"] = nullptr;
        expected = "/metadata/hairControl/0/param";
    }
    SECTION("missing author units")
    {
        source["metadata"].erase("scale");
        expected = "/metadata/scale";
    }
    SECTION("zero author scale")
    {
        source["metadata"]["scale"] = 0;
        expected = "/metadata/scale";
    }
    SECTION("wrong particle count")
    {
        source["metadata"]["hairControl"][0]["param"]["p"].erase(1);
        expected = "/metadata/hairControl/0/param/p";
    }
    SECTION("null vector component")
    {
        source["metadata"]["bustControl"][0]["param"]["op"]["z"] = nullptr;
        expected = "/metadata/bustControl/0/param/op/z";
    }
    SECTION("negative length")
    {
        source["metadata"]["hairControl"][0]["length"][1] = -1;
        expected = "/metadata/hairControl/0/length";
    }
    SECTION("invalid enabled flag")
    {
        source["metadata"]["partsControl"][0]["enabled"] = 2;
        expected = "/metadata/partsControl/0/enabled";
    }
    SECTION("bad binding")
    {
        source["metadata"]["hairControl"][0]["var_lrm"] = "cy";
        expected = "/metadata/hairControl/0/var_lrm";
    }
    const auto imported = import_modified(w, source);
    REQUIRE_FALSE(imported.ok);
    REQUIRE_FALSE(imported.report.errors.empty());
    REQUIRE(imported.report.errors[0].find(expected) != std::string::npos);
}
