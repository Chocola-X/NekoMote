#include <nekomote/asset_io/audio.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <span>
#include <stdexcept>

namespace nekomote::asset_io
{
namespace
{
using Bytes = std::span<const std::uint8_t>;
std::uint32_t integer(Bytes b, std::size_t position, std::size_t width)
{
    if (position > b.size() || width > b.size() - position)
        throw std::runtime_error("truncated WAVE field");
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < width; ++i)
        value |= std::uint32_t{b[position + i]} << (i * 8);
    return value;
}
bool tag(Bytes b, std::size_t p, const char* text)
{
    return p <= b.size() && b.size() - p >= 4 &&
           std::equal(text, text + 4, b.begin() + static_cast<std::ptrdiff_t>(p));
}
} // namespace
AudioLoadResult load_wave(const std::filesystem::path& path)
{
    AudioLoadResult result;
    try
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            throw std::runtime_error("cannot open WAVE");
        const auto size = file.tellg();
        if (size < 12 || size > 64 * 1024 * 1024)
            throw std::runtime_error("WAVE size outside 12 bytes..64 MiB");
        std::vector<std::uint8_t> storage(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(storage.data()), size))
            throw std::runtime_error("cannot read WAVE");
        const Bytes b(storage);
        if (!tag(b, 0, "RIFF") || !tag(b, 8, "WAVE") || integer(b, 4, 4) != b.size() - 8)
            throw std::runtime_error("invalid RIFF/WAVE envelope");
        Bytes format, data;
        bool found_data = false;
        for (std::size_t p = 12; p < b.size();)
        {
            const auto length = integer(b, p + 4, 4);
            if (b.size() - p < 8 || length > b.size() - p - 8)
                throw std::runtime_error("truncated WAVE chunk");
            const auto chunk = b.subspan(p + 8, length);
            if (tag(b, p, "fmt "))
            {
                if (!format.empty())
                    throw std::runtime_error("duplicate WAVE format");
                format = chunk;
            }
            else if (tag(b, p, "data"))
            {
                if (found_data)
                    throw std::runtime_error("duplicate WAVE data");
                found_data = true;
                data = chunk;
            }
            p += 8 + length;
            if (length % 2 != 0)
            {
                if (p == b.size())
                    throw std::runtime_error("missing WAVE chunk padding");
                ++p;
            }
        }
        if (format.size() < 16 || !found_data || data.empty())
            throw std::runtime_error("missing WAVE format or PCM data");
        const auto encoding = integer(format, 0, 2);
        auto& clip = result.clip;
        clip.channels = integer(format, 2, 2);
        clip.sample_rate = integer(format, 4, 4);
        const auto bits = integer(format, 14, 2);
        if (clip.channels < 1 || clip.channels > 2 || clip.sample_rate < 8000 ||
            clip.sample_rate > 192000 ||
            !((encoding == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) ||
              (encoding == 3 && bits == 32)))
            throw std::runtime_error("unsupported WAVE encoding, channel count or sample rate");
        const auto bytes = bits / 8;
        const auto alignment = clip.channels * bytes;
        if (integer(format, 12, 2) != alignment ||
            integer(format, 8, 4) != clip.sample_rate * alignment || data.size() % alignment != 0)
            throw std::runtime_error("inconsistent PCM alignment/rate");
        clip.samples.resize(data.size() / bytes);
        for (std::size_t i = 0; i < clip.samples.size(); ++i)
        {
            auto raw = integer(data, i * bytes, bytes);
            double sample;
            if (encoding == 3)
                sample = std::bit_cast<float>(raw);
            else if (bits == 8)
                sample = (static_cast<double>(raw) - 128) / 128;
            else
            {
                if (bits < 32 && (raw & (std::uint32_t{1} << (bits - 1))) != 0)
                    raw |= ~((std::uint32_t{1} << bits) - 1);
                sample = static_cast<double>(std::bit_cast<std::int32_t>(raw)) /
                         std::ldexp(1.0, static_cast<int>(bits - 1));
            }
            if (!std::isfinite(sample) || sample < -1 || sample > 1)
                throw std::runtime_error("PCM sample outside finite -1..1");
            clip.samples[i] = static_cast<float>(sample);
        }
        const auto frames = clip.samples.size() / clip.channels;
        clip.rms.resize((frames + model::kAudioRmsFrames - 1) / model::kAudioRmsFrames);
        for (std::size_t window = 0; window < clip.rms.size(); ++window)
        {
            const auto begin = window * model::kAudioRmsFrames;
            const auto end = std::min(frames, begin + model::kAudioRmsFrames);
            double squared = 0;
            for (std::size_t f = begin; f < end; ++f)
            {
                double mono = 0;
                for (std::uint32_t c = 0; c < clip.channels; ++c)
                    mono += static_cast<double>(clip.samples[f * clip.channels + c]);
                mono /= clip.channels;
                squared += mono * mono;
            }
            clip.rms[window] =
                static_cast<float>(std::sqrt(squared / static_cast<double>(end - begin)));
        }
        result.ok = true;
    }
    catch (const std::exception& error)
    {
        result.clip = {};
        result.error = path.string() + ": " + error.what();
    }
    return result;
}
} // namespace nekomote::asset_io
