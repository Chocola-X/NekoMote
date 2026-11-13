#pragma once

#include <memory>

#include "occlusion_provider.hpp"
#include "pointer_provider.hpp"

namespace nekomote::plasma
{
// Wayland global pointer bridge backed by a small KWin script. KWin exposes
// workspace.cursorPos to scripts; the script forwards changes over the session
// bus to a service owned by this plugin.
std::unique_ptr<PointerProvider> make_kwin_pointer_provider();
std::unique_ptr<OcclusionProvider> make_kwin_occlusion_provider();
} // namespace nekomote::plasma
