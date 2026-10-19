#include <nekomote/asset_io/audio_import.hpp>

#include <FLAC/stream_encoder.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>

namespace nekomote::asset_io
{
namespace
{
constexpr std::size_t kBlockFrames = 4096;
std::uint32_t integer(std::span<const std::uint8_t> bytes, std::size_t p, std::size_t width)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < width; ++i)
        value |= std::uint32_t{bytes[p + i]} << (i * 8);
    return value;
}
struct FileCloser
{
    void operator()(std::FILE* file) const noexcept
    {
        std::fclose(file);
    }
};
FLAC__StreamEncoderWriteStatus encode_write(const FLAC__StreamEncoder*, const FLAC__byte bytes[],
                                            std::size_t count, unsigned, unsigned, void* context)
{
    return std::fwrite(bytes, 1, count, static_cast<std::FILE*>(context)) == count
               ? FLAC__STREAM_ENCODER_WRITE_STATUS_OK
               : FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
}
FLAC__StreamEncoderSeekStatus encode_seek(const FLAC__StreamEncoder*, FLAC__uint64 offset,
                                          void* context)
{
    return ::fseeko(static_cast<std::FILE*>(context), static_cast<off_t>(offset), SEEK_SET) == 0
               ? FLAC__STREAM_ENCODER_SEEK_STATUS_OK
               : FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
}
FLAC__StreamEncoderTellStatus encode_tell(const FLAC__StreamEncoder*, FLAC__uint64* offset,
                                          void* context)
{
    const auto position = ::ftello(static_cast<std::FILE*>(context));
    if (position < 0)
        return FLAC__STREAM_ENCODER_TELL_STATUS_ERROR;
    *offset = static_cast<FLAC__uint64>(position);
    return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}
