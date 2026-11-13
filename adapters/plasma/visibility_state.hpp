#pragma once

namespace nekomote::plasma
{
// Values match X11's VisibilityUnobscured/PartiallyObscured/FullyObscured.
enum class VisibilityState : int
{
    Unobscured = 0,
    PartiallyObscured = 1,
    FullyObscured = 2
};

inline bool suspends_playback(VisibilityState state)
{
    return state == VisibilityState::FullyObscured;
}
} // namespace nekomote::plasma
