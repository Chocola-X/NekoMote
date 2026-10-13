#include <catch2/catch_test_macros.hpp>
#include <nekomote/asset_io/image.hpp>
#include <nekomote/asset_io/read_only_file.hpp>

#include <sys/mman.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
struct TemporaryPng
{
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("nekomote-crop-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
    ~TemporaryPng()
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
void require_reconstructed(nekomote::ImageViewRgba8 source,
                           const nekomote::asset_io::ImageResult& cropped)
{
    REQUIRE(cropped.ok);
    REQUIRE(cropped.region.source_width == source.width);
    REQUIRE(cropped.region.source_height == source.height);
    REQUIRE(cropped.region.valid_for(cropped.image.width, cropped.image.height));
    std::vector<std::uint8_t> reconstructed(source.pixels.size(), 0);
    for (int y = 0; y < cropped.image.height; ++y)
        std::copy_n(cropped.image.pixels.data() +
                        static_cast<std::size_t>(y) * cropped.image.width * 4,
                    static_cast<std::size_t>(cropped.image.width) * 4,
                    reconstructed.data() +
                        (static_cast<std::size_t>(y + cropped.region.top) * source.width +
                         cropped.region.left) *
                            4);
    REQUIRE(std::equal(reconstructed.begin(), reconstructed.end(), source.pixels.begin(),
                       source.pixels.end()));
}
} // namespace

TEST_CASE("Decoded pixels keep ownership across moves and return memory to the OS", "[image]")
{
    const std::filesystem::path root{NEKOMOTE_FIXTURES_DIR};
    void* address = nullptr;
    unsigned char resident = 0;
    {
        auto result = nekomote::asset_io::load_png(root / "minimal-resources/tex.png");
        REQUIRE(result.ok);
        address = result.image.pixels.data();
        REQUIRE(::mincore(address, 1, &resident) == 0);
        const auto bytes = result.image.pixels.size();
        nekomote::asset_io::DecodedImageRgba8 moved(std::move(result.image));
        REQUIRE(moved.pixels.data() == address);
        REQUIRE(moved.pixels.size() == bytes);
        REQUIRE(result.image.pixels.empty());
        auto replacement = nekomote::asset_io::load_png(root / "minimal-resources/tex.png");
        REQUIRE(replacement.ok);
        replacement.image = std::move(moved);
        REQUIRE(replacement.image.pixels.data() == address);
        REQUIRE(moved.pixels.empty());
        const nekomote::ImageViewRgba8 view = replacement.image;
        REQUIRE(view.pixels.data() == address);
        REQUIRE(view.pixels[0] == 255);
    }
    errno = 0;
    REQUIRE(::mincore(address, 1, &resident) == -1);
    REQUIRE(errno == ENOMEM);
}

TEST_CASE("Atlas crop preserves low alpha, transparent RGB and filtering gutters", "[image][crop]")
{
    nekomote::ImageRgba8 source{12, 10, std::vector<std::uint8_t>(12 * 10 * 4, 0)};
    source.pixels[(3 * 12 + 4) * 4 + 3] = 1;
    source.pixels[(6 * 12 + 8) * 4] = 255; // Alpha zero RGB still affects bilinear samples.
    TemporaryPng file;
    REQUIRE(nekomote::asset_io::write_png(file.path, source).empty());
    auto cropped = nekomote::asset_io::load_cropped_png(file.path);
    require_reconstructed(source, cropped);
    REQUIRE(cropped.region.left == 3);
    REQUIRE(cropped.region.top == 2);
    REQUIRE(cropped.image.width == 7);
    REQUIRE(cropped.image.height == 6);
    REQUIRE(cropped.image.pixels.capacity() < source.pixels.size());
    // Opaque/colored original edges must keep their original clamp behaviour.
    source.pixels[0] = 100;
    REQUIRE(nekomote::asset_io::write_png(file.path, source).empty());
    cropped = nekomote::asset_io::load_cropped_png(file.path);
    require_reconstructed(source, cropped);
    REQUIRE(cropped.region.left == 0);
    REQUIRE(cropped.region.top == 0);
}
TEST_CASE("Empty and fully occupied atlases have defined crop storage", "[image][crop]")
{
    TemporaryPng file;
    for (const auto dimensions :
         {std::pair{1, 1}, std::pair{1, 8}, std::pair{13, 1}, std::pair{13, 9}})
    {
        const auto [w, h] = dimensions;
        nekomote::ImageRgba8 source{w, h, std::vector<std::uint8_t>(w * h * 4, 0)};
        REQUIRE(nekomote::asset_io::write_png(file.path, source).empty());
        auto cropped = nekomote::asset_io::load_cropped_png(file.path);
        require_reconstructed(source, cropped);
        REQUIRE(cropped.image.width == 1);
        REQUIRE(cropped.image.height == 1);
        REQUIRE(cropped.image.pixels.size() == 4);
        std::fill(source.pixels.begin(), source.pixels.end(), 255);
        REQUIRE(nekomote::asset_io::write_png(file.path, source).empty());
        cropped = nekomote::asset_io::load_cropped_png(file.path);
        require_reconstructed(source, cropped);
        REQUIRE(cropped.image.width == w);
        REQUIRE(cropped.image.height == h);
    }
}
TEST_CASE("Crop decoding preserves PNG formats including Adam7 and rejects invalid input",
          "[image][crop]")
{
    const std::filesystem::path root{NEKOMOTE_FIXTURES_DIR};
    for (const auto* name : {"gray1.png", "gray16.png", "palette2.png", "rgb_trns.png",
                             "rgba16.png", "gamma.png", "adam7.png"})
    {
        INFO(name);
        const auto path = root / "png_normalization" / name;
        const auto full = nekomote::asset_io::load_png(path);
        REQUIRE(full.ok);
        require_reconstructed(full.image, nekomote::asset_io::load_cropped_png(path));
    }
    const auto full = nekomote::asset_io::load_png(root / "minimal-resources/tex.png");
    REQUIRE(full.ok);
    require_reconstructed(full.image,
                          nekomote::asset_io::load_cropped_png(root / "minimal-resources/tex.png"));
    for (const auto& path :
         {root / "missing.png", root / "minimal.json", root / "png_normalization/truncated.png",
          root / "png_normalization/oversized.png"})
    {
        const auto result = nekomote::asset_io::load_cropped_png(path);
        REQUIRE_FALSE(result.ok);
        REQUIRE_FALSE(result.error.empty());
        REQUIRE(result.image.pixels.empty());
        REQUIRE(result.image.width == 0);
    }
}

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
        REQUIRE(std::equal(decoded.image.pixels.begin(), decoded.image.pixels.end(),
                           expected.begin(), expected.end()));
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
