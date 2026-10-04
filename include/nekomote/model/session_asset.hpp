#pragma once

#include <nekomote/math.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nekomote::model
{
inline constexpr std::uint32_t kSessionAssetVersion = 1;
enum class TouchZone : std::uint8_t
{
    Bust,
    Eye,
    Face,
    Head,
    Pant,
    Count
};
inline constexpr std::size_t kTouchZoneCount = static_cast<std::size_t>(TouchZone::Count);
// Profile and regions are in authored model units, Y down. Output conversion
// includes both the asset's author scale and the instance's scale/placement.
struct TouchEllipse
{
    Vec2 center;
    Vec2 radius;
};
struct InteractionProfile
{
    std::optional<float> eye;
    std::array<std::optional<TouchEllipse>, kTouchZoneCount> regions;
};
struct SessionVariable
{
    std::string label;
    float target{0};
    float author_frames{0};
    float easing{0};
};
struct SessionAction
{
    std::string main_timeline;
    std::array<std::string, 6> difference_slots;
    std::vector<SessionVariable> variables;
};
struct ReactionBeat
{
    std::uint32_t author_tick{0};
    SessionAction action;
};
struct ReactionAsset
{
    std::string label;
    std::vector<TouchZone> zones;
    SessionAction action;
    SessionAction recovery;
    std::vector<ReactionBeat> beats;
    // Audio path is relative to the session asset directory. It is a resource
    // reference; model data does not load files or talk to devices.
    std::string audio_path;
    std::uint32_t duration_ticks{0};
    std::uint32_t recovery_ticks{0};
};
struct SessionAsset
{
    std::string model;
    InteractionProfile interaction;
    std::string idle_timeline;
    float talk_max{5};
    std::vector<ReactionAsset> reactions;
};
} // namespace nekomote::model
