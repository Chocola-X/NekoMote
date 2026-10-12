#include <catch2/catch_test_macros.hpp>
#include <nekomote/asset_io/image.hpp>
#include <nekomote/asset_io/read_only_file.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <vector>

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
    for (const auto* name : {"truncated.png", "oversized.png"})
    {
        INFO(name);
        r = nekomote::asset_io::load_png(root / "png_normalization" / name);
        REQUIRE_FALSE(r.ok);
        REQUIRE_FALSE(r.error.empty());
        REQUIRE(r.image.width == 0);
        REQUIRE(r.image.pixels.empty());
    }
    r = nekomote::asset_io::load_png(root / "minimal.json");
    REQUIRE_FALSE(r.ok);
    REQUIRE_FALSE(r.error.empty());
    REQUIRE(r.image.pixels.empty());
}
TEST_CASE("PNG normalization preserves encoded color and transparency across formats", "[image]")
{
    using Bytes = std::vector<std::uint8_t>;
    const std::filesystem::path root =
        std::filesystem::path(NEKOMOTE_FIXTURES_DIR) / "png_normalization";
    const auto check = [&root](const char* name, const Bytes& expected)
    {
        INFO(name);
        auto decoded = nekomote::asset_io::load_png(root / name);
        INFO(decoded.error);
        REQUIRE(decoded.ok);
        REQUIRE(decoded.image.pixels == expected);
    };
    check("gray1.png", {0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255});
    check("gray16.png", {18, 18, 18, 255, 128, 128, 128, 0, 255, 255, 255, 255});
    check("palette2.png", {10, 20, 30, 0, 40, 50, 60, 64, 70, 80, 90, 128, 100, 110, 120, 255});
    check("rgb_trns.png", {17, 18, 19, 0, 100, 101, 102, 255});
    check("rgba16.png", {18, 86, 154, 128, 171, 0, 255, 0});
    check("gamma.png", {50, 100, 150, 128, 200, 150, 100, 0});
    Bytes expected;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x)
            for (int value : {x * 30, y * 50, x + y, (x + y) % 2 ? 128 : 255})
                expected.push_back(static_cast<std::uint8_t>(value));
    check("adam7.png", expected);
}
TEST_CASE("Read-only file views own their mapping and reject invalid files", "[asset][file]")
{
    using nekomote::asset_io::ReadOnlyFile;
    const auto source =
        std::filesystem::path(NEKOMOTE_FIXTURES_DIR) / "png_normalization/adam7.png";
    ReadOnlyFile first(source);
    ReadOnlyFile second(source);
    REQUIRE(first.bytes().size() == std::filesystem::file_size(source));
    REQUIRE(std::equal(first.bytes().begin(), first.bytes().end(), second.bytes().begin()));
    REQUIRE(first.bytes()[0] == 137);
    REQUIRE(first.bytes()[1] == 'P');
    REQUIRE_THROWS_AS(ReadOnlyFile(source / "missing"), std::system_error);
    REQUIRE_THROWS_AS(ReadOnlyFile(source.parent_path()), std::runtime_error);
    REQUIRE_THROWS_AS(ReadOnlyFile(source.parent_path() / "empty.bin"), std::runtime_error);
}
