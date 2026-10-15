#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/asset_io/import_emote.hpp>
#include <nekomote/model/asset.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace nekomote;

namespace
{

fs::path fixtures_dir()
{
    return fs::path{NEKOMOTE_FIXTURES_DIR};
}

asset_io::ImportResult import_fixture(const std::string& name)
{
    asset_io::ImportOptions opts;
    opts.json_path = fixtures_dir() / (name + ".json");
    opts.resx_path = fixtures_dir() / (name + ".resx.json");
    return asset_io::import_emote(opts);
}

} // namespace

TEST_CASE("Minimal fixture imports with correct structure", "[import]")
{
    auto result = import_fixture("minimal");
    REQUIRE(result.ok);

    const model::ModelAsset& a = result.asset;
    REQUIRE(a.spec == "ems");
    REQUIRE_THAT(a.version, Catch::Matchers::WithinAbs(3.03f, 1e-3f));
    REQUIRE(a.screen_width == 800);
    REQUIRE(a.screen_height == 600);

    REQUIRE(a.parts.size() == 1);
    REQUIRE(a.parts[0].group == "body_parts");
    REQUIRE(a.parts[0].name == "torso");
    REQUIRE(a.parts[0].layers.size() == 1);

    const model::Layer& layer = a.parts[0].layers[0];
    REQUIRE(layer.label == "base");
    REQUIRE(layer.mesh_division == 4);
    REQUIRE(layer.mesh_transform == 1);
    REQUIRE(layer.parameterize.value() == 0);
    REQUIRE(layer.frame_list.size() == 2);
    REQUIRE(layer.parameter.size() == 1);
    REQUIRE(layer.parameter[0].id == "move_UD");
    REQUIRE(layer.parameter[0].range_begin == -30.0f);
    REQUIRE(layer.parameter[0].range_end == 30.0f);

    const model::LayerFrame& f0 = layer.frame_list[0];
    REQUIRE(f0.time == 0.0f);
    REQUIRE(f0.content.data.coord.data[0] == 10.0f);
    REQUIRE(f0.content.data.coord.data[1] == 20.0f);
    REQUIRE(f0.content.data.icon.data == "0000");
    REQUIRE(f0.content.data.src.data == "tex");
    REQUIRE(f0.content.data.opa.has_value());
    REQUIRE(f0.content.data.opa.value() == 1.0f);
    REQUIRE(f0.content.data.mesh.data.bp.has_value());

    const model::LayerFrame& f1 = layer.frame_list[1];
    REQUIRE(f1.time == 30.0f);
    REQUIRE_FALSE(f1.content.data.mesh.data.bp.has_value());
}

TEST_CASE("Minimal fixture icons and texture", "[import]")
{
    auto result = import_fixture("minimal");
    REQUIRE(result.ok);

    const model::ModelAsset& a = result.asset;
    REQUIRE(a.icons.size() == 2);
    REQUIRE(a.icons.count("tex/0000") == 1);
    REQUIRE(a.icons.count("tex/0001") == 1);
    REQUIRE(a.icons.at("tex/0000").width == 32.0f);
    REQUIRE(a.icons.at("tex/0001").width == 64.0f);

    REQUIRE(a.textures.size() == 1);
    REQUIRE(a.textures[0].name == "tex");
    REQUIRE(a.textures[0].width == 256);
    REQUIRE(a.textures[0].height == 256);
    REQUIRE(a.textures[0].path == "minimal-resources/tex.png");
    REQUIRE(result.report.texture_path == "minimal-resources/tex.png");
}

TEST_CASE("Minimal fixture variables and parts order", "[import]")
{
    auto result = import_fixture("minimal");
    REQUIRE(result.ok);

    const model::ModelAsset& a = result.asset;
    REQUIRE(a.variables.size() == 1);
    REQUIRE(a.variables[0].label == "move_UD");
    REQUIRE(a.variables[0].frames.size() == 3);
    REQUIRE(a.variables[0].frames[0].first == -30);
    REQUIRE(a.variables[0].frames[0].second == "up");

    REQUIRE(a.parts_order.size() == 2);
    REQUIRE(a.parts_order[0] == "0");
    REQUIRE(a.parts_order[1] == "1");
}

TEST_CASE("Minimal fixture diagnostics counts", "[import]")
{
    auto result = import_fixture("minimal");
    REQUIRE(result.ok);
    REQUIRE(result.report.parts_count == 1);
    REQUIRE(result.report.layers_count == 1);
    REQUIRE(result.report.frames_count == 2);
    REQUIRE(result.report.icons_count == 2);
    REQUIRE(result.report.textures_count == 1);
    REQUIRE(result.report.variables_count == 1);
    REQUIRE(result.asset.bp_nonnull == 1);
    REQUIRE(result.asset.cc_nonnull == 0);
}

TEST_CASE("Missing file fails gracefully", "[import]")
{
    asset_io::ImportOptions opts;
    opts.json_path = fixtures_dir() / "nonexistent.json";
    opts.resx_path = fixtures_dir() / "nonexistent.resx.json";
    auto result = asset_io::import_emote(opts);
    REQUIRE_FALSE(result.ok);
    REQUIRE_FALSE(result.report.errors.empty());
}

TEST_CASE("Real model chocola-date-a imports", "[import][real][integration]")
{
    const char* root = std::getenv("NEKOMOTE_TEST_REAL_ASSET");
    if (root == nullptr)
    {
        SKIP("NEKOMOTE_TEST_REAL_ASSET is not set");
    }

    fs::path p = fs::path{root} / "chocola" / "chocola-date-a";
    asset_io::ImportOptions opts;
    opts.json_path = p / "chocola-date-a.json";
    opts.resx_path = p / "chocola-date-a.resx.json";
    opts.resource_root = p;

    auto result = asset_io::import_emote(opts);
    REQUIRE(result.ok);

    const model::ModelAsset& a = result.asset;
    REQUIRE(a.spec == "ems");
    REQUIRE(a.screen_width == 800);
    REQUIRE(a.screen_height == 1080);
    REQUIRE(a.parts.size() > 0);
    REQUIRE(a.icons.size() == 80);
    REQUIRE(a.variables.size() == 53);
    REQUIRE(a.total_layers == 431);
    REQUIRE(a.total_frames == 1892);
    REQUIRE(a.bp_nonnull == 468);
    REQUIRE(a.cc_nonnull == 0);
    REQUIRE(result.report.texture_path.find("tex-texture.png") != std::string::npos);
}
