#include <catch2/catch_test_macros.hpp>
#include <nekomote/asset_io/audio.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
using Bytes = std::vector<std::uint8_t>;
void put(Bytes& b, std::uint32_t v, int n)
{
    for (int i = 0; i < n; ++i)
        b.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 255));
}
void tag(Bytes& b, const char* s)
{
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<std::uint8_t>(s[i]));
}
Bytes wave(std::uint32_t bits, const Bytes& pcm)
{
    Bytes b;
    tag(b, "RIFF");
    put(b, 0, 4);
    tag(b, "WAVE");
    tag(b, "JUNK");
    put(b, 1, 4);
    put(b, 7, 1);
    put(b, 0, 1);
    tag(b, "fmt ");
    put(b, 16, 4);
    put(b, 1, 2);
    put(b, 1, 2);
    put(b, 44100, 4);
    put(b, 44100 * (bits / 8), 4);
    put(b, bits / 8, 2);
    put(b, bits, 2);
    tag(b, "data");
    put(b, static_cast<std::uint32_t>(pcm.size()), 4);
    b.insert(b.end(), pcm.begin(), pcm.end());
    if (pcm.size() % 2 != 0)
        put(b, 0, 1);
    const auto size = static_cast<std::uint32_t>(b.size() - 8);
    for (int i = 0; i < 4; ++i)
        b[static_cast<std::size_t>(4 + i)] = static_cast<std::uint8_t>(size >> (i * 8));
    return b;
}
struct File
{
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("nekomote-wave-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");
    ~File()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    void write(const Bytes& b) const
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    }
};
} // namespace
TEST_CASE("PCM WAVE signed samples, padding and RMS are decoded at loading", "[audio][asset]")
{
    File file;
    for (const auto bits : {8u, 16u, 24u, 32u})
    {
        INFO(bits);
        Bytes pcm;
        if (bits == 8)
            pcm = {0, 128, 192};
        else
        {
            put(pcm, std::uint32_t{1} << (bits - 1), static_cast<int>(bits / 8));
            put(pcm, 0, static_cast<int>(bits / 8));
            put(pcm, std::uint32_t{1} << (bits - 2), static_cast<int>(bits / 8));
        }
        file.write(wave(bits, pcm));
        const auto result = nekomote::asset_io::load_wave(file.path);
        REQUIRE(result.ok);
        REQUIRE(result.clip.channels == 1);
        REQUIRE(result.clip.samples == std::vector<float>{-1, 0, .5f});
        REQUIRE(std::abs(result.clip.rms[0] - std::sqrt(1.25f / 3)) < 1e-6f);
    }
}
TEST_CASE("WAVE rejects truncated, compressed and misaligned PCM", "[audio][asset]")
{
    File file;
    auto b = wave(16, {0, 0, 0, 0});
    SECTION("truncation")
    {
        b.pop_back();
    }
    SECTION("compressed")
    {
        b[30] = 2;
    }
    SECTION("bad alignment")
    {
        b[42] = 3;
    }
    SECTION("excess channels")
    {
        b[32] = 3;
    }
    SECTION("bad rate")
    {
        b[34] = 0;
    }
    SECTION("oversized chunk")
    {
        b[26] = 255;
    }
    file.write(b);
    const auto result = nekomote::asset_io::load_wave(file.path);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.clip.samples.empty());
    REQUIRE(result.error.find(file.path.string()) != std::string::npos);
}

TEST_CASE("Float PCM rejects NaN and stereo RMS follows mono downmix", "[audio][asset]")
{
    File file;
    SECTION("float32")
    {
        Bytes pcm;
        put(pcm, 0x3e800000, 4); // 0.25 IEEE float
        put(pcm, 0xbe800000, 4); // -0.25
        auto b = wave(32, pcm);
        b[30] = 3;
        file.write(b);
        auto loaded = nekomote::asset_io::load_wave(file.path);
        REQUIRE(loaded.ok);
        REQUIRE(loaded.clip.samples == std::vector<float>{.25f, -.25f});
        REQUIRE(loaded.clip.rms[0] == .25f);
        b[56] = 0xc0;
        b[57] = 0x7f; // Quiet NaN in first sample.
        file.write(b);
        loaded = nekomote::asset_io::load_wave(file.path);
        REQUIRE_FALSE(loaded.ok);
        REQUIRE(loaded.clip.samples.empty());
    }
    SECTION("stereo")
    {
        auto b = wave(16, {0, 64, 0, 192, 0, 64, 0, 192});
        b[32] = 2;
        b[42] = 4;
        const std::uint32_t rate = 44100 * 4;
        for (int i = 0; i < 4; ++i)
            b[static_cast<std::size_t>(38 + i)] = static_cast<std::uint8_t>(rate >> (8 * i));
        file.write(b);
        const auto loaded = nekomote::asset_io::load_wave(file.path);
        REQUIRE(loaded.ok);
        REQUIRE(loaded.clip.channels == 2);
        REQUIRE(loaded.clip.rms[0] == 0.0f);
    }
}
