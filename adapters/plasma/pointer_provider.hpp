#pragma once

#include <QPointF>
#include <memory>
#include <optional>

class QQuickItem;

namespace nekomote::plasma
{
enum class PointerScope
{
    Unavailable,
    LocalSurface,
    GlobalDesktop
};

// Desktop-wide pointer sampling for gaze. Wayland does not expose a global
// pointer, so those sessions keep the Qt surface-local input path.
class PointerProvider
{
  public:
    virtual ~PointerProvider() = default;
    virtual PointerScope scope() const noexcept = 0;
    virtual std::optional<QPointF> positionFor(const QQuickItem& item) = 0;
};

std::unique_ptr<PointerProvider> make_global_pointer_provider();
} // namespace nekomote::plasma
