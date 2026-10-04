#include <nekomote/audio/device.hpp>

#include <pulse/pulseaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace nekomote::audio
{
namespace
{
constexpr std::size_t kCommands = 32;
void release(pa_operation* operation) noexcept
{
    if (operation)
        pa_operation_unref(operation);
}
} // namespace
struct Device::Impl
{
    std::vector<model::AudioClip> clips;
    std::array<application::AudioCommand, kCommands> commands{};
    std::atomic<std::size_t> read{0}, write{0};
    std::atomic<bool> quitting{false};
    std::atomic<DeviceState> state{DeviceState::Connecting};
    std::atomic<float> rms{0};
    std::atomic<int> error{0};
    std::thread worker;
    // Worker-owned, never accessed from the evaluation thread.
    pa_mainloop* loop{nullptr};
    pa_context* context{nullptr};
    pa_stream* stream{nullptr};
    const model::AudioClip* clip{nullptr};
    std::size_t sample_offset{0};
    bool paused{false}, stream_paused{false};
    explicit Impl(std::vector<model::AudioClip> data) : clips(std::move(data))
    {
        for (const auto& value : clips)
        {
            if (value.sample_rate < 8000 || value.sample_rate > 192000 || value.channels < 1 ||
                value.channels > 2 || value.samples.empty() ||
                value.samples.size() % value.channels != 0 ||
                value.rms.size() !=
                    (value.samples.size() / value.channels + model::kAudioRmsFrames - 1) /
                        model::kAudioRmsFrames ||
                std::any_of(value.samples.begin(), value.samples.end(),
                            [](float v) { return !std::isfinite(v) || v < -1 || v > 1; }) ||
                std::any_of(value.rms.begin(), value.rms.end(),
                            [](float v) { return !std::isfinite(v) || v < 0 || v > 1; }))
                throw std::invalid_argument("invalid decoded audio bank");
        }
        worker = std::thread([this] { run(); });
    }
    ~Impl()
    {
        quitting.store(true, std::memory_order_release);
        if (worker.joinable())
            worker.join();
    }
    void fail(int code) noexcept
    {
        error.store(code == 0 ? PA_ERR_INTERNAL : code, std::memory_order_relaxed);
        rms.store(0, std::memory_order_relaxed);
        state.store(DeviceState::Failed, std::memory_order_release);
    }
    void close_stream() noexcept
    {
        if (stream)
        {
            pa_stream_set_write_callback(stream, nullptr, nullptr);
            pa_stream_disconnect(stream);
            pa_stream_unref(stream);
            stream = nullptr;
        }
        clip = nullptr;
        sample_offset = 0;
        rms.store(0, std::memory_order_relaxed);
    }
    void write_pcm(std::size_t requested) noexcept
    {
        if (!stream || !clip || paused || sample_offset == clip->samples.size())
            return;
        const auto frame_bytes = clip->channels * sizeof(float);
        const auto count = std::min(requested / frame_bytes * clip->channels,
                                    clip->samples.size() - sample_offset);
        if (count == 0)
            return;
        if (pa_stream_write(stream, clip->samples.data() + sample_offset, count * sizeof(float),
                            nullptr, 0, PA_SEEK_RELATIVE) < 0)
            fail(pa_context_errno(context));
        else
            sample_offset += count;
    }
    static void requested(pa_stream*, std::size_t bytes, void* userdata) noexcept
    {
        static_cast<Impl*>(userdata)->write_pcm(bytes);
    }
    void command(application::AudioCommand value) noexcept
    {
        using application::AudioCommandKind;
        if (value.kind == AudioCommandKind::Stop)
        {
            close_stream();
            state.store(DeviceState::Ready, std::memory_order_release);
        }
        else if (value.kind == AudioCommandKind::Play)
        {
            close_stream();
            clip = &clips[value.clip.value - 1];
            const pa_sample_spec spec{PA_SAMPLE_FLOAT32NE, clip->sample_rate,
                                      static_cast<std::uint8_t>(clip->channels)};
            stream = pa_stream_new(context, "NekoMote voice", &spec, nullptr);
            if (!stream)
            {
                fail(pa_context_errno(context));
                return;
            }
            pa_stream_set_write_callback(stream, requested, this);
            // No prebuffer deadlock for short clips. Server timing includes the
            // sink latency; lip RMS follows pa_stream_get_time, never wall time.
            const auto frame_bytes = clip->channels * sizeof(float);
            const auto length = static_cast<std::uint32_t>(clip->sample_rate * frame_bytes / 25);
            const pa_buffer_attr attr{length * 2, length, 0,
                                      static_cast<std::uint32_t>(512 * frame_bytes), 0};
            const auto flags = static_cast<pa_stream_flags_t>(
                PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_INTERPOLATE_TIMING |
                PA_STREAM_ADJUST_LATENCY | (paused ? PA_STREAM_START_CORKED : PA_STREAM_NOFLAGS));
            if (pa_stream_connect_playback(stream, nullptr, &attr, flags, nullptr, nullptr) < 0)
                fail(pa_context_errno(context));
            else
            {
                stream_paused = paused;
                state.store(paused ? DeviceState::Paused : DeviceState::Playing,
                            std::memory_order_release);
            }
        }
        else
        {
            paused = value.kind == AudioCommandKind::Pause;
            if (stream && pa_stream_get_state(stream) == PA_STREAM_READY)
            {
                const auto operation =
                    pa_stream_cork(stream, paused ? 1 : 0, operation_result, this);
                if (!operation)
                    fail(pa_context_errno(context));
                release(operation);
                stream_paused = paused;
            }
            if (state.load(std::memory_order_relaxed) == DeviceState::Failed)
                return;
            rms.store(0, std::memory_order_relaxed);
            state.store(stream ? (paused ? DeviceState::Paused : DeviceState::Playing)
                               : DeviceState::Ready,
                        std::memory_order_release);
        }
    }
    static void operation_result(pa_stream* stream, int success, void* userdata) noexcept
    {
        auto& self = *static_cast<Impl*>(userdata);
        if (stream == self.stream && !success)
            self.fail(pa_context_errno(self.context));
    }
    void observe() noexcept
    {
        if (!stream || !clip)
            return;
        const auto stream_state = pa_stream_get_state(stream);
        if (stream_state == PA_STREAM_FAILED || stream_state == PA_STREAM_TERMINATED)
        {
            fail(pa_context_errno(context));
            return;
        }
        if (stream_state != PA_STREAM_READY)
            return;
        // Pause commands can arrive while a stream is still connecting.
        if (stream_paused != paused)
        {
            const auto operation = pa_stream_cork(stream, paused ? 1 : 0, operation_result, this);
            if (!operation)
                fail(pa_context_errno(context));
            release(operation);
            stream_paused = paused;
        }
        if (paused)
            return;
        const auto writable = pa_stream_writable_size(stream);
        if (writable != static_cast<std::size_t>(-1))
            write_pcm(writable);
        if (state.load(std::memory_order_relaxed) == DeviceState::Failed)
            return;
        pa_usec_t usec = 0;
        if (pa_stream_get_time(stream, &usec) != 0)
            return;
        const auto frame =
            static_cast<std::uint64_t>(static_cast<double>(usec) * clip->sample_rate / 1000000);
        if (frame >= clip->samples.size() / clip->channels)
        {
            close_stream();
            state.store(DeviceState::Ready, std::memory_order_release);
        }
        else
            rms.store(clip->rms[static_cast<std::size_t>(frame / model::kAudioRmsFrames)],
                      std::memory_order_relaxed);
    }
    void run() noexcept
    {
        loop = pa_mainloop_new();
        if (loop)
            context = pa_context_new(pa_mainloop_get_api(loop), "NekoMote");
        if (!context || pa_context_connect(context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0)
            fail(context ? pa_context_errno(context) : PA_ERR_INTERNAL);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!quitting.load(std::memory_order_acquire) &&
               state.load(std::memory_order_acquire) != DeviceState::Failed)
        {
            int return_code = 0;
            if (pa_mainloop_iterate(loop, 0, &return_code) < 0)
            {
                fail(PA_ERR_INTERNAL);
                break;
            }
            if (state.load(std::memory_order_acquire) == DeviceState::Failed)
                break;
            const auto connection = pa_context_get_state(context);
            if (connection == PA_CONTEXT_FAILED || connection == PA_CONTEXT_TERMINATED)
            {
                fail(pa_context_errno(context));
                break;
            }
            if (connection == PA_CONTEXT_READY)
            {
                if (state.load(std::memory_order_relaxed) == DeviceState::Connecting)
                    state.store(DeviceState::Ready, std::memory_order_release);
                auto position = read.load(std::memory_order_relaxed);
                const auto end = write.load(std::memory_order_acquire);
                while (position != end)
                {
                    command(commands[position % kCommands]);
                    ++position;
                    read.store(position, std::memory_order_release);
                    if (state.load(std::memory_order_relaxed) == DeviceState::Failed)
                        break;
                }
                observe();
            }
            else if (std::chrono::steady_clock::now() > deadline)
                fail(PA_ERR_TIMEOUT);
            std::this_thread::sleep_for(std::chrono::milliseconds(stream ? 4 : 12));
        }
        close_stream();
        if (context)
        {
            pa_context_disconnect(context);
            pa_context_unref(context);
        }
        if (loop)
            pa_mainloop_free(loop);
    }
};
Device::Device(std::vector<model::AudioClip> clips)
    : impl_(std::make_unique<Impl>(std::move(clips)))
{
}
Device::~Device() = default;
bool Device::submit(application::AudioCommand command) noexcept
{
    const auto kind = command.kind;
    if (kind != application::AudioCommandKind::Play &&
        kind != application::AudioCommandKind::Stop &&
        kind != application::AudioCommandKind::Pause &&
        kind != application::AudioCommandKind::Resume)
        return false;
    if (kind == application::AudioCommandKind::Play &&
        (!command.clip.valid() || command.clip.value > impl_->clips.size()))
        return false;
    if (impl_->state.load(std::memory_order_acquire) == DeviceState::Failed)
        return false;
    const auto position = impl_->write.load(std::memory_order_relaxed);
    if (position - impl_->read.load(std::memory_order_acquire) == kCommands)
        return false;
    impl_->commands[position % kCommands] = command;
    impl_->write.store(position + 1, std::memory_order_release);
    return true;
}
Snapshot Device::snapshot() const noexcept
{
    const auto state = impl_->state.load(std::memory_order_acquire);
    return {state, state == DeviceState::Playing ? impl_->rms.load(std::memory_order_relaxed) : 0,
            impl_->error.load(std::memory_order_relaxed)};
}
} // namespace nekomote::audio
