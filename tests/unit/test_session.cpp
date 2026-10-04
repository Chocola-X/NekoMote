#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/application/session.hpp>
#include <nekomote/asset_io/import_emote.hpp>
#include <nekomote/asset_io/session_asset.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

namespace
{
using namespace nekomote;
using namespace nekomote::application;
struct Reference
{
    model::ModelAsset model;
    model::SessionAsset session;
    std::unique_ptr<const SessionProgram> program;
    Reference()
    {
        const std::filesystem::path root{NEKOMOTE_FIXTURES_DIR};
        auto loaded =
            asset_io::import_emote({root / "minimal.json", root / "minimal.resx.json", root, {}});
        if (!loaded.ok)
            throw std::runtime_error(loaded.report.errors.front());
        model = std::move(loaded.asset);
        for (auto& part : model.parts)
            for (auto& layer : part.layers)
            {
                layer.inherit_mask = 0x20007fc;
                layer.transform_order.clear();
            }
        model.base_group = "body_parts";
        model.base_motion = "torso";
        for (const auto* name : {"face_eye_LR", "face_eye_UD", "head_slant", "head_LR", "head_UD",
                                 "body_slant", "body_LR", "body_UD", "face_talk"})
        {
            model.variables.push_back({name, {}});
            model::VariableControl control;
            control.variable = name;
            control.kind = model::VariableControlKind::Linear;
            model.variable_controls.push_back(control);
        }
        const auto field = [](float value)
        { return model::Field<float>{model::FieldState::Value, value}; };
        model::Timeline idle;
        idle.label = "idle";
        idle.loop_begin = 0;
        idle.loop_end = 30;
        idle.tracks = {
            {"body_UD",
             {{0, 2, field(10), field(0)}, {15, 2, field(-10), field(0)}, {30, 0, {}, {}}}}};
        model.timelines.push_back(idle);
        auto motion = idle;
        motion.label = "pose";
        motion.loop_begin = motion.loop_end = -1;
        motion.tracks = {{"head_LR", {{0, 2, field(30), field(0)}, {30, 0, {}, {}}}}};
        model.timelines.push_back(motion);
        session.model = "test";
        session.idle_timeline = "idle";
        session.interaction.eye = -100;
        for (auto& region : session.interaction.regions)
            region = model::TouchEllipse{{0, 0}, {20, 20}};
        model::ReactionAsset reaction;
        reaction.label = "reaction";
        reaction.zones = {model::TouchZone::Bust};
        reaction.action.main_timeline = "pose";
        reaction.action.difference_slots[3] = "idle";
        reaction.audio_path = "audio/voice.wav";
        reaction.duration_ticks = 12;
        reaction.recovery_ticks = 4;
        reaction.beats = {{4, reaction.action}};
        reaction.beats[0].action.variables = {{"head_LR", -10, 0, 0}};
        reaction.recovery.difference_slots[3] = "idle";
        reaction.recovery.variables = {{"head_LR", 0, 2, 0}};
        session.reactions.push_back(reaction);
        reaction.label = "eye";
        reaction.zones = {model::TouchZone::Eye};
        reaction.audio_path = "";
        session.reactions.push_back(reaction);
        compile();
    }
    void compile()
    {
        auto result = compile_session(model, session);
        if (!result.program)
            throw std::runtime_error(result.errors.front());
        program = std::move(result.program);
    }
    float value(const SessionInstance& instance, const char* label) const
    {
        const auto id = program->playback().scene().find_variable(label);
        return instance.playback().values()[id.value - 1].value;
    }
};
} // namespace
TEST_CASE("Application schedules reaction beats and recovery on the shared author clock",
          "[application]")
{
    Reference reference;
    SessionInstance session(*reference.program,
                            {{1}, 1, PointerScope::LocalSurface, AudioMode::Device});
    REQUIRE(session.start());
    REQUIRE(session.react(reference.program->find_reaction("reaction")));
    REQUIRE(session.reaction_phase() == ReactionPhase::Playing);
    auto command = session.pop_audio_command();
    REQUIRE(command);
    REQUIRE(command->kind == AudioCommandKind::Play);
    REQUIRE(command->clip.valid());
    REQUIRE(session.advance(3.0 / 60));
    REQUIRE(reference.value(session, "head_LR") > 0.0f);
    REQUIRE(session.advance(1.0 / 60));
    REQUIRE(reference.value(session, "head_LR") == -10.0f);
    REQUIRE(session.touch({0, 0}).error == SessionError::Busy);
    REQUIRE(session.advance(8.0 / 60));
    REQUIRE(session.playback().tick_count() == std::uint64_t{12});
    REQUIRE(session.reaction_phase() == ReactionPhase::Recovering);
    command = session.pop_audio_command();
    REQUIRE(command);
    REQUIRE(command->kind == AudioCommandKind::Stop);
    REQUIRE(session.advance(4.0 / 60));
    REQUIRE(session.reaction_phase() == ReactionPhase::Idle);
    REQUIRE(reference.value(session, "head_LR") == 0.0f);
    REQUIRE(session.prepare_frame());
    REQUIRE_FALSE(session.frame().commands.empty());
}
TEST_CASE("Application input regions, capabilities and placement are explicit", "[application]")
{
    Reference reference;
    REQUIRE(reference.program->gaze_available());
    REQUIRE(reference.program->touch_available());
    SessionInstance unavailable(*reference.program);
    REQUIRE(unavailable.pointer_move({0, 0}).error == SessionError::InputUnavailable);
    REQUIRE(unavailable.touch({0, 0}).error == SessionError::InputUnavailable);
    REQUIRE(unavailable.advance(1.0 / 60));
    SessionInstance session(*reference.program,
                            {{1}, 1, PointerScope::LocalSurface, AudioMode::Disabled});
    REQUIRE(session.set_origin({50, 80}));
    REQUIRE(session.set_scale(2));
    REQUIRE(session.touch({50, 80}));
    REQUIRE(session.active_reaction() == reference.program->find_reaction("reaction"));
    REQUIRE(session.reset());
    REQUIRE(session.touch({500, 800}));
    REQUIRE_FALSE(session.active_reaction().valid());
    REQUIRE(session.pointer_move({180, -120}));
    REQUIRE(session.advance(1.0 / 60));
    REQUIRE(reference.value(session, "head_LR") > 0.0f);
    REQUIRE_FALSE(session.pointer_move({std::numeric_limits<float>::quiet_NaN(), 0}));
    REQUIRE_FALSE(session.set_scale(0));
    REQUIRE_FALSE(session.set_audio_level(-1));
    REQUIRE(session.pointer_leave());
    REQUIRE(session.pointer_enter({-100, -120}));
    REQUIRE(session.advance(1.0 / 60));
    REQUIRE(session.playback().tick_count() == std::uint64_t{2});
    REQUIRE(session.reset());
    REQUIRE(session.set_scale(1));
    REQUIRE(session.set_origin({0, 0}));
    REQUIRE(session.pointer_move({180, -100}));
    REQUIRE(session.advance(1.0 / 60));
    REQUIRE_THAT(reference.value(session, "face_eye_LR"),
                 Catch::Matchers::WithinAbs(60.0 * std::sqrt(1.0 / 30), 1e-5));
    REQUIRE_THAT(reference.value(session, "head_LR"),
                 Catch::Matchers::WithinAbs(30.0 * std::sqrt(1.0 / 60), 1e-5));
    REQUIRE_THAT(reference.value(session, "body_LR"),
                 Catch::Matchers::WithinAbs(20.0 * std::sqrt(1.0 / 120), 1e-5));
}
TEST_CASE("Application pause, audio snapshots, partitions and resets preserve independent state",
          "[application]")
{
    Reference reference;
    SessionInstance at30(*reference.program,
                         {{1}, 1, PointerScope::LocalSurface, AudioMode::Device}),
        at60(*reference.program, {{1}, 1, PointerScope::LocalSurface, AudioMode::Device}),
        at144(*reference.program, {{1}, 1, PointerScope::LocalSurface, AudioMode::Device});
    for (auto* session : {&at30, &at60, &at144})
    {
        REQUIRE(session->react(reference.program->find_reaction("reaction")));
        REQUIRE(session->set_audio_level(.1f));
        REQUIRE(session->pointer_move({160, -130}));
    }
    for (int i = 0; i < 3; ++i)
        REQUIRE(at30.advance(1.0 / 30));
    for (int i = 0; i < 6; ++i)
        REQUIRE(at60.advance(1.0 / 60));
    for (int i = 0; i < 14; ++i)
        REQUIRE(at144.advance(1.0 / 144));
    REQUIRE(at144.advance(1.0 / 360)); // Total exactly six author ticks.
    REQUIRE(at30.playback().tick_count() == at60.playback().tick_count());
    REQUIRE(at144.playback().tick_count() == at60.playback().tick_count());
    for (std::size_t i = 0; i < at30.playback().values().size(); ++i)
    {
        REQUIRE(at30.playback().values()[i].value == at60.playback().values()[i].value);
        REQUIRE(at144.playback().values()[i].value == at60.playback().values()[i].value);
    }
    REQUIRE(reference.value(at60, "face_talk") > 0.0f);
    REQUIRE(at60.set_playback_state(runtime::PlaybackState::Paused));
    REQUIRE(at60.advance(20));
    REQUIRE(at60.playback().tick_count() == std::uint64_t{6});
    REQUIRE(at60.reaction_phase() == ReactionPhase::Playing);
    REQUIRE(at60.set_playback_state(runtime::PlaybackState::Running));
    REQUIRE(at60.advance(100));
    REQUIRE(at60.playback().tick_count() == std::uint64_t{21});
    REQUIRE(at60.reaction_phase() == ReactionPhase::Idle);
    REQUIRE(at30.reaction_phase() == ReactionPhase::Playing);
    REQUIRE(at60.reset());
    REQUIRE(at60.playback().tick_count() == std::uint64_t{0});
    REQUIRE(at60.reaction_phase() == ReactionPhase::Idle);
    REQUIRE(reference.value(at60, "face_talk") == 0.0f);
}
TEST_CASE("Application rejects invalid rule bindings and reports undrained audio events",
          "[application]")
{
    Reference reference;
    auto invalid = reference.session;
    invalid.reactions[0].action.main_timeline = "absent";
    REQUIRE_FALSE(compile_session(reference.model, invalid).program);
    invalid = reference.session;
    invalid.reactions[0].action.variables = {{"absent", 1, 0, 0}};
    REQUIRE_FALSE(compile_session(reference.model, invalid).program);
    invalid = reference.session;
    invalid.reactions[0].beats[0].author_tick = 12;
    REQUIRE_FALSE(compile_session(reference.model, invalid).program);
    SessionInstance session(*reference.program,
                            {{1}, 1, PointerScope::Unavailable, AudioMode::Device});
    REQUIRE_FALSE(session.react({}));
    for (int i = 0; i < 8; ++i)
        REQUIRE(session.set_playback_state(i % 2 == 0 ? runtime::PlaybackState::Paused
                                                      : runtime::PlaybackState::Running));
    REQUIRE(session.react(reference.program->find_reaction("reaction")).error ==
            SessionError::EventQueueFull);
    REQUIRE(session.reaction_phase() == ReactionPhase::Idle);
    REQUIRE(session.reset());
    REQUIRE(session.react(reference.program->find_reaction("reaction")));
}

TEST_CASE("Undrained audio commands fault a partial scheduled reaction until reset",
          "[application]")
{
    Reference reference;
    SessionInstance session(*reference.program,
                            {{1}, 1, PointerScope::LocalSurface, AudioMode::Device});
    REQUIRE(session.react(reference.program->find_reaction("reaction")));
    for (int i = 0; i < 7; ++i)
        REQUIRE(session.set_playback_state(i % 2 == 0 ? runtime::PlaybackState::Paused
                                                      : runtime::PlaybackState::Running));
    REQUIRE(session.pop_audio_command());
    REQUIRE(session.set_playback_state(runtime::PlaybackState::Running));
    REQUIRE(session.advance(12.0 / 60).error == SessionError::EventQueueFull);
    REQUIRE(session.frame().commands.empty());
    REQUIRE(session.advance(1.0 / 60).error == SessionError::Faulted);
    REQUIRE(session.reset());
    REQUIRE(session.pop_audio_command());
    REQUIRE(session.react(reference.program->find_reaction("reaction")));
    REQUIRE(session.advance(1.0 / 60));
}
