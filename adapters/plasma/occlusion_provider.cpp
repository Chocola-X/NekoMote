#include "occlusion_provider.hpp"

#include <QGuiApplication>
#include <QObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSocketNotifier>

#include "visibility_state.hpp"

#if defined(NEKOMOTE_PLASMA_X11)
#include <X11/Xlib.h>
#endif

namespace nekomote::plasma
{
namespace
{
#if defined(NEKOMOTE_PLASMA_X11)
class X11OcclusionProvider final : public OcclusionProvider
{
  public:
    X11OcclusionProvider() : display_(XOpenDisplay(nullptr))
    {
        if (!display_)
            return;
        notifier_ = new QSocketNotifier(ConnectionNumber(display_), QSocketNotifier::Read);
        QObject::connect(notifier_, &QSocketNotifier::activated, notifier_, [this] { drain(); });
    }
    ~X11OcclusionProvider() override
    {
        delete notifier_;
        notifier_ = nullptr;
        if (display_)
            XCloseDisplay(display_);
    }
    X11OcclusionProvider(const X11OcclusionProvider&) = delete;
    X11OcclusionProvider& operator=(const X11OcclusionProvider&) = delete;

    bool valid() const noexcept
    {
        return display_ != nullptr && notifier_ != nullptr;
    }

    bool fullyObscured(const QQuickItem& item) override
    {
        auto* window = item.window();
        if (!display_ || !window)
            return false;
        const Window id = static_cast<Window>(window->winId());
        if (id != window_id_)
        {
            window_id_ = id;
            obscured_ = false;
            if (id)
            {
                XWindowAttributes attributes;
                if (XGetWindowAttributes(display_, id, &attributes))
                {
                    XSelectInput(display_, id, VisibilityChangeMask);
                    XFlush(display_);
                }
            }
        }
        drain();
        return obscured_;
    }

  private:
    void drain()
    {
        if (!display_)
            return;
        while (XPending(display_) > 0)
        {
            XEvent event;
            XNextEvent(display_, &event);
            if (event.type == VisibilityNotify && event.xvisibility.window == window_id_)
                obscured_ =
                    suspends_playback(static_cast<VisibilityState>(event.xvisibility.state));
        }
    }

    Display* display_{nullptr};
    QSocketNotifier* notifier_{nullptr};
    Window window_id_{0};
    bool obscured_{false};
};
#endif
} // namespace

std::unique_ptr<OcclusionProvider> make_occlusion_provider()
{
#if defined(NEKOMOTE_PLASMA_X11)
    if (QGuiApplication::platformName().startsWith(QLatin1String("xcb")))
    {
        auto provider = std::make_unique<X11OcclusionProvider>();
        if (provider->valid())
            return provider;
    }
#endif
    return {};
}
} // namespace nekomote::plasma
