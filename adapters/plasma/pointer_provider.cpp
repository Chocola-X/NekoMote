#include "pointer_provider.hpp"

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>

#include "pointer_geometry.hpp"

#if defined(NEKOMOTE_PLASMA_X11)
#include <X11/Xlib.h>
#endif

namespace nekomote::plasma
{
namespace
{
#if defined(NEKOMOTE_PLASMA_X11)
class X11PointerProvider final : public PointerProvider
{
  public:
    X11PointerProvider() : display_(XOpenDisplay(nullptr))
    {
        if (display_)
            root_ = DefaultRootWindow(display_);
    }
    ~X11PointerProvider() override
    {
        if (display_)
            XCloseDisplay(display_);
    }
    X11PointerProvider(const X11PointerProvider&) = delete;
    X11PointerProvider& operator=(const X11PointerProvider&) = delete;

    PointerScope scope() const noexcept override
    {
        return display_ ? PointerScope::GlobalDesktop : PointerScope::Unavailable;
    }

    std::optional<QPointF> positionFor(const QQuickItem& item) override
    {
        auto* window = item.window();
        if (!display_ || !window)
            return std::nullopt;
        const Window x11_window = static_cast<Window>(window->winId());
        if (!x11_window)
            return std::nullopt;
        XWindowAttributes attributes;
        if (!XGetWindowAttributes(display_, x11_window, &attributes))
            return std::nullopt;
        int window_x = 0;
        int window_y = 0;
        Window child = 0;
        if (!XTranslateCoordinates(display_, x11_window, root_, 0, 0, &window_x, &window_y, &child))
            return std::nullopt;
        Window root_return{};
        Window child_return{};
        int root_x = 0;
        int root_y = 0;
        int win_x = 0;
        int win_y = 0;
        unsigned int mask = 0;
        if (XQueryPointer(display_, root_, &root_return, &child_return, &root_x, &root_y, &win_x,
                          &win_y, &mask) == False)
            return std::nullopt;
        // X returns device pixels while Qt item coordinates are logical. The
        // host window's root rectangle gives both the scale and the origin.
        const auto local =
            x11_local_logical({root_x, root_y}, {window_x, window_y},
                              {attributes.width, attributes.height}, window->devicePixelRatio());
        return item.mapFromScene(QPointF(local.x, local.y));
    }

  private:
    Display* display_{nullptr};
    Window root_{0};
};
#endif
} // namespace

std::unique_ptr<PointerProvider> make_global_pointer_provider()
{
#if defined(NEKOMOTE_PLASMA_X11)
    // XWayland sessions also report "xcb"; this provider is created from a
    // Plasma wallpaper host, which is an X11 client only on an X11 session.
    if (QGuiApplication::platformName().startsWith(QLatin1String("xcb")))
    {
        auto provider = std::make_unique<X11PointerProvider>();
        if (provider->scope() == PointerScope::GlobalDesktop)
            return provider;
    }
#endif
    return {};
}
} // namespace nekomote::plasma
