#include <nekomote/asset_io/audio.hpp>

#include <FLAC/stream_decoder.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace nekomote::asset_io
{
namespace
{
constexpr std::uintmax_t kMaxSourceBytes = 64 * 1024 * 1024;
constexpr std::uint64_t kMaxPcmSamples = 256 * 1024 * 1024 / sizeof(float);
struct DecoderState
{
    model::AudioClip clip;
    std::uint64_t frames{0};
    std::uint64_t written{0};
    std::uint32_t bits{0};
    const char* error{nullptr};
};
void metadata(const FLAC__StreamDecoder*, const FLAC__StreamMetadata* metadata, void* context)
{
    auto& state = *static_cast<DecoderState*>(context);
    if (metadata->type != FLAC__METADATA_TYPE_STREAMINFO)
        return;
    const auto& info = metadata->data.stream_info;
    if (state.frames != 0 || info.channels < 1 || info.channels > 2 || info.sample_rate < 8000 ||
        info.sample_rate > 192000 ||
        (info.bits_per_sample != 8 && info.bits_per_sample != 16 && info.bits_per_sample != 24 &&
         info.bits_per_sample != 32) ||
        info.total_samples == 0 || info.total_samples > kMaxPcmSamples / info.channels)
    {
        state.error = "unsupported FLAC metadata or decoded PCM exceeds 256 MiB";
        return;
    }
    try
    {
        state.frames = info.total_samples;
        state.bits = info.bits_per_sample;
        state.clip.channels = info.channels;
        state.clip.sample_rate = info.sample_rate;
        state.clip.samples.resize(static_cast<std::size_t>(state.frames * info.channels));
    }
    catch (...)
    {
        state.error = "cannot allocate decoded FLAC PCM";
    }
}
FLAC__StreamDecoderWriteStatus write_frame(const FLAC__StreamDecoder*, const FLAC__Frame* frame,
                                           const FLAC__int32* const channels[], void* context)
{
    auto& state = *static_cast<DecoderState*>(context);
    if (state.error)
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    if (state.frames == 0 || frame->header.channels != state.clip.channels ||
        frame->header.sample_rate != state.clip.sample_rate ||
        frame->header.bits_per_sample != state.bits ||
        frame->header.blocksize > state.frames - state.written)
    {
        state.error = "FLAC frame disagrees with stream metadata";
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    const double scale = std::ldexp(1.0, -static_cast<int>(state.bits - 1));
    for (std::uint32_t f = 0; f < frame->header.blocksize; ++f)
        for (std::uint32_t c = 0; c < state.clip.channels; ++c)
            state.clip
                .samples[static_cast<std::size_t>((state.written + f) * state.clip.channels + c)] =
                static_cast<float>(static_cast<double>(channels[c][f]) * scale);
    state.written += frame->header.blocksize;
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}
void decode_error(const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void* context)
{
    static_cast<DecoderState*>(context)->error = "corrupt FLAC frame";
}
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
AudioLoadResult load_flac(const std::filesystem::path& path)
{
    AudioLoadResult result;
    try
    {
        const auto size = std::filesystem::file_size(path);
        if (size < 42 || size > kMaxSourceBytes)
            throw std::runtime_error("FLAC size outside 42 bytes..64 MiB");
        std::unique_ptr<FLAC__StreamDecoder, decltype(&FLAC__stream_decoder_delete)> decoder(
            FLAC__stream_decoder_new(), FLAC__stream_decoder_delete);
        if (!decoder)
            throw std::runtime_error("cannot allocate FLAC decoder");
        DecoderState state;
        if (!FLAC__stream_decoder_set_md5_checking(decoder.get(), true))
            throw std::runtime_error("cannot enable FLAC checksum verification");
        const auto initialized = FLAC__stream_decoder_init_file(
            decoder.get(), path.c_str(), write_frame, metadata, decode_error, &state);
        if (initialized != FLAC__STREAM_DECODER_INIT_STATUS_OK)
            throw std::runtime_error(FLAC__StreamDecoderInitStatusString[initialized]);
        // Stop after metadata before attempting frames: allocation/limit failures
        // must not let the decoder process the rest of an untrusted stream.
        bool processed = FLAC__stream_decoder_process_until_end_of_metadata(decoder.get());
        if (processed && !state.error && state.frames != 0)
            processed = FLAC__stream_decoder_process_until_end_of_stream(decoder.get());
        const bool verified = FLAC__stream_decoder_finish(decoder.get());
        if (state.error)
            throw std::runtime_error(state.error);
        if (!processed || !verified || state.frames == 0 || state.written != state.frames)
            throw std::runtime_error("truncated FLAC stream or PCM checksum mismatch");
        prepare_rms(state.clip);
        result.clip = std::move(state.clip);
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
