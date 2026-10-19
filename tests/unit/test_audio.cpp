#include <catch2/catch_test_macros.hpp>
#include <nekomote/asset_io/audio.hpp>
#include <nekomote/asset_io/audio_import.hpp>

#include <FLAC/stream_decoder.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
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

namespace
{
struct NativePcm
{
    std::vector<std::int32_t> samples;
    bool failed{false};
};
FLAC__StreamDecoderWriteStatus capture_pcm(const FLAC__StreamDecoder*, const FLAC__Frame* frame,
                                           const FLAC__int32* const pcm[], void* data)
{
    auto& capture = *static_cast<NativePcm*>(data);
    try
    {
        for (unsigned f = 0; f < frame->header.blocksize; ++f)
            for (unsigned c = 0; c < frame->header.channels; ++c)
                capture.samples.push_back(pcm[c][f]);
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }
    catch (...)
    {
        capture.failed = true;
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
}
void capture_error(const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void* data)
{
    static_cast<NativePcm*>(data)->failed = true;
}
Bytes read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}
} // namespace
TEST_CASE("FLAC conversion preserves integer PCM and runtime samples/RMS", "[audio][asset]")
{
    File source, encoded;
    for (const auto bits : {8u, 16u, 24u, 32u})
    {
        INFO(bits);
        Bytes pcm;
        std::vector<std::int32_t> expected;
        for (std::uint32_t i = 0; i < 11000; ++i)
        {
            const auto raw = (i * 2654435761u) & (bits == 32 ? 0xffffffffu : (1u << bits) - 1);
            put(pcm, raw, static_cast<int>(bits / 8));
            auto signed_raw = raw;
            if (bits > 8 && bits < 32 && (raw & (1u << (bits - 1))) != 0)
                signed_raw |= ~((1u << bits) - 1);
            std::int32_t value;
            std::memcpy(&value, &signed_raw, sizeof(value));
            expected.push_back(bits == 8 ? static_cast<std::int32_t>(raw) - 128 : value);
        }
        source.write(wave(bits, pcm));
        std::filesystem::remove(encoded.path);
        const auto error = nekomote::asset_io::transcode_wave_to_flac(source.path, encoded.path);
        INFO(error);
        REQUIRE(error.empty());
        const auto original = nekomote::asset_io::load_wave(source.path);
        const auto loaded = nekomote::asset_io::load_flac(encoded.path);
        INFO(loaded.error);
        REQUIRE(original.ok);
        REQUIRE(loaded.ok);
        REQUIRE(loaded.clip.sample_rate == original.clip.sample_rate);
        REQUIRE(loaded.clip.channels == original.clip.channels);
        REQUIRE(loaded.clip.samples == original.clip.samples);
        REQUIRE(loaded.clip.rms == original.clip.rms);
        // Float playback cannot represent every 32-bit integer. Verify the
        // encoded source PCM separately so an accidental quantization cannot pass.
        std::unique_ptr<FLAC__StreamDecoder, decltype(&FLAC__stream_decoder_delete)> decoder(
            FLAC__stream_decoder_new(), FLAC__stream_decoder_delete);
        REQUIRE(decoder);
        NativePcm native;
        REQUIRE(FLAC__stream_decoder_init_file(decoder.get(), encoded.path.c_str(), capture_pcm,
                                               nullptr, capture_error,
                                               &native) == FLAC__STREAM_DECODER_INIT_STATUS_OK);
        REQUIRE(FLAC__stream_decoder_process_until_end_of_stream(decoder.get()));
        REQUIRE(FLAC__stream_decoder_finish(decoder.get()));
        REQUIRE_FALSE(native.failed);
        REQUIRE(native.samples == expected);
    }
}

