#pragma once

#include <memory>

class QQuickItem;

namespace nekomote::plasma
{
// Detects when opaque toplevel windows completely cover the wallpaper. Used
// only on X11; Wayland clients cannot inspect the desktop's window tree.
class OcclusionProvider
{
  public:
    virtual ~OcclusionProvider() = default;
    virtual bool fullyObscured(const QQuickItem& item) = 0;
};

std::unique_ptr<OcclusionProvider> make_occlusion_provider();
} // namespace nekomote::plasma
