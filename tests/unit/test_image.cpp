#include <catch2/catch_test_macros.hpp>
#include <nekomote/asset_io/image.hpp>
#include <nekomote/asset_io/read_only_file.hpp>

#include <sys/mman.h>
#include <zlib.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    for (const auto& dimensions :
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
namespace
{
struct TemporaryDirectory
{
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("nekomote-recompress-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory()
    {
        std::filesystem::create_directory(path);
    }
    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
std::vector<std::uint8_t> gradient_rgba(int width, int height)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            auto* p = pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            p[0] = static_cast<std::uint8_t>(x * 7);
            p[1] = static_cast<std::uint8_t>(y * 11);
            p[2] = static_cast<std::uint8_t>((x + y) * 3);
            p[3] = static_cast<std::uint8_t>(x < width / 2 ? 255 : 128);
        }
    return pixels;
}
} // namespace

TEST_CASE("Recompression is lossless and produces a leaner non-interlaced file", "[image]")
{
    TemporaryDirectory directory;
    const auto source = directory.path / "source.png";
    const auto output = directory.path / "recompressed.png";
    const auto pixels = gradient_rgba(64, 48);
    REQUIRE(nekomote::asset_io::write_png(source, {64, 48, pixels}).empty());
    REQUIRE(nekomote::asset_io::recompress_png(source, output).empty());
    const auto original = nekomote::asset_io::load_png(source);
    const auto recompressed = nekomote::asset_io::load_png(output);
    REQUIRE(original.ok);
    REQUIRE(recompressed.ok);
    REQUIRE(recompressed.image.width == 64);
    REQUIRE(recompressed.image.height == 48);
    REQUIRE(recompressed.image.pixels == original.image.pixels);
    REQUIRE(recompressed.region.source_width == original.region.source_width);
    // A smooth gradient compresses far better with Paeth/adaptive rows than the
    // default encoding; the re-encode must not grow the file.
    REQUIRE(std::filesystem::file_size(output) <= std::filesystem::file_size(source));
}

TEST_CASE("Recompression rejects unsupported layouts and keeps failures clean", "[image]")
{
    TemporaryDirectory directory;
    const auto source = directory.path / "palette.png";
    const auto output = directory.path / "output.png";
    const std::filesystem::path root{NEKOMOTE_FIXTURES_DIR};
    REQUIRE(std::filesystem::copy_file(root / "png_normalization/palette2.png", source));
    const auto error = nekomote::asset_io::recompress_png(source, output);
    REQUIRE(!error.empty());
    REQUIRE_FALSE(std::filesystem::exists(output));
    // A missing source must fail without creating the destination either.
    const auto missing = nekomote::asset_io::recompress_png(directory.path / "missing.png", output);
    REQUIRE(!missing.empty());
    REQUIRE_FALSE(std::filesystem::exists(output));
}

TEST_CASE("Metadata stripping drops auxiliary chunks and keeps pixels identical", "[image]")
{
    TemporaryDirectory directory;
    const auto source = directory.path / "with-metadata.png";
    const auto output = directory.path / "stripped.png";
    const auto pixels = gradient_rgba(32, 20);
    REQUIRE(nekomote::asset_io::write_png(source, {32, 20, pixels}).empty());
    std::vector<std::uint8_t> raw;
    {
        std::ifstream stream(source, std::ios::binary);
        raw.assign(std::istreambuf_iterator<char>(stream), {});
    }
    const auto make_text_chunk = [](const std::string& body)
    {
        static const char type[] = "tEXt";
        std::vector<std::uint8_t> chunk;
        chunk.push_back(static_cast<std::uint8_t>(body.size() >> 24));
        chunk.push_back(static_cast<std::uint8_t>(body.size() >> 16));
        chunk.push_back(static_cast<std::uint8_t>(body.size() >> 8));
        chunk.push_back(static_cast<std::uint8_t>(body.size()));
        chunk.insert(chunk.end(), type, type + 4);
        chunk.insert(chunk.end(), body.begin(), body.end());
        auto crc = crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(type), 4);
        crc =
            crc32(crc, reinterpret_cast<const Bytef*>(body.data()), static_cast<uInt>(body.size()));
        chunk.push_back(static_cast<std::uint8_t>(crc >> 24));
        chunk.push_back(static_cast<std::uint8_t>(crc >> 16));
        chunk.push_back(static_cast<std::uint8_t>(crc >> 8));
        chunk.push_back(static_cast<std::uint8_t>(crc));
        return chunk;
    };
    const auto append =
        [&](const std::vector<std::uint8_t>& prefix, const std::vector<std::uint8_t>& extra)
    {
        std::vector<std::uint8_t> combined = prefix;
        combined.insert(combined.end(), extra.begin(), extra.end());
        return combined;
    };
    // Graft a valid tEXt chunk right after IHDR (8 + 25 bytes).
    const auto valid = append(std::vector<std::uint8_t>(raw.begin(), raw.begin() + 8 + 25),
                              append(make_text_chunk(std::string("note\0provenance", 15)),
                                     std::vector<std::uint8_t>(raw.begin() + 8 + 25, raw.end())));
    {
        std::ofstream stream(source, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(valid.data()),
                     static_cast<std::streamsize>(valid.size()));
    }
    REQUIRE(nekomote::asset_io::strip_png_metadata(source, output).empty());
    const auto before = nekomote::asset_io::load_png(source);
    const auto after = nekomote::asset_io::load_png(output);
    REQUIRE(before.ok);
    REQUIRE(after.ok);
    REQUIRE(after.image.pixels == before.image.pixels);
    REQUIRE(after.region.source_width == before.region.source_width);
    REQUIRE(std::filesystem::file_size(output) < std::filesystem::file_size(source));

    // A corrupted chunk CRC must be rejected, not silently rewritten.
    auto corrupted = valid;
    corrupted[8 + 25 + 4 + 12] ^= 0xFF;
    {
        std::ofstream stream(source, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(corrupted.data()),
                     static_cast<std::streamsize>(corrupted.size()));
    }
    const auto output2 = directory.path / "stripped2.png";
    REQUIRE_FALSE(nekomote::asset_io::strip_png_metadata(source, output2).empty());
    REQUIRE_FALSE(std::filesystem::exists(output2));
}