TEST_CASE("FLAC stereo decoding preserves RMS across block/window boundaries", "[audio][asset]")
{
    File source, encoded;
    Bytes pcm;
    for (std::uint32_t i = 0; i < 10002; ++i)
        put(pcm, i * 37u, 2);
    auto b = wave(16, pcm);
    b[32] = 2;
    b[42] = 4;
    const std::uint32_t rate = 44100 * 4;
    for (int i = 0; i < 4; ++i)
        b[static_cast<std::size_t>(38 + i)] = static_cast<std::uint8_t>(rate >> (8 * i));
    source.write(b);
    REQUIRE(nekomote::asset_io::transcode_wave_to_flac(source.path, encoded.path).empty());
    const auto original = nekomote::asset_io::load_wave(source.path);
    const auto loaded = nekomote::asset_io::load_flac(encoded.path);
    REQUIRE(original.ok);
    REQUIRE(loaded.ok);
    REQUIRE(loaded.clip.channels == 2);
    REQUIRE(loaded.clip.samples == original.clip.samples);
    REQUIRE(loaded.clip.rms == original.clip.rms);
}

TEST_CASE("FLAC rejects corrupt frames, invalid counts and excessive decoded size",
          "[audio][asset]")
{
    File source, encoded;
    Bytes pcm;
    for (std::uint32_t i = 0; i < 12000; ++i)
        put(pcm, i * 7919u, 2);
    source.write(wave(16, pcm));
    REQUIRE(nekomote::asset_io::transcode_wave_to_flac(source.path, encoded.path).empty());
    auto b = read_file(encoded.path);
    REQUIRE(b.size() > 50);
    SECTION("truncated frame")
    {
        b.resize(b.size() - 7);
    }
    SECTION("frame corruption")
    {
        b.back() ^= 1;
    }
    SECTION("PCM checksum mismatch")
    {
        b[26] ^= 1; // STREAMINFO MD5, independently checked after decoding.
    }
    SECTION("zero sample count")
    {
        b[21] &= 0xf0;
        std::fill(b.begin() + 22, b.begin() + 26, 0);
    }
    SECTION("decoded PCM limit")
    {
        b[21] |= 15;
        std::fill(b.begin() + 22, b.begin() + 26, 255);
    }
    SECTION("invalid channel count")
    {
        b[20] = static_cast<std::uint8_t>((b[20] & 0xf1) | (2 << 1)); // Three channels.
    }
    SECTION("sample count exceeds actual frames")
    {
        ++b[25];
    }
    encoded.write(b);
    const auto loaded = nekomote::asset_io::load_flac(encoded.path);
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.clip.samples.empty());
    REQUIRE(loaded.error.find(encoded.path.string()) != std::string::npos);
}

TEST_CASE("FLAC converter rejects float and malformed WAV without replacing output",
          "[audio][asset]")
{
    File source, encoded;
    auto b = wave(32, {0, 0, 128, 62});
    SECTION("float source")
    {
        b[30] = 3;
    }
    SECTION("truncated source")
    {
        b.pop_back();
    }
    source.write(b);
    const Bytes sentinel{1, 2, 3};
    encoded.write(sentinel);
    const auto error = nekomote::asset_io::transcode_wave_to_flac(source.path, encoded.path);
    REQUIRE_FALSE(error.empty());
    REQUIRE(error.find(source.path.string()) != std::string::npos);
    REQUIRE(read_file(encoded.path) == sentinel);
}

TEST_CASE("FLAC publication refuses existing files and links and removes temporary outputs",
          "[audio][asset]")
{
    File source, encoded;
    source.write(wave(16, {0, 0, 0, 64}));
    const Bytes sentinel{1, 2, 3};
    SECTION("existing regular file")
    {
        encoded.write(sentinel);
    }
    SECTION("existing dangling link")
    {
        std::filesystem::create_symlink("missing.flac", encoded.path);
    }
    const auto error = nekomote::asset_io::transcode_wave_to_flac(source.path, encoded.path);
    REQUIRE_FALSE(error.empty());
    if (std::filesystem::is_symlink(encoded.path))
        REQUIRE(std::filesystem::read_symlink(encoded.path) == "missing.flac");
    else
        REQUIRE(read_file(encoded.path) == sentinel);
    const auto prefix = encoded.path.filename().string() + ".tmp-";
    for (const auto& entry : std::filesystem::directory_iterator(encoded.path.parent_path()))
        REQUIRE_FALSE(entry.path().filename().string().starts_with(prefix));
}