struct WaveReader
{
    std::ifstream file;
    std::uint32_t channels{0}, rate{0}, bits{0}, encoding{0};
    std::size_t data_bytes{0};
    explicit WaveReader(const std::filesystem::path& path)
        : file(path, std::ios::binary | std::ios::ate)
    {
        if (!file)
            throw std::runtime_error("cannot open WAVE");
        const auto end = file.tellg();
        if (end < 12 || end > 64 * 1024 * 1024)
            throw std::runtime_error("WAVE size outside 12 bytes..64 MiB");
        const auto size = static_cast<std::size_t>(end);
        file.seekg(0);
        std::array<std::uint8_t, 16> header{};
        read(std::span(header).first(12));
        if (std::memcmp(header.data(), "RIFF", 4) != 0 ||
            std::memcmp(header.data() + 8, "WAVE", 4) != 0 || integer(header, 4, 4) != size - 8)
            throw std::runtime_error("invalid RIFF/WAVE envelope");
        bool format_found = false, data_found = false;
        std::size_t data_offset = 0;
        for (std::size_t p = 12; p < size;)
        {
            if (size - p < 8)
                throw std::runtime_error("truncated WAVE chunk");
            file.seekg(static_cast<std::streamoff>(p));
            read(std::span(header).first(8));
            const auto length = integer(header, 4, 4);
            if (length > size - p - 8)
                throw std::runtime_error("truncated WAVE chunk");
            if (std::memcmp(header.data(), "fmt ", 4) == 0)
            {
                if (format_found || length < 16)
                    throw std::runtime_error("duplicate or incomplete WAVE format");
                format_found = true;
                read(header);
                encoding = integer(header, 0, 2);
                channels = integer(header, 2, 2);
                rate = integer(header, 4, 4);
                bits = integer(header, 14, 2);
                if (channels < 1 || channels > 2 || rate < 8000 || rate > 192000 ||
                    !((encoding == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) ||
                      (encoding == 3 && bits == 32)))
                    throw std::runtime_error(
                        "unsupported WAVE encoding, channel count or sample rate");
                const auto alignment = channels * (bits / 8);
                if (integer(header, 12, 2) != alignment ||
                    integer(header, 8, 4) != rate * alignment)
                    throw std::runtime_error("inconsistent PCM alignment/rate");
            }
            else if (std::memcmp(header.data(), "data", 4) == 0)
            {
                if (data_found)
                    throw std::runtime_error("duplicate WAVE data");
                data_found = true;
                data_offset = p + 8;
                data_bytes = length;
            }
            p += 8 + length;
            if (length % 2 != 0)
            {
                if (p == size)
                    throw std::runtime_error("missing WAVE chunk padding");
                ++p;
            }
        }
        if (!format_found || !data_found || data_bytes == 0)
            throw std::runtime_error("missing WAVE format or PCM data");
        if (data_bytes % (channels * (bits / 8)) != 0)
            throw std::runtime_error("inconsistent PCM alignment/rate");
        file.seekg(static_cast<std::streamoff>(data_offset));
    }
    void read(std::span<std::uint8_t> bytes)
    {
        if (!file.read(reinterpret_cast<char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("cannot read WAVE");
    }
    std::size_t samples() const
    {
        return data_bytes / (bits / 8);
    }
    std::int32_t signed_sample(std::span<const std::uint8_t> bytes, std::size_t position) const
    {
        auto raw = integer(bytes, position, bits / 8);
        if (bits == 8)
            return static_cast<std::int32_t>(raw) - 128;
        if (bits < 32 && (raw & (std::uint32_t{1} << (bits - 1))) != 0)
            raw |= ~((std::uint32_t{1} << bits) - 1);
        std::int32_t sample;
        std::memcpy(&sample, &raw, sizeof(sample));
        return sample;
    }
};
void prepare_rms(model::AudioClip& clip)
{
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
}
} // namespace
AudioLoadResult load_wave(const std::filesystem::path& path)
{
    AudioLoadResult result;
    try
    {
        WaveReader input(path);
        auto& clip = result.clip;
        clip.channels = input.channels;
        clip.sample_rate = input.rate;
        clip.samples.resize(input.samples());
        std::array<std::uint8_t, kBlockFrames * 2 * 4> block{};
        const auto width = input.bits / 8;
        const auto scale = std::ldexp(1.0, -static_cast<int>(input.bits - 1));
        for (std::size_t position = 0; position < clip.samples.size();)
        {
            const auto count = std::min(clip.samples.size() - position, block.size() / width);
            input.read(std::span(block).first(count * width));
            for (std::size_t i = 0; i < count; ++i)
            {
                double sample;
                if (input.encoding == 3)
                {
                    const auto raw = integer(block, i * width, width);
                    float decoded;
                    std::memcpy(&decoded, &raw, sizeof(decoded));
                    sample = decoded;
                }
                else
                    sample = static_cast<double>(input.signed_sample(block, i * width)) * scale;
                if (!std::isfinite(sample) || sample < -1 || sample > 1)
                    throw std::runtime_error("PCM sample outside finite -1..1");
                clip.samples[position + i] = static_cast<float>(sample);
            }
            position += count;
        }
        prepare_rms(clip);
        result.ok = true;
    }
    catch (const std::exception& error)
    {
        result.clip = {};
        result.error = path.string() + ": " + error.what();
    }
    return result;
}
std::string transcode_wave_to_flac(const std::filesystem::path& source,
                                   const std::filesystem::path& destination)
{
    std::filesystem::path temporary;
    bool temporary_owned = false;
    try
    {
        WaveReader input(source);
        if (input.encoding != 1)
            throw std::runtime_error(
                "lossless FLAC conversion requires integer PCM; float WAVE is unsupported");
        if (std::filesystem::absolute(source).lexically_normal() ==
                std::filesystem::absolute(destination).lexically_normal() ||
            (std::filesystem::exists(destination) &&
             std::filesystem::equivalent(source, destination)))
            throw std::runtime_error("source and destination must differ");
        std::unique_ptr<std::FILE, FileCloser> output;
        std::unique_ptr<FLAC__StreamEncoder, decltype(&FLAC__stream_encoder_delete)> encoder(
            FLAC__stream_encoder_new(), FLAC__stream_encoder_delete);
        if (!encoder)
            throw std::runtime_error("cannot allocate FLAC encoder");
        if (!FLAC__stream_encoder_set_verify(encoder.get(), true) ||
            !FLAC__stream_encoder_set_channels(encoder.get(), input.channels) ||
            !FLAC__stream_encoder_set_bits_per_sample(encoder.get(), input.bits) ||
            !FLAC__stream_encoder_set_sample_rate(encoder.get(), input.rate) ||
            !FLAC__stream_encoder_set_compression_level(encoder.get(), 8) ||
            !FLAC__stream_encoder_set_total_samples_estimate(encoder.get(),
                                                             input.samples() / input.channels))
            throw std::runtime_error("cannot configure lossless FLAC encoder");
        temporary = destination;
        temporary +=
            ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        // Exclusive creation prevents a colliding temporary path or link from
        // being overwritten. The encoder callbacks borrow this owned FILE.
        output.reset(std::fopen(temporary.c_str(), "w+bx"));
        if (!output)
            throw std::runtime_error("cannot exclusively create temporary FLAC output");
        temporary_owned = true;
        const auto initialized = FLAC__stream_encoder_init_stream(
            encoder.get(), encode_write, encode_seek, encode_tell, nullptr, output.get());
        if (initialized != FLAC__STREAM_ENCODER_INIT_STATUS_OK)
            throw std::runtime_error(FLAC__StreamEncoderInitStatusString[initialized]);
        std::array<std::uint8_t, kBlockFrames * 2 * 4> bytes{};
        std::array<FLAC__int32, kBlockFrames * 2> pcm{};
        const auto width = input.bits / 8;
        for (std::size_t remaining = input.samples(); remaining != 0;)
        {
            const auto count = std::min(remaining, kBlockFrames * input.channels);
            input.read(std::span(bytes).first(count * width));
            for (std::size_t i = 0; i < count; ++i)
                pcm[i] = input.signed_sample(bytes, i * width);
            if (!FLAC__stream_encoder_process_interleaved(
                    encoder.get(), pcm.data(), static_cast<unsigned>(count / input.channels)))
                throw std::runtime_error(
                    FLAC__StreamEncoderStateString[FLAC__stream_encoder_get_state(encoder.get())]);
            remaining -= count;
        }
        if (!FLAC__stream_encoder_finish(encoder.get()))
            throw std::runtime_error("cannot finish or verify FLAC encoding");
        if (std::fclose(output.release()) != 0)
            throw std::runtime_error("cannot flush FLAC output");
        // Both paths are siblings on one filesystem. Hard-link publication is
        // atomic and refuses an existing destination, including dangling links.
        std::filesystem::create_hard_link(temporary, destination);
        std::filesystem::remove(temporary);
        temporary_owned = false;
        return {};
    }
    catch (const std::exception& error)
    {
        if (temporary_owned)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
        return source.string() + ": " + error.what();
    }
}
} // namespace nekomote::asset_io
