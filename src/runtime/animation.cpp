#include <nekomote/runtime/animation.hpp>
#include <nekomote/runtime/animation_curve.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace nekomote::runtime
{
namespace
{
constexpr float kInfinity = std::numeric_limits<float>::infinity();
using Kind = model::VariableControlKind;
struct Controller
{
    Kind kind{Kind::Immediate};
    float eye_closed{0};
    int eye_interval{-1};
    std::vector<std::array<float, 2>> edges;
    std::vector<float> points, distance;
    std::vector<int> next, jumps;
    std::vector<bool> continuous;
    std::vector<std::pair<ParamId, model::SelectorOption>> options;
};
struct Event
{
    std::uint32_t time{0};
    float target{0}, duration{0}, easing{0};
    bool value{false};
};
struct Track
{
    ParamId variable;
    std::vector<Event> events;
};
struct Clip
{
    std::string label;
    std::uint32_t end{0}, loop_begin{0}, loop_end{0};
    bool loop{false}, supported{true}, additive_supported{true};
    std::vector<Track> tracks;
};
struct ProgramData
{
    std::vector<Controller> controllers{1};
    std::vector<Clip> clips{1};
    std::vector<std::string> diagnostics;
};
void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}
bool integer_time(float value) noexcept
{
    return std::isfinite(value) && value >= 0 && value <= 1000000 && value == std::floor(value);
}
int edge_at(const Controller& c, float value) noexcept
{
    for (std::size_t i = 0; i < c.edges.size(); ++i)
        if (value >= c.edges[i][0] && value <= c.edges[i][1])
            return static_cast<int>(i);
    return -1;
}
void compile_graph(Controller& c, const model::VariableControl& source)
{
    c.edges = source.edges;
    for (const auto& e : c.edges)
    {
        require(std::isfinite(e[0]) && std::isfinite(e[1]) && e[0] <= e[1],
                source.variable + ": invalid expression edge");
        c.points.push_back(e[0]);
        c.points.push_back(e[1]);
    }
    for (const auto& junction : source.junctions)
        c.points.insert(c.points.end(), junction.begin(), junction.end());
    std::sort(c.points.begin(), c.points.end());
    c.points.erase(std::unique(c.points.begin(), c.points.end()), c.points.end());
    const auto n = c.points.size();
    require(n > 0 && n <= 128, source.variable + ": invalid expression graph capacity");
    for (float p : c.points)
        require(std::isfinite(p) && edge_at(c, p) >= 0,
                source.variable + ": junction outside expression edges");
    c.distance.assign(n * n, kInfinity);
    c.next.assign(n * n, -1);
    c.jumps.assign(n * n, 1000000);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j)
        {
            const bool connected =
                std::any_of(c.edges.begin(), c.edges.end(),
                            [&](const auto& e)
                            {
                                return c.points[i] >= e[0] && c.points[i] <= e[1] &&
                                       c.points[j] >= e[0] && c.points[j] <= e[1];
                            });
            if (connected)
            {
                c.distance[i * n + j] = std::abs(c.points[i] - c.points[j]);
                c.next[i * n + j] = static_cast<int>(j);
                c.jumps[i * n + j] = 0;
            }
        }
    for (const auto& junction : source.junctions)
        for (float a : junction)
            for (float b : junction)
            {
                const auto i = static_cast<std::size_t>(
                    std::lower_bound(c.points.begin(), c.points.end(), a) - c.points.begin());
                const auto j = static_cast<std::size_t>(
                    std::lower_bound(c.points.begin(), c.points.end(), b) - c.points.begin());
                c.distance[i * n + j] = 0;
                c.next[i * n + j] = static_cast<int>(j);
                c.jumps[i * n + j] = i == j ? 0 : 1;
            }
    const auto adjacent_distance = c.distance;
    const auto adjacent_next = c.next;
    const auto adjacent_jumps = c.jumps;
    c.continuous.resize(n * n);
    for (std::size_t i = 0; i < n * n; ++i)
        c.continuous[i] = adjacent_jumps[i] == 0;
    // Enter the destination interval through a junction before approaching the
    // target. Traversing its interior as part of the graph route would create
    // an unobserved detour and reversal (e.g. eyebrow 34/35 -> 5).
    for (const auto& terminal : c.edges)
    {
        auto distance = adjacent_distance;
        auto next = adjacent_next;
        auto jumps = adjacent_jumps;
        const auto in_terminal = [&](std::size_t i)
        { return c.points[i] >= terminal[0] && c.points[i] <= terminal[1]; };
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j)
                if (i != j && in_terminal(i) && in_terminal(j) && c.continuous[i * n + j])
                {
                    distance[i * n + j] = kInfinity;
                    next[i * n + j] = -1;
                    jumps[i * n + j] = 1000000;
                }
        for (std::size_t k = 0; k < n; ++k)
            for (std::size_t i = 0; i < n; ++i)
                for (std::size_t j = 0; j < n; ++j)
                    if (jumps[i * n + k] + jumps[k * n + j] < jumps[i * n + j] ||
                        (jumps[i * n + k] + jumps[k * n + j] == jumps[i * n + j] &&
                         distance[i * n + k] + distance[k * n + j] < distance[i * n + j]))
                    {
                        distance[i * n + j] = distance[i * n + k] + distance[k * n + j];
                        next[i * n + j] = next[i * n + k];
                        jumps[i * n + j] = jumps[i * n + k] + jumps[k * n + j];
                    }
        for (std::size_t j = 0; j < n; ++j)
            if (in_terminal(j))
                for (std::size_t i = 0; i < n; ++i)
                {
                    c.distance[i * n + j] = distance[i * n + j];
                    c.next[i * n + j] = next[i * n + j];
                    c.jumps[i * n + j] = jumps[i * n + j];
                }
    }
}
struct Segment
{
    float begin{0}, end{0};
};
struct Transition
{
    Kind controller_kind{Kind::Immediate};
    float value{0}, start{0}, target{0}, duration{0}, easing{0}, progress{0}, elapsed{0}, length{0};
    bool active{false}, deferred{false};
    std::size_t path_index{0};
    float traversed_length{0};
    float curve_distance{0};
    std::vector<Segment> path;
    explicit Transition(std::size_t capacity = 0)
    {
        path.reserve(capacity);
    }
    void reset() noexcept
    {
        value = start = target = duration = easing = progress = elapsed = length = 0;
        active = deferred = false;
        path.clear();
        path_index = 0;
        traversed_length = 0;
        curve_distance = 0;
    }
};
bool route(const Controller& c, Transition& state, float target) noexcept
{
    if (c.kind != Kind::Expression && c.kind != Kind::Eye)
    {
        state.path.clear();
        state.length = 0;
        return true;
    }
    const auto start_edge = edge_at(c, state.value), end_edge = edge_at(c, target);
    if (start_edge < 0 || end_edge < 0)
        return false;
    const auto& a = c.edges[static_cast<std::size_t>(start_edge)];
    const auto& b = c.edges[static_cast<std::size_t>(end_edge)];
    float best = start_edge == end_edge ? std::abs(target - state.value) : kInfinity;
    float nearest_target = kInfinity;
    int best_jumps = 1000000;
    int first = -1, last = -1;
    const auto n = c.points.size();
    if (start_edge != end_edge)
        for (std::size_t j = 0; j < n; ++j)
        {
            if (c.points[j] < b[0] || c.points[j] > b[1])
                continue;
            const float target_distance = std::abs(target - c.points[j]);
            for (std::size_t i = 0; i < n; ++i)
            {
                if (c.points[i] < a[0] || c.points[i] > a[1])
                    continue;
                if (c.kind == Kind::Eye && start_edge == c.eye_interval &&
                    c.points[i] != c.eye_closed)
                    continue;
                const float cost =
                    std::abs(c.points[i] - state.value) + c.distance[i * n + j] + target_distance;
                const int jumps = c.jumps[i * n + j];
                if (std::isfinite(cost) &&
                    (jumps < best_jumps ||
                     (jumps == best_jumps && target_distance < nearest_target) ||
                     (jumps == best_jumps && target_distance == nearest_target &&
                      c.kind == Kind::Eye && c.points[j] == c.eye_closed &&
                      static_cast<int>(j) != last) ||
                     (jumps == best_jumps && target_distance == nearest_target &&
                      static_cast<int>(j) == last && cost < best)))
                {
                    best = cost;
                    best_jumps = jumps;
                    nearest_target = target_distance;
                    first = static_cast<int>(i);
                    last = static_cast<int>(j);
                }
            }
        }
    if (!std::isfinite(best))
        return false;
    state.path.clear();
    const auto segment = [&](float begin, float end)
    {
        if (begin == end)
            return;
        if (!state.path.empty() && state.path.back().end == begin &&
            (state.path.back().end - state.path.back().begin) * (end - begin) > 0)
            state.path.back().end = end;
        else
            state.path.push_back({begin, end});
    };
    if (first < 0)
        segment(state.value, target);
    else
    {
        segment(state.value, c.points[static_cast<std::size_t>(first)]);
        int cursor = first;
        for (std::size_t guard = 0; cursor != last && guard < n; ++guard)
        {
            const auto current = static_cast<std::size_t>(cursor);
            const int next = c.next[current * n + static_cast<std::size_t>(last)];
            if (next < 0 || next == cursor)
                return false;
            const auto successor = static_cast<std::size_t>(next);
            if (c.continuous[current * n + successor])
                segment(c.points[current], c.points[successor]);
            cursor = next;
        }
        if (cursor != last)
            return false;
        segment(c.points[static_cast<std::size_t>(last)], target);
    }
    state.length = best;
    return true;
}
bool request(const Controller& c, Transition& state, float target, float duration,
             float easing) noexcept
{
    if (!route(c, state, target))
        return false;
    if (c.kind == Kind::Eye && edge_at(c, state.value) == edge_at(c, target))
        state.path.clear();
    state.controller_kind = c.kind;
    state.start = state.value;
    state.target = c.kind == Kind::Selector ? std::trunc(target) : target;
    state.duration = duration;
    state.easing = easing;
    state.progress = state.elapsed = state.curve_distance = 0;
    state.path_index = 0;
    state.traversed_length = 0;
    state.active = true;
    state.deferred = c.kind == Kind::Mouth || c.kind == Kind::Selector;
    if (c.kind == Kind::Immediate || (!state.deferred && duration == 0) ||
        ((c.kind == Kind::Expression || c.kind == Kind::Eye) && state.length == 0))
    {
        state.value = target;
        state.active = false;
    }
    return true;
}
bool update_expression_curve(Transition& state) noexcept
{
    // The curve caches distance, independently of the rounded model value. At a
    // junction the discarded overshoot changes the next inverse curve input.
    // Observed through public Math calls and Float32 stores; see p5-expression-easing.json.
    const auto advance_distance = [&](float previous)
    {
        const double phase = ease_progress(
            static_cast<double>(previous) / static_cast<double>(state.length), -state.easing);
        const double next =
            static_cast<double>(state.length) *
            ease_progress(phase + static_cast<double>(1.0f / state.duration), state.easing);
        state.curve_distance = static_cast<float>(next);
        return next - static_cast<double>(previous);
    };
    if (state.elapsed == 1)
        state.value = state.path.front().begin;
    const double distance = advance_distance(state.curve_distance);
    const auto& segment = state.path[state.path_index];
    const double direction = segment.end > segment.begin ? 1 : -1;
    float next_value = static_cast<float>(static_cast<double>(state.value) + direction * distance);
    if ((static_cast<double>(next_value) - static_cast<double>(segment.end)) * direction >= 0)
    {
        if (state.path_index + 1 == state.path.size())
        {
            state.value = state.target; // includes a terminal zero-distance junction
            state.active = false;
            return true;
        }
        state.traversed_length += std::abs(segment.end - segment.begin);
        const float reset_distance =
            static_cast<float>(static_cast<double>(state.traversed_length) - distance);
        if (reset_distance < 0)
        {
            // The reference produces NaN when this reaches a fractional inverse
            // power. Reject the numerical domain instead of clamping its input.
            state.active = false;
            return false;
        }
        const double next_distance = advance_distance(reset_distance);
        const auto& successor = state.path[++state.path_index];
        next_value = static_cast<float>(static_cast<double>(successor.begin) +
                                        (successor.end > successor.begin ? 1 : -1) * next_distance);
        next_value = std::clamp(next_value, std::min(successor.begin, successor.end),
                                std::max(successor.begin, successor.end));
    }
    state.value = next_value;
    return true;
}
bool update(Transition& state) noexcept
{
    if (!state.active)
        return true;
    state.elapsed += 1;
    if (!state.path.empty() && (state.easing != 0 || state.controller_kind == Kind::Eye))
    {
        return update_expression_curve(state);
    }
    if (state.deferred || state.elapsed >= state.duration)
    {
        state.value = state.target;
        state.active = false;
        return true;
    }
    // The Float32 progress cache is saved for the next tick; this tick
    // consumes the transient sum in the double curve and final value.
    // Public Math/storage observations: p6-transform-precision.json.
    const double progress = std::min(1.0, static_cast<double>(state.progress) +
                                              static_cast<double>(1.0f / state.duration));
    state.progress = static_cast<float>(progress);
    const double amount = ease_progress(progress, state.easing);
    if (state.path.empty())
        state.value = static_cast<float>(
            static_cast<double>(state.start) +
            (static_cast<double>(state.target) - static_cast<double>(state.start)) * amount);
    else
    {
        const float distance = state.length / state.duration;
        auto index = state.path_index;
        const auto& segment = state.path[index];
        const float direction = segment.end > segment.begin ? 1.0f : -1.0f;
        float value = state.value + direction * distance;
        if ((value - segment.end) * direction >= 0 ||
            static_cast<double>(state.length) * amount >=
                static_cast<double>(state.traversed_length + std::abs(segment.end - segment.begin)))
        {
            if (index + 1 < state.path.size())
            {
                state.traversed_length += std::abs(segment.end - segment.begin);
                ++index;
                const auto& next = state.path[index];
                value = next.begin + (next.end > next.begin ? distance : -distance);
                value = std::clamp(value, std::min(next.begin, next.end),
                                   std::max(next.begin, next.end));
            }
            else
                value = segment.end;
        }
        state.path_index = index;
        state.value = value;
    }
    return true;
}
} // namespace
struct AnimationProgram::Impl : ProgramData
{
};
AnimationProgram::AnimationProgram(std::unique_ptr<Impl> data) : impl_(std::move(data))
{
}
AnimationProgram::~AnimationProgram() = default;
TimelineId AnimationProgram::find_timeline(std::string_view label) const noexcept
{
    for (std::size_t i = 1; i < impl_->clips.size(); ++i)
        if (impl_->clips[i].label == label)
            return TimelineId{static_cast<std::uint32_t>(i)};
    return {};
}
std::string_view AnimationProgram::timeline_label(TimelineId id) const noexcept
{
    return id.valid() && id.value < impl_->clips.size() ? impl_->clips[id.value].label
                                                        : std::string_view{};
}
std::span<const std::string> AnimationProgram::diagnostics() const noexcept
{
    return impl_->diagnostics;
}
AnimationCompileResult compile_animation(const model::ModelAsset& asset, const GeometryScene& scene)
{
    AnimationCompileResult result;
    try
    {
        auto data = std::make_unique<AnimationProgram::Impl>();
        data->controllers.resize(scene.variables().size() + 1);
        std::vector<bool> assigned(data->controllers.size());
        for (const auto& control : asset.variable_controls)
        {
            const auto id = scene.find_variable(control.variable);
            require(id.valid(), control.variable + ": missing controller variable");
            require(!assigned[id.value], control.variable + ": duplicate variable controller");
            assigned[id.value] = true;
            auto& c = data->controllers[id.value];
            c.kind = control.enabled ? control.kind : Kind::Immediate;
            if (!control.enabled)
                continue;
            if (c.kind == Kind::Expression || c.kind == Kind::Eye)
                compile_graph(c, control);
            if (c.kind == Kind::Eye)
            {
                c.eye_closed = control.end_frame;
                c.eye_interval = edge_at(c, control.begin_frame);
                require(c.eye_interval >= 0 && edge_at(c, c.eye_closed) == c.eye_interval,
                        control.variable + ": invalid Eye open/closed interval");
            }
            for (const auto& option : control.options)
            {
                const auto variable = scene.find_variable(option.variable);
                require(variable.valid(), option.variable + ": missing selector output");
                c.options.emplace_back(variable, option);
            }
        }
        std::vector<bool> selector_outputs(data->controllers.size());
        for (std::size_t i = 1; i < data->controllers.size(); ++i)
            if (data->controllers[i].kind == Kind::Selector)
                for (const auto& [id, option] : data->controllers[i].options)
                {
                    require(id.value != i && !selector_outputs[id.value],
                            option.variable + ": repeated or cyclic selector output");
                    require(data->controllers[id.value].kind == Kind::Linear,
                            option.variable + ": unverified selector output controller");
                    require(option.on_value == 0 && option.off_value == 1,
                            option.variable + ": unverified selector value range");
                    selector_outputs[id.value] = true;
                }
        // Mouth controllers create a separate interpolated talk channel. A bare
        // variable is immediate; talkLabel is not an ordinary unbound variable.
        for (const auto& control : asset.variable_controls)
            if (control.enabled && control.kind == Kind::Mouth && !control.talk_variable.empty())
            {
                const auto id = scene.find_variable(control.talk_variable);
                require(id.valid(), control.talk_variable + ": missing talk variable");
                if (!assigned[id.value])
                    data->controllers[id.value].kind = Kind::Linear;
            }
        for (const auto& timeline : asset.timelines)
        {
            Clip clip;
            clip.label = timeline.label;
            require(!clip.label.empty(), "empty timeline label");
            require(std::none_of(data->clips.begin() + 1, data->clips.end(),
                                 [&](const auto& c) { return c.label == clip.label; }),
                    clip.label + ": duplicate timeline label");
            clip.loop = timeline.loop_begin >= 0 || timeline.loop_end >= 0;
            if (clip.loop && integer_time(timeline.loop_begin) && integer_time(timeline.loop_end) &&
                timeline.loop_end > timeline.loop_begin)
            {
                clip.loop_begin = static_cast<std::uint32_t>(timeline.loop_begin);
                clip.loop_end = static_cast<std::uint32_t>(timeline.loop_end);
            }
            else if (clip.loop)
                clip.supported = false;
            if (timeline.last_time >= 0 && !integer_time(timeline.last_time))
                clip.supported = false;
            for (const auto& source : timeline.tracks)
            {
                if (source.keys.empty())
                    continue;
                Track track;
                track.variable = scene.find_variable(source.label);
                require(track.variable.valid(),
                        clip.label + "/" + source.label + ": missing variable");
                const auto& controller = data->controllers[track.variable.value];
                if (controller.kind != Kind::Linear)
                    clip.additive_supported = false;
                for (std::size_t i = 0; i < source.keys.size(); ++i)
                {
                    const auto& key = source.keys[i];
                    if (!integer_time(key.time) || (key.type != 0 && key.type != 2) ||
                        (i > 0 && key.time <= source.keys[i - 1].time) ||
                        (key.type == 2 && !key.value.has_value()))
                    {
                        clip.supported = false;
                        continue;
                    }
                    const auto time = static_cast<std::uint32_t>(key.time);
                    clip.end = std::max(clip.end, time);
                    Event e;
                    e.time = time;
                    e.value = key.type == 2;
                    if (e.value)
                    {
                        e.target = key.value.data;
                        e.easing = key.easing.has_value() ? key.easing.data : 0;
                        e.duration = i + 1 < source.keys.size()
                                         ? std::max(0.0f, source.keys[i + 1].time - key.time - 1)
                                         : 0;
                        if (!std::isfinite(e.target) || !std::isfinite(e.easing))
                            clip.supported = false;
                        if ((controller.kind == Kind::Expression || controller.kind == Kind::Eye) &&
                            edge_at(controller, e.target) < 0)
                            clip.supported = false;
                    }
                    track.events.push_back(e);
                }
                if (source.keys.back().type != 0)
                    clip.supported = false; // terminal ownership not yet verified
                clip.tracks.push_back(std::move(track));
            }
            if (clip.loop && clip.loop_end > 0)
                clip.end = clip.loop_end;
            else if (integer_time(timeline.last_time))
                clip.end = static_cast<std::uint32_t>(timeline.last_time);
            if (!clip.supported)
                data->diagnostics.push_back(
                    clip.label +
                    ": selected timeline contains unverified keys/loop/terminal semantics");
            data->clips.push_back(std::move(clip));
        }
        result.program =
            std::unique_ptr<const AnimationProgram>(new AnimationProgram(std::move(data)));
    }
    catch (const std::runtime_error& e)
    {
        result.errors.push_back(e.what());
    }
    return result;
}
struct AnimationInstance::Impl
{
    struct ClipState
    {
        std::uint32_t time{0};
        bool playing{false}, additive{false}, skip_update{false}, sampled{false};
        std::vector<Transition> tracks;
    };
    struct SelectorState
    {
        std::vector<float> weights, starts;
        float progress{0}, elapsed{0};
        bool initialized{false}, pending{false};
    };
    const ProgramData& program;
    std::vector<Transition> base;
    std::vector<SelectorState> selectors;
    std::vector<ClipState> clips;
    std::vector<ParameterValue> output;
    std::vector<std::size_t> order;
    std::uint64_t ticks{0};
    explicit Impl(const ProgramData& data)
        : program(data), selectors(data.controllers.size()), clips(data.clips.size())
    {
        order.reserve(clips.size());
        base.reserve(data.controllers.size());
        for (std::size_t i = 0; i < data.controllers.size(); ++i)
        {
            const auto& c = data.controllers[i];
            base.emplace_back(c.points.size() + 2);
            selectors[i].weights.resize(c.options.size());
            selectors[i].starts.resize(c.options.size());
        }
        for (std::size_t i = 1; i < data.controllers.size(); ++i)
            output.push_back({ParamId{static_cast<std::uint32_t>(i)}, 0});
        for (std::size_t i = 1; i < clips.size(); ++i)
        {
            clips[i].tracks.reserve(data.clips[i].tracks.size());
            for (std::size_t j = 0; j < data.clips[i].tracks.size(); ++j)
                clips[i].tracks.emplace_back(); // additive channels are linear
        }
    }
    bool request_parameter(ParamId id, float target, float duration, float easing) noexcept
    {
        if (!request(program.controllers[id.value], base[id.value], target, duration, easing))
            return false;
        if (program.controllers[id.value].kind == Kind::Selector)
            selectors[id.value].pending = true;
        return true;
    }
    void update_selectors() noexcept
    {
        for (std::size_t i = 1; i < selectors.size(); ++i)
        {
            const auto& control = program.controllers[i];
            if (control.kind != Kind::Selector)
                continue;
            auto& state = selectors[i];
            const auto& transition = base[i];
            if (!state.initialized)
            {
                const float initial = state.pending ? transition.start : transition.value;
                for (std::size_t j = 0; j < state.weights.size(); ++j)
                    state.weights[j] = initial == static_cast<float>(j) ? 1.0f : 0.0f;
                state.initialized = true;
                std::copy(state.weights.begin(), state.weights.end(), state.starts.begin());
                state.elapsed = transition.duration;
            }
            if (state.pending)
            {
                std::copy(state.weights.begin(), state.weights.end(), state.starts.begin());
                state.elapsed = state.progress = 0;
                state.pending = false;
            }
            state.elapsed += 1;
            double amount = 1;
            if (state.elapsed < transition.duration)
            {
                const double progress =
                    std::min(1.0, static_cast<double>(state.progress) +
                                      static_cast<double>(1.0f / transition.duration));
                state.progress = static_cast<float>(progress);
                amount = ease_progress(progress, transition.easing);
            }
            for (std::size_t j = 0; j < control.options.size(); ++j)
            {
                const double direction = transition.value == static_cast<float>(j) ? 1 : -1;
                const double weight =
                    std::clamp(static_cast<double>(state.starts[j]) + direction * amount, 0.0, 1.0);
                state.weights[j] = static_cast<float>(weight);
                const auto& [id, option] = control.options[j];
                base[id.value].value = static_cast<float>(
                    static_cast<double>(option.off_value) +
                    (static_cast<double>(option.on_value) - static_cast<double>(option.off_value)) *
                        weight);
            }
        }
    }
    void compose() noexcept
    {
        for (auto& value : output)
            value.value = base[value.id.value].value;
        for (std::size_t i = 1; i < clips.size(); ++i)
        {
            const auto& state = clips[i];
            if (!state.playing || !state.additive || !state.sampled)
                continue;
            for (std::size_t j = 0; j < state.tracks.size(); ++j)
                output[program.clips[i].tracks[j].variable.value - 1].value +=
                    state.tracks[j].value;
        }
    }
    bool dispatch(std::size_t index, std::uint32_t time) noexcept
    {
        auto& state = clips[index];
        const auto& clip = program.clips[index];
        for (std::size_t j = 0; j < clip.tracks.size(); ++j)
        {
            const auto& track = clip.tracks[j];
            for (const auto& event : track.events)
                if (event.time == time && event.value)
                {
                    const auto& controller = program.controllers[track.variable.value];
                    const bool accepted = state.additive
                                              ? request(controller, state.tracks[j], event.target,
                                                        event.duration, event.easing)
                                              : request_parameter(track.variable, event.target,
                                                                  event.duration, event.easing);
                    if (!accepted)
                        return false;
                }
        }
        return true;
    }
};
AnimationInstance::AnimationInstance(const AnimationProgram& program)
    : impl_(std::make_unique<Impl>(*program.impl_))
{
}
AnimationInstance::~AnimationInstance() = default;
AnimationStatus AnimationInstance::transition(ParamId id, float target, float frames,
                                              float easing) noexcept
{
    if (!id.valid() || id.value >= impl_->base.size())
        return {AnimationError::InvalidParameter, {}, id};
    if (!std::isfinite(target) || !std::isfinite(frames) || !std::isfinite(easing) || frames < 0)
        return {AnimationError::InvalidValue, {}, id};
    if (!impl_->request_parameter(id, target, frames, easing))
        return {AnimationError::UnsupportedController, {}, id};
    impl_->compose();
    return {};
}
AnimationStatus AnimationInstance::play(TimelineId id, TimelinePlayMode mode) noexcept
{
    auto& data = *impl_;
    if (!id.valid() || id.value >= data.clips.size())
        return {AnimationError::InvalidTimeline, id, {}};
    const auto& clip = data.program.clips[id.value];
    if (!clip.supported || (mode == TimelinePlayMode::AdditiveParallel && !clip.additive_supported))
        return {AnimationError::UnsupportedTimeline, id, {}};
    if (mode == TimelinePlayMode::Replace)
    {
        for (auto& state : data.clips)
            state.playing = false;
        data.order.clear();
    }
    data.order.erase(std::remove(data.order.begin(), data.order.end(), id.value), data.order.end());
    data.order.push_back(id.value);
    auto& state = data.clips[id.value];
    state.time = 0;
    state.playing = true;
    state.sampled = false;
    state.additive = mode == TimelinePlayMode::AdditiveParallel;
    for (auto& track : state.tracks)
        track.reset();
    if (!data.dispatch(id.value, 0))
        return {AnimationError::UnsupportedController, id, {}};
    data.compose();
    return {};
}
AnimationStatus AnimationInstance::stop(TimelineId id) noexcept
{
    if (!id.valid() || id.value >= impl_->clips.size())
        return {AnimationError::InvalidTimeline, id, {}};
    impl_->clips[id.value].playing = false;
    impl_->compose();
    return {};
}
bool AnimationInstance::is_playing(TimelineId id) const noexcept
{
    return id.valid() && id.value < impl_->clips.size() && impl_->clips[id.value].playing;
}
AnimationStatus AnimationInstance::step() noexcept
{
    auto& data = *impl_;
    for (const auto i : data.order)
    {
        auto& state = data.clips[i];
        const auto& clip = data.program.clips[i];
        state.skip_update = false;
        if (!state.playing)
            continue;
        state.sampled = true;
        ++state.time;
        if (clip.loop && state.time >= clip.loop_end)
        {
            state.time = clip.loop_begin;
            if (state.additive)
            {
                state.skip_update = true;
            }
        }
        else if (state.time >= clip.end)
            state.playing = false;
        if (state.playing && !data.dispatch(i, state.time))
            return {AnimationError::UnsupportedController,
                    TimelineId{static_cast<std::uint32_t>(i)},
                    {}};
    }
    for (std::size_t i = 1; i < data.base.size(); ++i)
        if (!update(data.base[i]))
            return {
                AnimationError::UnsupportedController, {}, ParamId{static_cast<std::uint32_t>(i)}};
    for (std::size_t i = 1; i < data.clips.size(); ++i)
        if (data.clips[i].playing && data.clips[i].additive && !data.clips[i].skip_update)
            for (auto& transition : data.clips[i].tracks)
                if (!update(transition))
                    return {AnimationError::UnsupportedController,
                            TimelineId{static_cast<std::uint32_t>(i)},
                            {}};
    data.update_selectors();
    data.compose();
    ++data.ticks;
    return {};
}
void AnimationInstance::reset() noexcept
{
    auto& data = *impl_;
    for (auto& transition : data.base)
        transition.reset();
    for (auto& selector : data.selectors)
    {
        std::fill(selector.weights.begin(), selector.weights.end(), 0);
        std::fill(selector.starts.begin(), selector.starts.end(), 0);
        selector.progress = selector.elapsed = 0;
        selector.initialized = selector.pending = false;
    }
    for (auto& clip : data.clips)
    {
        clip.time = 0;
        clip.playing = clip.additive = clip.skip_update = clip.sampled = false;
        for (auto& track : clip.tracks)
            track.reset();
    }
    data.order.clear();
    data.ticks = 0;
    data.compose();
}
std::span<const ParameterValue> AnimationInstance::values() const noexcept
{
    return impl_->output;
}
std::uint64_t AnimationInstance::tick_count() const noexcept
{
    return impl_->ticks;
}
} // namespace nekomote::runtime
