#include "occlusion_provider.hpp"

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <algorithm>
#include <cstring>
#include <vector>

#include "kwin_pointer_provider.hpp"
#include "occlusion_geometry.hpp"

#if defined(NEKOMOTE_PLASMA_X11)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

namespace nekomote::plasma
{
namespace
{
#if defined(NEKOMOTE_PLASMA_X11)
class X11OcclusionProvider final : public OcclusionProvider
{
  public:
    X11OcclusionProvider()
        : display_(XOpenDisplay(nullptr)), root_(display_ ? DefaultRootWindow(display_) : 0),
          dock_atom_(display_ ? XInternAtom(display_, "_NET_WM_WINDOW_TYPE_DOCK", False) : None),
          type_atom_(display_ ? XInternAtom(display_, "_NET_WM_WINDOW_TYPE", False) : None),
          frame_extents_atom_(display_ ? XInternAtom(display_, "_NET_FRAME_EXTENTS", False) : None),
          opacity_atom_(display_ ? XInternAtom(display_, "_NET_WM_WINDOW_OPACITY", False) : None)
    {
    }
    ~X11OcclusionProvider() override
    {
        if (display_)
            XCloseDisplay(display_);
    }
    X11OcclusionProvider(const X11OcclusionProvider&) = delete;
    X11OcclusionProvider& operator=(const X11OcclusionProvider&) = delete;

    bool valid() const noexcept
    {
        return display_ != nullptr;
    }

