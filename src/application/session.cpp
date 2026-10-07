#include <nekomote/application/session.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <utility>

namespace nekomote::application
{
namespace
{
constexpr std::array<std::string_view, 8> kGazeVariables{"face_eye_LR", "face_eye_UD", "head_slant",
                                                         "head_LR",     "head_UD",     "body_slant",
                                                         "body_LR",     "body_UD"};
constexpr float kEyeFrames = 30, kHeadFrames = 60, kBodyFrames = 120;
constexpr double kHeadDistanceSquared = 60 * 60, kBodyDistanceSquared = 120 * 120;
constexpr double kReturnTicks = 60;
constexpr float kLipFrames = 4.2f, kLipStopFrames = 7.2f, kDiffFadeOutFrames = 18;
constexpr double kLipThreshold = 0.015, kLipGain = 50, kLipRetention = 0.62;
constexpr std::size_t kAudioCommands = 8;
struct BackgroundLayer
{
    TextureId texture{0};
    int width{0}, height{0};
};
void fill_background_quad(RenderVertex* v, std::uint32_t* idx, int ow, int oh, int iw, int ih,
                          float alpha, std::uint32_t base) noexcept
{
    const auto cover = std::max(static_cast<double>(ow) / iw, static_cast<double>(oh) / ih);
    const auto du = static_cast<float>(ow / (iw * cover) * .5);
    const auto dv = static_cast<float>(oh / (ih * cover) * .5);
    const auto fw = static_cast<float>(ow), fh = static_cast<float>(oh);
    const std::array<float, 4> color{1, 1, 1, alpha};
    v[0] = RenderVertex{0, 0, .5f - du, .5f - dv, color};
    v[1] = RenderVertex{fw, 0, .5f + du, .5f - dv, color};
    v[2] = RenderVertex{fw, fh, .5f + du, .5f + dv, color};
    v[3] = RenderVertex{0, fh, .5f - du, .5f + dv, color};
    idx[0] = base;
    idx[1] = base + 1;
    idx[2] = base + 2;
    idx[3] = base;
    idx[4] = base + 2;
    idx[5] = base + 3;
}
struct BoundVariable
{
    ParamId parameter;
    float target, frames, easing;
};
struct Action
{
    TimelineId main;
    std::array<TimelineId, 6> differences;
    std::vector<BoundVariable> variables;
};
struct Beat
{
    std::uint32_t tick;
    Action action;
};
struct Reaction
{
    std::string label;
    Action action, recovery;
    std::vector<Beat> beats;
    AudioClipId audio;
    std::uint32_t duration, recovery_duration;
};
void require(bool condition, const std::string& reason)
{
    if (!condition)
        throw std::runtime_error(reason);
}
SessionStatus checked(runtime::PlaybackStatus status) noexcept
{
    return status ? SessionStatus{} : SessionStatus{SessionError::Playback, status};
}
bool finite(Vec2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}
} // namespace
struct SessionProgram::Impl
{
    std::unique_ptr<const runtime::PlaybackProgram> playback;
    model::InteractionProfile interaction;
    float author_scale{1}, talk_max{5};
    TimelineId idle;
    std::array<ParamId, kGazeVariables.size()> gaze_parameters;
    VariableSourceId gaze, lip;
    ParamId talk;
    std::vector<Reaction> reactions{1};
    std::array<std::vector<ReactionId>, model::kTouchZoneCount> zones;
    std::vector<std::string> audio;
    bool mirror{false};
};
SessionProgram::SessionProgram(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
}
SessionProgram::~SessionProgram() = default;
const runtime::PlaybackProgram& SessionProgram::playback() const noexcept
{
    return *impl_->playback;
}
std::span<const std::string> SessionProgram::audio_paths() const noexcept
{
    return impl_->audio;
}
ReactionId SessionProgram::find_reaction(std::string_view label) const noexcept
{
    for (std::size_t i = 1; i < impl_->reactions.size(); ++i)
        if (impl_->reactions[i].label == label)
            return ReactionId{static_cast<std::uint32_t>(i)};
    return {};
}
std::string_view SessionProgram::reaction_label(ReactionId id) const noexcept
{
    return id.valid() && id.value < impl_->reactions.size() ? impl_->reactions[id.value].label
                                                            : std::string_view{};
}
bool SessionProgram::gaze_available() const noexcept
{
    return impl_->gaze.valid();
}
bool SessionProgram::touch_available() const noexcept
{
    for (std::size_t i = 0; i < impl_->zones.size(); ++i)
    {
        if (impl_->interaction.regions[i] && !impl_->zones[i].empty())
            return true;
    }
    return false;
}
bool SessionProgram::mirror() const noexcept
{
    return impl_->mirror;
}
SessionCompileResult compile_session(const model::ModelAsset& model,
                                     const model::SessionAsset& asset)
{
    SessionCompileResult result;
    try
    {
        auto data = std::make_unique<SessionProgram::Impl>();
        data->interaction = asset.interaction;
        data->author_scale = model.author_scale.has_value() ? model.author_scale.data : 1;
        data->talk_max = asset.talk_max;
        data->mirror = model.mirror;
        require(std::isfinite(data->talk_max) && data->talk_max > 0 && data->talk_max <= 100,
                "invalid talk limit");
        require(!asset.interaction.eye || std::isfinite(*asset.interaction.eye),
                "nonfinite eye marker");
        for (const auto& ellipse : asset.interaction.regions)
            require(!ellipse || (finite(ellipse->center) && finite(ellipse->radius) &&
                                 ellipse->radius.x > 0 && ellipse->radius.y > 0),
                    "invalid touch region");
        auto geometry = runtime::compile_geometry_scene(model);
        require(static_cast<bool>(geometry.scene),
                geometry.errors.empty() ? "geometry compilation failed" : geometry.errors.front());
        std::vector<runtime::VariableSourceDefinition> sources;
        bool gaze = asset.interaction.eye.has_value();
        for (const auto name : kGazeVariables)
            gaze &= geometry.scene->find_variable(name).valid();
        if (gaze)
        {
            sources.push_back({"eyetrack", {}});
            for (const auto name : kGazeVariables)
                sources.back().variables.emplace_back(name);
        }
        if (geometry.scene->find_variable("face_talk").valid())
            sources.push_back({"lipSync", {"face_talk"}});
        geometry = {}; // Binding discovery only; final playback owns its compiled scene.
        auto playback = runtime::compile_playback(model, runtime::PhysicsMode::Enabled, sources);
        require(static_cast<bool>(playback.program),
                playback.errors.empty() ? "playback compilation failed" : playback.errors.front());
        data->playback = std::move(playback.program);
        const auto& program = *data->playback;
        data->gaze = program.animation().find_variable_source("eyetrack");
        data->lip = program.animation().find_variable_source("lipSync");
        data->talk = program.scene().find_variable("face_talk");
        for (std::size_t i = 0; i < kGazeVariables.size(); ++i)
            data->gaze_parameters[i] = program.scene().find_variable(kGazeVariables[i]);
        const auto timeline = [&](const std::string& label)
        {
            const auto id = program.animation().find_timeline(label);
            require(label.empty() || (id.valid() && program.animation().timeline_supported(id)),
                    label + ": unavailable timeline");
            return id;
        };
        data->idle = timeline(asset.idle_timeline);
        const auto action = [&](const model::SessionAction& source)
        {
            Action bound;
            bound.main = timeline(source.main_timeline);
            for (std::size_t i = 0; i < bound.differences.size(); ++i)
                bound.differences[i] = timeline(source.difference_slots[i]);
            for (const auto& v : source.variables)
            {
                const auto parameter = program.scene().find_variable(v.label);
                require(parameter.valid(), v.label + ": unavailable variable");
                require(std::isfinite(v.target) && std::isfinite(v.author_frames) &&
                            v.author_frames >= 0 && std::isfinite(v.easing),
                        v.label + ": invalid request");
                bound.variables.push_back({parameter, v.target, v.author_frames, v.easing});
            }
            return bound;
        };
        require(asset.reactions.size() <= 4096, "too many reactions");
        for (const auto& source : asset.reactions)
        {
            require(!source.label.empty() &&
                        std::none_of(data->reactions.begin() + 1, data->reactions.end(),
                                     [&](const auto& r) { return r.label == source.label; }),
                    "empty/duplicate reaction label");
            require(source.duration_ticks > 0 && source.duration_ticks <= 3600000 &&
                        source.recovery_ticks <= 3600000,
                    source.label + ": invalid duration");
            require(!source.zones.empty() && source.zones.size() <= model::kTouchZoneCount &&
                        source.beats.size() <= 256,
                    source.label + ": invalid zones/beats");
            Reaction reaction{source.label, action(source.action), action(source.recovery), {},
                              {},           source.duration_ticks, source.recovery_ticks};
            for (const auto& beat : source.beats)
            {
                require(
                    beat.author_tick < source.duration_ticks &&
                        (reaction.beats.empty() || beat.author_tick > reaction.beats.back().tick),
                    source.label + ": unordered/unreachable beat");
                reaction.beats.push_back({beat.author_tick, action(beat.action)});
            }
            if (!source.audio_path.empty())
            {
                const auto found =
                    std::find(data->audio.begin(), data->audio.end(), source.audio_path);
                if (found == data->audio.end())
                {
                    data->audio.push_back(source.audio_path);
                    reaction.audio = AudioClipId{static_cast<std::uint32_t>(data->audio.size())};
                }
                else
                    reaction.audio =
                        AudioClipId{static_cast<std::uint32_t>(found - data->audio.begin() + 1)};
            }
            const ReactionId id{static_cast<std::uint32_t>(data->reactions.size())};
            data->reactions.push_back(std::move(reaction));
            std::array<bool, model::kTouchZoneCount> seen{};
            for (const auto zone : source.zones)
            {
                const auto i = static_cast<std::size_t>(zone);
                require(i < seen.size() && !seen[i], source.label + ": invalid/duplicate zone");
                seen[i] = true;
                data->zones[i].push_back(id);
            }
        }
        result.program = std::unique_ptr<const SessionProgram>(new SessionProgram(std::move(data)));
    }
    catch (const std::exception& error)
    {
        result.errors.push_back(error.what());
    }
    return result;
}
struct SessionInstance::Impl
{
    const SessionProgram::Impl& program;
    runtime::PlaybackInstance player;
    SessionConfig config;
    Vec2 origin{}, pointer{}, return_from{};
    float scale{1}, audio_level{0}, lip_value{0};
    bool started{false}, faulted{false}, pointer_inside{false}, pending_gaze{false},
        returning{false};
    std::uint32_t random;
    std::uint64_t return_begin{0}, reaction_begin{0}, recovery_end{0};
    ReactionPhase phase{ReactionPhase::Idle};
    runtime::PlaybackState playback_state{runtime::PlaybackState::Running};
    ReactionId reaction;
    TimelineId main;
    std::array<TimelineId, 6> differences{};
    std::size_t next_beat{0};
    std::array<AudioCommand, kAudioCommands> commands{};
    std::size_t command_head{0}, command_count{0};
    SessionStatus callback_status;
    BackgroundLayer bg_current;
    BackgroundLayer bg_target;
    double bg_fade_progress{1.0};
    double bg_fade_duration{0};
    int output_width{0}, output_height{0};
    mutable std::array<RenderVertex, 8> bg_vertices{};
    mutable std::array<std::uint32_t, 12> bg_indices{};
    mutable std::array<DrawCommand, 2> bg_commands{};
    Impl(const SessionProgram::Impl& data, SessionConfig options)
        : program(data), player(*data.playback, options.animation), config(options),
          random(options.reaction_seed)
    {
        if ((config.pointer != PointerScope::Unavailable &&
             config.pointer != PointerScope::LocalSurface) ||
            (config.audio != AudioMode::Disabled && config.audio != AudioMode::Device))
            throw std::invalid_argument("invalid session capabilities");
    }
    SessionStatus fail(SessionStatus status) noexcept
    {
        faulted = true;
        return status;
    }
    bool enqueue(AudioCommandKind kind, AudioClipId clip = {}) noexcept
    {
        if (config.audio == AudioMode::Disabled)
            return true;
        if (command_count == commands.size())
            return false;
        commands[(command_head + command_count) % commands.size()] = {kind, clip};
        ++command_count;
        return true;
    }
    runtime::PlaybackStatus apply(const Action& action) noexcept
    {
        if (main != action.main)
        {
            if (main.valid())
                if (auto status = player.stop(main); !status)
                    return status;
            main = action.main;
            if (main.valid())
                if (auto status = player.play(main, runtime::TimelinePlayMode::Parallel); !status)
                    return status;
        }
        for (std::size_t i = 0; i < differences.size(); ++i)
            if (differences[i] != action.differences[i])
            {
                if (differences[i].valid())
                    if (auto status = player.fade_out(differences[i], kDiffFadeOutFrames); !status)
                        return status;
                differences[i] = action.differences[i];
                if (differences[i].valid())
                    if (auto status = player.play(differences[i],
                                                  runtime::TimelinePlayMode::AdditiveParallel);
                        !status)
                        return status;
            }
        for (const auto& variable : action.variables)
            if (auto status = player.transition(variable.parameter, variable.target,
                                                variable.frames, variable.easing);
                !status)
                return status;
        return {};
    }
    runtime::PlaybackStatus gaze(Vec2 point) noexcept
    {
        const double total_scale = static_cast<float>(static_cast<double>(scale) *
                                                      static_cast<double>(program.author_scale));
        const double x = static_cast<double>(point.x) - static_cast<double>(origin.x);
        const double y = static_cast<double>(point.y) -
                         (static_cast<double>(*program.interaction.eye) * total_scale +
                          static_cast<double>(origin.y));
        const auto request = [&](std::size_t i, double target, float frames)
        {
            return player.transition_difference(program.gaze, program.gaze_parameters[i],
                                                static_cast<float>(target), frames, -1);
        };
        if (auto status = request(0, x / 3, kEyeFrames); !status)
            return status;
        if (auto status = request(1, y / 3, kEyeFrames); !status)
            return status;
        const double squared = x * x + y * y;
        if (squared > kHeadDistanceSquared)
        {
            if (auto status = request(2, x / 12, kHeadFrames); !status)
                return status;
            if (auto status = request(3, x / 6, kHeadFrames); !status)
                return status;
            if (auto status = request(4, y / 6, kHeadFrames); !status)
                return status;
        }
        if (squared > kBodyDistanceSquared)
        {
            if (auto status = request(5, x / 18, kBodyFrames); !status)
                return status;
            if (auto status = request(6, x / 9, kBodyFrames); !status)
                return status;
            if (auto status = request(7, y / 9, kBodyFrames); !status)
                return status;
        }
        return {};
    }
    runtime::PlaybackStatus tick(std::uint64_t upcoming) noexcept
    {
        if (phase == ReactionPhase::Playing)
        {
            const auto& active = program.reactions[reaction.value];
            while (next_beat < active.beats.size() &&
                   upcoming >= reaction_begin + active.beats[next_beat].tick)
            {
                if (auto status = apply(active.beats[next_beat].action); !status)
                    return status;
                ++next_beat;
            }
            if (upcoming >= reaction_begin + active.duration)
            {
                if (!enqueue(AudioCommandKind::Stop))
                {
                    callback_status = {SessionError::EventQueueFull, {}};
                    return {runtime::PlaybackError::Faulted, {}, {}, {}};
                }
                if (program.lip.valid())
                    if (auto status = player.transition_difference(program.lip, program.talk, 0,
                                                                   kLipStopFrames, -1);
                        !status)
                        return status;
                if (auto status = apply(active.recovery); !status)
                    return status;
                phase = ReactionPhase::Recovering;
                recovery_end = upcoming + active.recovery_duration;
                audio_level = lip_value = 0;
            }
        }
        if (phase == ReactionPhase::Recovering && upcoming >= recovery_end)
        {
            phase = ReactionPhase::Idle;
            reaction = {};
        }
        if (program.gaze.valid() && pointer_inside &&
            config.pointer == PointerScope::LocalSurface && (pending_gaze || returning))
        {
            Vec2 point = pointer;
            if (returning)
            {
                const double t =
                    std::min(1.0, static_cast<double>(upcoming - return_begin) / kReturnTicks);
                const double factor = 1 - std::pow(1 - t, 3);
                point = {static_cast<float>(
                             static_cast<double>(return_from.x) +
                             (static_cast<double>(pointer.x) - static_cast<double>(return_from.x)) *
                                 factor),
                         static_cast<float>(
                             static_cast<double>(return_from.y) +
                             (static_cast<double>(pointer.y) - static_cast<double>(return_from.y)) *
                                 factor)};
                if (t == 1)
                    returning = false;
            }
            if (auto status = gaze(point); !status)
                return status;
            pending_gaze = false;
        }
        if (phase == ReactionPhase::Playing && config.audio == AudioMode::Device &&
            program.lip.valid() && program.reactions[reaction.value].audio.valid())
        {
            const double target =
                std::clamp((static_cast<double>(audio_level) - kLipThreshold) * kLipGain, 0.0,
                           static_cast<double>(program.talk_max));
            lip_value = static_cast<float>(static_cast<double>(lip_value) * kLipRetention +
                                           target * (1 - kLipRetention));
            if (auto status = player.transition_difference(program.lip, program.talk, lip_value,
                                                           kLipFrames, -1);
                !status)
                return status;
        }
        return {};
    }
    static runtime::PlaybackStatus before_tick(void* context, std::uint64_t upcoming) noexcept
    {
        return static_cast<Impl*>(context)->tick(upcoming);
    }
};
SessionInstance::SessionInstance(const SessionProgram& program, SessionConfig config)
    : impl_(std::make_unique<Impl>(*program.impl_, config))
{
}
SessionInstance::~SessionInstance() = default;
SessionStatus SessionInstance::start() noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {SessionError::Faulted, {}};
    if (data.started)
        return {};
    if (data.program.idle.valid())
    {
        if (auto status =
                data.player.play(data.program.idle, runtime::TimelinePlayMode::AdditiveParallel);
            !status)
            return data.fail(checked(status));
        data.differences[3] = data.program.idle;
    }
    data.started = true;
    return {};
}
SessionStatus SessionInstance::set_origin(Vec2 origin) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {SessionError::Faulted, {}};
    const auto status = data.player.set_origin(origin);
    if (status)
        data.origin = origin;
    return checked(status);
}
SessionStatus SessionInstance::set_scale(float scale) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {SessionError::Faulted, {}};
    const auto status = data.player.set_scale(scale);
    if (status)
        data.scale = scale;
    return checked(status);
}
SessionStatus SessionInstance::pointer_move(Vec2 position) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {SessionError::Faulted, {}};
    if (!finite(position))
        return {SessionError::InvalidInput, {}};
    if (data.config.pointer == PointerScope::Unavailable || !data.program.gaze.valid())
        return {SessionError::InputUnavailable, {}};
    data.pointer = position;
    data.pointer_inside = data.pending_gaze = true;
    return {};
}
SessionStatus SessionInstance::pointer_leave() noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {SessionError::Faulted, {}};
    if (data.config.pointer == PointerScope::Unavailable || !data.program.gaze.valid())
        return {SessionError::InputUnavailable, {}};
    data.return_from = data.pointer;
    data.pointer_inside = data.pending_gaze = data.returning = false;
    return {};
}
SessionStatus SessionInstance::pointer_enter(Vec2 position) noexcept
{
    auto& data = *impl_;
    const auto status = pointer_move(position);
    if (!status)
        return status;
    data.returning = true;
    data.return_begin = data.player.tick_count();
    return {};
}
SessionStatus SessionInstance::touch(Vec2 position) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {SessionError::Faulted, {}};
    if (!finite(position))
        return {SessionError::InvalidInput, {}};
    if (data.config.pointer == PointerScope::Unavailable)
        return {SessionError::InputUnavailable, {}};
    if (data.phase != ReactionPhase::Idle)
        return {SessionError::Busy, {}};
    const double total_scale = static_cast<float>(static_cast<double>(data.scale) *
                                                  static_cast<double>(data.program.author_scale));
    const double x =
        (static_cast<double>(position.x) - static_cast<double>(data.origin.x)) / total_scale;
    const double y =
        (static_cast<double>(position.y) - static_cast<double>(data.origin.y)) / total_scale;
    for (std::size_t i = 0; i < data.program.zones.size(); ++i)
    {
        const auto& region = data.program.interaction.regions[i];
        const auto& choices = data.program.zones[i];
        if (!region || choices.empty())
            continue;
        const double dx =
            (x - static_cast<double>(region->center.x)) / static_cast<double>(region->radius.x);
        const double dy =
            (y - static_cast<double>(region->center.y)) / static_cast<double>(region->radius.y);
        if (dx * dx + dy * dy > 1)
            continue;
        // Separate deterministic state from Eye's random sequence.
        std::uint32_t next = data.random + 0x9e3779b9u;
        next ^= next >> 16;
        next *= 0x85ebca6bu;
        next ^= next >> 13;
        data.random = next;
        return react(choices[static_cast<std::size_t>(next) % choices.size()]);
    }
    return {};
}
SessionStatus SessionInstance::react(ReactionId id) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {SessionError::Faulted, {}};
    if (!id.valid() || id.value >= data.program.reactions.size())
        return {SessionError::InvalidReaction, {}};
    if (data.phase != ReactionPhase::Idle)
        return {SessionError::Busy, {}};
    if (data.config.audio == AudioMode::Device && data.command_count == data.commands.size())
        return {SessionError::EventQueueFull, {}};
    if (auto status = start(); !status)
        return status;
    const auto& reaction = data.program.reactions[id.value];
    if (auto status = data.apply(reaction.action); !status)
        return data.fail(checked(status));
    if (reaction.audio.valid())
        data.enqueue(AudioCommandKind::Play, reaction.audio);
    data.reaction = id;
    data.phase = ReactionPhase::Playing;
    data.reaction_begin = data.player.tick_count();
    data.next_beat = 0;
    data.audio_level = data.lip_value = 0;
    return {};
}
SessionStatus SessionInstance::set_audio_level(float rms) noexcept
{
    if (impl_->faulted)
        return {SessionError::Faulted, {}};
    if (!std::isfinite(rms) || rms < 0 || rms > 1)
        return {SessionError::InvalidInput, {}};
    impl_->audio_level = rms;
    return {};
}
SessionStatus SessionInstance::set_playback_state(runtime::PlaybackState state) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {SessionError::Faulted, {}};
    if (state != runtime::PlaybackState::Running && state != runtime::PlaybackState::Paused)
        return {SessionError::InvalidInput, {}};
    if (state == data.playback_state)
        return {};
    if (!data.enqueue(state == runtime::PlaybackState::Paused ? AudioCommandKind::Pause
                                                              : AudioCommandKind::Resume))
        return {SessionError::EventQueueFull, {}};
    data.player.set_playback_state(state);
    data.playback_state = state;
    return {};
}
SessionStatus SessionInstance::advance(double elapsed, runtime::PhysicsInputs inputs) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {SessionError::Faulted, {}};
    if (!std::isfinite(elapsed) || elapsed < 0)
        return {SessionError::InvalidInput, {}};
    if (auto status = start(); !status)
        return status;
    data.callback_status = {};
    const auto status = data.player.advance(elapsed, inputs, {&data, &Impl::before_tick});
    if (!status)
        return data.fail(data.callback_status ? checked(status) : data.callback_status);
    if (data.playback_state == runtime::PlaybackState::Running &&
        data.bg_target.texture.value != 0 && data.bg_fade_duration > 0)
    {
        data.bg_fade_progress +=
            std::min(elapsed, runtime::kMaximumAdvanceSeconds) / data.bg_fade_duration;
        if (data.bg_fade_progress >= 1.0)
        {
            data.bg_fade_progress = 1.0;
            data.bg_current = data.bg_target;
            data.bg_target = {};
        }
    }
    return {};
}
SessionStatus SessionInstance::prepare_frame() noexcept
{
    if (impl_->faulted)
        return {SessionError::Faulted, {}};
    const auto status = checked(impl_->player.prepare_frame());
    return status ? status : impl_->fail(status);
}
SessionStatus SessionInstance::reset() noexcept
{
    auto& data = *impl_;
    data.player.reset();
    data.playback_state = runtime::PlaybackState::Running;
    data.started = data.faulted = data.pointer_inside = data.pending_gaze = data.returning = false;
    data.audio_level = data.lip_value = 0;
    data.main = {};
    data.differences = {};
    data.reaction = {};
    data.phase = ReactionPhase::Idle;
    data.random = data.config.reaction_seed;
    data.command_head = data.command_count = 0;
    data.bg_current = {};
    data.bg_target = {};
    data.bg_fade_progress = 1.0;
    data.bg_fade_duration = 0;
    data.enqueue(AudioCommandKind::Stop);
    return start();
}
std::optional<AudioCommand> SessionInstance::pop_audio_command() noexcept
{
    auto& data = *impl_;
    if (!data.command_count)
        return std::nullopt;
    const auto command = data.commands[data.command_head];
    data.command_head = (data.command_head + 1) % data.commands.size();
    --data.command_count;
    return command;
}
ReactionPhase SessionInstance::reaction_phase() const noexcept
{
    return impl_->phase;
}
ReactionId SessionInstance::active_reaction() const noexcept
{
    return impl_->reaction;
}
const runtime::PlaybackInstance& SessionInstance::playback() const noexcept
{
    return impl_->player;
}
RenderFrame SessionInstance::frame() const noexcept
{
    return impl_->faulted ? RenderFrame{} : impl_->player.frame();
}
SessionStatus SessionInstance::set_output_size(int width, int height) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {SessionError::Faulted, {}};
    if (width < 0 || height < 0)
        return {SessionError::InvalidInput, {}};
    data.output_width = width;
    data.output_height = height;
    return {};
}
SessionStatus SessionInstance::set_background(TextureId id, int width, int height) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {SessionError::Faulted, {}};
    if (id.value == 0 || width <= 0 || height <= 0)
        return {SessionError::InvalidInput, {}};
    data.bg_current = {id, width, height};
    data.bg_target = {};
    data.bg_fade_progress = 1.0;
    data.bg_fade_duration = 0;
    return {};
}
SessionStatus SessionInstance::fade_background(TextureId id, int width, int height,
                                               double duration_seconds) noexcept
{
    auto& data = *impl_;
    if (data.faulted)
        return {SessionError::Faulted, {}};
    if (id.value == 0 || width <= 0 || height <= 0)
        return {SessionError::InvalidInput, {}};
    if (!std::isfinite(duration_seconds) || duration_seconds <= 0)
        return {SessionError::InvalidInput, {}};
    if (data.bg_current.texture.value == 0)
    {
        data.bg_current = {id, width, height};
        return {};
    }
    data.bg_target = {id, width, height};
    data.bg_fade_progress = 0;
    data.bg_fade_duration = duration_seconds;
    return {};
}
void SessionInstance::clear_background() noexcept
{
    auto& data = *impl_;
    data.bg_current = {};
    data.bg_target = {};
    data.bg_fade_progress = 1.0;
    data.bg_fade_duration = 0;
}
RenderFrame SessionInstance::background_frame() const noexcept
{
    auto& data = *impl_;
    if (data.faulted || data.bg_current.texture.value == 0 || data.output_width <= 0 ||
        data.output_height <= 0)
        return {};
    const bool fading = data.bg_target.texture.value != 0 && data.bg_fade_progress < 1.0;
    if (!fading)
    {
        fill_background_quad(data.bg_vertices.data(), data.bg_indices.data(), data.output_width,
                             data.output_height, data.bg_current.width, data.bg_current.height,
                             1.0f, 0);
        data.bg_commands[0] = {data.bg_current.texture, 0, 6, BlendMode::Over};
        return {std::span<const RenderVertex>(data.bg_vertices.data(), 4),
                std::span<const std::uint32_t>(data.bg_indices.data(), 6),
                std::span<const DrawCommand>(data.bg_commands.data(), 1),
                ColorComposition::PremultipliedOver};
    }
    const auto alpha = static_cast<float>(data.bg_fade_progress);
    fill_background_quad(data.bg_vertices.data(), data.bg_indices.data(), data.output_width,
                         data.output_height, data.bg_current.width, data.bg_current.height, 1.0f,
                         0);
    fill_background_quad(data.bg_vertices.data() + 4, data.bg_indices.data() + 6, data.output_width,
                         data.output_height, data.bg_target.width, data.bg_target.height, alpha, 4);
    data.bg_commands[0] = {data.bg_current.texture, 0, 6, BlendMode::Over};
    data.bg_commands[1] = {data.bg_target.texture, 6, 6, BlendMode::Over};
    return {std::span<const RenderVertex>(data.bg_vertices.data(), 8),
            std::span<const std::uint32_t>(data.bg_indices.data(), 12),
            std::span<const DrawCommand>(data.bg_commands.data(), 2),
            ColorComposition::PremultipliedOver};
}
} // namespace nekomote::application
