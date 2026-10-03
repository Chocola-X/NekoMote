#include <catch2/catch_test_macros.hpp>
#include <nekomote/asset_io/image.hpp>

#include <filesystem>

TEST_CASE("PNG decoding preserves atlas dimensions, row orientation and alpha", "[image]")
{
    const std::filesystem::path root{NEKOMOTE_FIXTURES_DIR};
    auto r = nekomote::asset_io::load_png(root / "minimal-resources/tex.png");
    REQUIRE(r.ok);
    REQUIRE(r.image.width == 256);
    REQUIRE(r.image.height == 256);
    REQUIRE(r.image.pixels.size() == 256 * 256 * 4);
    const auto pixel = [&r](std::size_t x, std::size_t y, std::size_t c)
    { return r.image.pixels[(y * 256 + x) * 4 + c]; };
    REQUIRE(pixel(0, 0, 0) == 255);
    REQUIRE(pixel(0, 31, 0) == 0);
    REQUIRE(pixel(31, 0, 3) == 128);
    REQUIRE(pixel(255, 255, 3) == 0);
}
TEST_CASE("Invalid images fail without returning usable pixels", "[image]")
{
    const std::filesystem::path root{NEKOMOTE_FIXTURES_DIR};
    auto r = nekomote::asset_io::load_png(root / "does-not-exist.png");
    REQUIRE_FALSE(r.ok);
    REQUIRE_FALSE(r.error.empty());
    REQUIRE(r.image.pixels.empty());
    r = nekomote::asset_io::load_png(root / "minimal.json");
    REQUIRE_FALSE(r.ok);
    REQUIRE_FALSE(r.error.empty());
    REQUIRE(r.image.pixels.empty());
}