    bool fullyObscured(const QQuickItem& item) override
    {
        auto* window = item.window();
        if (!display_ || !window)
            return false;
        const Window desktop = static_cast<Window>(window->winId());
        if (!desktop)
            return false;
        XWindowAttributes desktop_attributes;
        if (!XGetWindowAttributes(display_, desktop, &desktop_attributes) ||
            desktop_attributes.map_state != IsViewable)
            return false;
        int desktop_x = 0;
        int desktop_y = 0;
        Window child = 0;
        if (!XTranslateCoordinates(display_, desktop, root_, 0, 0, &desktop_x, &desktop_y, &child))
            return false;
        const OcclusionRect bounds{desktop_x, desktop_y, desktop_x + desktop_attributes.width,
                                   desktop_y + desktop_attributes.height};
        if (bounds.left >= bounds.right || bounds.top >= bounds.bottom)
            return false;

        Window root_return = 0;
        Window parent_return = 0;
        Window* children = nullptr;
        unsigned int child_count = 0;
        if (!XQueryTree(display_, root_, &root_return, &parent_return, &children, &child_count))
            return false;
        int desktop_index = -1;
        for (unsigned int i = 0; i < child_count; ++i)
            if (children[i] == desktop)
            {
                desktop_index = static_cast<int>(i);
                break;
            }
        if (desktop_index < 0)
        {
            if (children)
                XFree(children);
            return false;
        }

        std::vector<OcclusionRect> rects;
        for (unsigned int i = 0; i < child_count; ++i)
        {
            const Window window_id = children[i];
            if (window_id == desktop)
                continue;
            XWindowAttributes attributes;
            if (!XGetWindowAttributes(display_, window_id, &attributes) ||
                attributes.map_state != IsViewable || attributes.width <= 1 ||
                attributes.height <= 1)
                continue;

            XClassHint hint{nullptr, nullptr};
            XGetClassHint(display_, window_id, &hint);
            char* name = nullptr;
            XFetchName(display_, window_id, &name);
            Atom type = None;
            int format = 0;
            unsigned long count = 0;
            unsigned long remaining = 0;
            unsigned char* data = nullptr;
            if (XGetWindowProperty(display_, window_id, type_atom_, 0, 1, False, XA_ATOM, &type,
                                   &format, &count, &remaining, &data) == Success &&
                data)
            {
                if (count > 0)
                    type = *reinterpret_cast<Atom*>(data);
                XFree(data);
            }
            const bool has_identity = (hint.res_class && *hint.res_class) ||
                                      (hint.res_name && *hint.res_name) || (name && *name) ||
                                      type != None;
            const bool dock = type == dock_atom_;
            const bool panel =
                dock || (hint.res_class && std::strcmp(hint.res_class, "plasmashell") == 0);
            const bool below_desktop = static_cast<int>(i) < desktop_index;
            const bool transparent = window_is_transparent(window_id);
            if (!has_identity || transparent || (below_desktop && !panel))
            {
                if (hint.res_class)
                    XFree(hint.res_class);
                if (hint.res_name)
                    XFree(hint.res_name);
                if (name)
                    XFree(name);
                continue;
            }

            int x = 0;
            int y = 0;
            if (!XTranslateCoordinates(display_, window_id, root_, 0, 0, &x, &y, &child))
            {
                if (hint.res_class)
                    XFree(hint.res_class);
                if (hint.res_name)
                    XFree(hint.res_name);
                if (name)
                    XFree(name);
                continue;
            }
            OcclusionRect rect{x, y, x + attributes.width, y + attributes.height};
            int left = 0;
            int right = 0;
            int top = 0;
            int bottom = 0;
            int frame_format = 0;
            unsigned long frame_count = 0;
            unsigned long frame_remaining = 0;
            unsigned char* frame_data = nullptr;
            if (XGetWindowProperty(display_, window_id, frame_extents_atom_, 0, 4, False,
                                   XA_CARDINAL, &type, &frame_format, &frame_count,
                                   &frame_remaining, &frame_data) == Success &&
                frame_data)
            {
                if (frame_count == 4)
                {
                    const auto* extents = reinterpret_cast<unsigned long*>(frame_data);
                    left = static_cast<int>(extents[0]);
                    right = static_cast<int>(extents[1]);
                    top = static_cast<int>(extents[2]);
                    bottom = static_cast<int>(extents[3]);
                }
                XFree(frame_data);
            }
            rect = expand(rect, left, right, top, bottom);
            rect = intersect(rect, bounds);
            if (rect.left < rect.right && rect.top < rect.bottom)
            {
                if (rect.left <= bounds.left && rect.top <= bounds.top &&
                    rect.right >= bounds.right && rect.bottom >= bounds.bottom)
                {
                    if (children)
                        XFree(children);
                    if (hint.res_class)
                        XFree(hint.res_class);
                    if (hint.res_name)
                        XFree(hint.res_name);
                    if (name)
                        XFree(name);
                    return true;
                }
                rects.push_back(rect);
            }

            if (hint.res_class)
                XFree(hint.res_class);
            if (hint.res_name)
                XFree(hint.res_name);
            if (name)
                XFree(name);
        }
        if (children)
            XFree(children);
        // Leave a small tolerance for one-pixel compositor gaps. A visible
        // wallpaper edge is not worth an extra full-frame render.
        return fully_covered(rects, bounds);
    }

  private:
    bool window_is_transparent(Window window)
    {
        Atom actual = None;
        int format = 0;
        unsigned long count = 0;
        unsigned long remaining = 0;
        unsigned char* data = nullptr;
        if (XGetWindowProperty(display_, window, opacity_atom_, 0, 1, False, XA_CARDINAL, &actual,
                               &format, &count, &remaining, &data) == Success &&
            data)
        {
            const bool transparent =
                count > 0 && *reinterpret_cast<unsigned long*>(data) < 0xFFFFFFFFUL;
            XFree(data);
            return transparent;
        }
        return false;
    }

    Display* display_{nullptr};
    Window root_{0};
    Atom dock_atom_{None};
    Atom type_atom_{None};
    Atom frame_extents_atom_{None};
    Atom opacity_atom_{None};
};
#endif
} // namespace

std::unique_ptr<OcclusionProvider> make_occlusion_provider()
{
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return make_kwin_occlusion_provider();
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
