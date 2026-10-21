#include "host.hpp"

#include "nekomote/abi.h"

#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/shape.h>
#if defined(NEKOMOTE_HAS_VULKAN)
#include "nekomote/host_vulkan/host.h"
#endif
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nekomote::x11
{
namespace
{
using Clock = std::chrono::steady_clock;
volatile std::sig_atomic_t stop_requested = 0;

void check(nekomote_error result, const char* operation)
{
    if (result != NEKOMOTE_OK)
        throw std::runtime_error(std::string(operation) + ": " + nekomote_error_message());
}

uint64_t timestamp_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch())
            .count());
}

nekomote_gl_proc load_gl(const char* name)
{
    return glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name));
}

struct XFreeDeleter
{
    void operator()(void* data) const noexcept
    {
        if (data)
            XFree(data);
    }
};
template <typename T>
using XOwned = std::unique_ptr<T, XFreeDeleter>;

// Xlib only provides a process-wide C error hook. Scope it to synchronous
// setup/property transactions on this standalone host's sole X11 thread;
// never leave a callback pointing to a destroyed owner or synchronize per frame.
class XTransaction
{
  public:
    explicit XTransaction(Display* display) : display_(display)
    {
        XSync(display_, False);
        previous_ = XSetErrorHandler(handle);
        parent_ = active_;
        active_ = this;
    }
    ~XTransaction()
    {
        finish();
    }
    int finish() noexcept
    {
        if (active_ == this)
        {
            XSync(display_, False);
            XSetErrorHandler(previous_);
            active_ = parent_;
        }
        return error_;
    }

  private:
    static int handle(Display*, XErrorEvent* event) noexcept
    {
        if (active_ && !active_->error_)
            active_->error_ = event->error_code;
        return 0;
    }
    static thread_local XTransaction* active_;
    Display* display_;
    XErrorHandler previous_ = nullptr;
    XTransaction* parent_ = nullptr;
    int error_ = 0;
};
thread_local XTransaction* XTransaction::active_ = nullptr;

std::vector<unsigned long> property(Display* display, Window window, Atom name, Atom type)
{
    constexpr long kMaxPropertyItems = 4096;
    Atom actual_type = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char* raw = nullptr;
    XTransaction transaction(display);
    const int result = XGetWindowProperty(display, window, name, 0, kMaxPropertyItems, False, type,
                                          &actual_type, &format, &count, &remaining, &raw);
    XOwned<unsigned char> data(raw);
    if (transaction.finish() || result != Success || actual_type != type || format != 32 ||
        remaining || (count && !raw))
        return {};
    const auto* values = reinterpret_cast<const unsigned long*>(raw);
    return count ? std::vector<unsigned long>(values, values + count)
                 : std::vector<unsigned long>{};
}

bool extension(const char* list, std::string_view wanted)
{
    if (!list)
        return false;
    std::string_view remaining(list);
    while (!remaining.empty())
    {
        const auto space = remaining.find(' ');
        if (remaining.substr(0, space) == wanted)
            return true;
        if (space == std::string_view::npos)
            break;
        remaining.remove_prefix(space + 1);
    }
    return false;
}

struct Output
{
    std::string name;
    RROutput id = None;
    int x = 0, y = 0, width = 0, height = 0;
    bool primary = false;
};

std::vector<Output> outputs(Display* display, Window root)
{
    XTransaction transaction(display);
    std::unique_ptr<XRRScreenResources, decltype(&XRRFreeScreenResources)> resources(
        XRRGetScreenResourcesCurrent(display, root), XRRFreeScreenResources);
    if (!resources)
        throw std::runtime_error("cannot query XRandR screen resources");
    const RROutput primary = XRRGetOutputPrimary(display, root);
    std::vector<Output> result;
    for (int i = 0; i < resources->noutput; ++i)
    {
        std::unique_ptr<XRROutputInfo, decltype(&XRRFreeOutputInfo)> info(
            XRRGetOutputInfo(display, resources.get(), resources->outputs[i]), XRRFreeOutputInfo);
        if (!info || info->connection != RR_Connected || info->crtc == None)
            continue;
        std::unique_ptr<XRRCrtcInfo, decltype(&XRRFreeCrtcInfo)> crtc(
            XRRGetCrtcInfo(display, resources.get(), info->crtc), XRRFreeCrtcInfo);
        if (!crtc || !crtc->width || !crtc->height || crtc->width > INT_MAX ||
            crtc->height > INT_MAX)
            continue;
        result.push_back({std::string(info->name, static_cast<size_t>(info->nameLen)),
                          resources->outputs[i], crtc->x, crtc->y, static_cast<int>(crtc->width),
                          static_cast<int>(crtc->height), resources->outputs[i] == primary});
    }
    if (transaction.finish())
        throw std::runtime_error(
            "XRandR topology changed during output discovery; restart to retry");
    return result;
}
} // namespace

void request_stop() noexcept
{
    stop_requested = 1;
}

class Host::Impl
{
  public:
    explicit Impl(Options options) : options_(std::move(options))
    {
    }
    ~Impl()
    {
        // Stop borrowed-device submissions before destroying wallpaper resources.
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_host_.device)
            vkDeviceWaitIdle(vk_host_.device);
#endif
        // The wallpaper and its audio/loading workers die with GL still current.
        if (wallpaper_)
            nekomote_wallpaper_destroy(wallpaper_);
#if defined(NEKOMOTE_HAS_VULKAN)
        vk_host_c_destroy(&vk_host_);
#endif
        if (display_)
        {
            // An asynchronous setup failure can leave a locally assigned XID
            // that the server never created. Cleanup must tolerate that case.
            XTransaction cleanup(display_);
            if (context_)
            {
                glXMakeCurrent(display_, None, nullptr);
                glXDestroyContext(display_, context_);
            }
            if (window_)
                XDestroyWindow(display_, window_);
            if (colormap_)
                XFreeColormap(display_, colormap_);
        }
        if (display_)
            XCloseDisplay(display_);
    }
    void run();

  private:
    Atom atom(const char* name) const
    {
        return XInternAtom(display_, name, False);
    }
    void initialize_display();
    void validate_wm();
    void create_window();
    void initialize_wallpaper();
#if defined(NEKOMOTE_HAS_VULKAN)
    void initialize_vulkan();
#endif
    void update_output();
    void handle_event(const XEvent& event);
    void render();
    void capture();
    void set_mapped(bool mapped);

    Options options_;
    Display* display_ = nullptr;
    Window root_ = None, window_ = None;
    Colormap colormap_ = None;
    GLXContext context_ = nullptr;
#if defined(NEKOMOTE_HAS_VULKAN)
    VkHostC vk_host_{};
#endif
    nekomote_wallpaper* wallpaper_ = nullptr;
    Atom wm_delete_ = None, wm_protocols_ = None;
    int randr_event_ = 0;
    Output output_;
    int width_ = 0, height_ = 0;
    bool mapped_ = false, closed_ = false;
    uint64_t rendered_frames_ = 0;
    Clock::time_point last_frame_{}, next_frame_{};
};

void Host::Impl::initialize_display()
{
    display_ = XOpenDisplay(nullptr);
    if (!display_)
        throw std::runtime_error("cannot connect to X11 DISPLAY");
    root_ = DefaultRootWindow(display_);
    int error_base = 0, major = 1, minor = 3;
    if (!XRRQueryExtension(display_, &randr_event_, &error_base) ||
        !XRRQueryVersion(display_, &major, &minor) || major < 1 || (major == 1 && minor < 3))
        throw std::runtime_error("XRandR 1.3 or newer is required");
    const auto available = outputs(display_, root_);
    if (options_.list_outputs)
    {
        for (const auto& output : available)
            std::cout << output.name << ' ' << output.width << 'x' << output.height << '+'
                      << output.x << '+' << output.y << (output.primary ? " primary" : "") << '\n';
        return;
    }
    if (available.empty())
        throw std::runtime_error("no active XRandR output");
    auto chosen = std::find_if(
        available.begin(), available.end(), [&](const Output& value)
        { return options_.output.empty() ? value.primary : value.name == options_.output; });
    if (chosen == available.end())
    {
        if (!options_.output.empty())
            throw std::runtime_error("requested XRandR output is not active: " + options_.output);
        chosen = available.begin();
    }
    output_ = *chosen;
    width_ = output_.width;
    height_ = output_.height;
    XRRSelectInput(display_, root_,
                   RRScreenChangeNotifyMask | RRCrtcChangeNotifyMask | RROutputChangeNotifyMask);
}

void Host::Impl::validate_wm()
{
    const auto check_atom = atom("_NET_SUPPORTING_WM_CHECK");
    const auto wm = property(display_, root_, check_atom, XA_WINDOW);
    if (wm.size() != 1 || wm[0] == None || property(display_, wm[0], check_atom, XA_WINDOW) != wm)
        throw std::runtime_error("an EWMH window manager is required (_NET_SUPPORTING_WM_CHECK)");
    const auto supported = property(display_, root_, atom("_NET_SUPPORTED"), XA_ATOM);
    for (const char* name :
         {"_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DESKTOP", "_NET_WM_DESKTOP", "_NET_WM_STATE",
          "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_SKIP_PAGER"})
        if (std::find(supported.begin(), supported.end(), atom(name)) == supported.end())
            throw std::runtime_error(std::string("window manager does not advertise ") + name);
    const auto clients = property(display_, root_, atom("_NET_CLIENT_LIST"), XA_WINDOW);
    for (const auto client : clients)
    {
        const auto types = property(display_, client, atom("_NET_WM_WINDOW_TYPE"), XA_ATOM);
        if (std::find(types.begin(), types.end(), atom("_NET_WM_WINDOW_TYPE_DESKTOP")) !=
            types.end())
        {
            std::cerr << "Another desktop window exists; background/icon/menu coexistence is "
                         "desktop-specific. No existing window or setting will be modified.\n";
            break;
        }
    }
}

void Host::Impl::create_window()
{
    XTransaction setup(display_);
    const char* extensions = nullptr;
    XOwned<GLXFBConfig> configs;
    XOwned<XVisualInfo> gl_visual;
    Visual* visual = DefaultVisual(display_, DefaultScreen(display_));
    int depth = DefaultDepth(display_, DefaultScreen(display_));
    if (options_.backend == Backend::opengl)
    {
        extensions = glXQueryExtensionsString(display_, DefaultScreen(display_));
        if (!extension(extensions, "GLX_ARB_create_context") ||
            !extension(extensions, "GLX_ARB_create_context_profile"))
            throw std::runtime_error("GLX_ARB_create_context and core profiles are required");
        const std::array<int, 19> attributes{GLX_X_RENDERABLE,
                                             True,
                                             GLX_DRAWABLE_TYPE,
                                             GLX_WINDOW_BIT,
                                             GLX_RENDER_TYPE,
                                             GLX_RGBA_BIT,
                                             GLX_X_VISUAL_TYPE,
                                             GLX_TRUE_COLOR,
                                             GLX_RED_SIZE,
                                             8,
                                             GLX_GREEN_SIZE,
                                             8,
                                             GLX_BLUE_SIZE,
                                             8,
                                             GLX_ALPHA_SIZE,
                                             0,
                                             GLX_DOUBLEBUFFER,
                                             True,
                                             None};
        int count = 0;
        configs.reset(
            glXChooseFBConfig(display_, DefaultScreen(display_), attributes.data(), &count));
        if (!configs || count <= 0)
            throw std::runtime_error("no compatible GLX framebuffer configuration");
        gl_visual.reset(glXGetVisualFromFBConfig(display_, configs.get()[0]));
        if (!gl_visual)
            throw std::runtime_error("no X11 visual for GLX framebuffer");
        visual = gl_visual->visual;
        depth = gl_visual->depth;
    }
    colormap_ = XCreateColormap(display_, root_, visual, AllocNone);
    XSetWindowAttributes settings{};
    settings.colormap = colormap_;
    settings.event_mask = StructureNotifyMask | PointerMotionMask | EnterWindowMask |
                          LeaveWindowMask | ButtonPressMask | ButtonReleaseMask | ExposureMask;
    window_ = XCreateWindow(display_, root_, output_.x, output_.y, static_cast<unsigned>(width_),
                            static_cast<unsigned>(height_), 0, depth, InputOutput, visual,
                            CWColormap | CWEventMask, &settings);
    if (!window_)
        throw std::runtime_error("cannot create X11 desktop window");
    const auto set_property =
        [&](const char* name, Atom type, const unsigned long* values, int size)
    {
        XChangeProperty(display_, window_, atom(name), type, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(values), size);
    };
    const unsigned long desktop_type = atom("_NET_WM_WINDOW_TYPE_DESKTOP");
    set_property("_NET_WM_WINDOW_TYPE", XA_ATOM, &desktop_type, 1);
    const unsigned long all_desktops = 0xffffffffUL;
    set_property("_NET_WM_DESKTOP", XA_CARDINAL, &all_desktops, 1);
    const std::array<unsigned long, 2> states{atom("_NET_WM_STATE_SKIP_TASKBAR"),
                                              atom("_NET_WM_STATE_SKIP_PAGER")};
    set_property("_NET_WM_STATE", XA_ATOM, states.data(), static_cast<int>(states.size()));
    const unsigned long pid = static_cast<unsigned long>(getpid());
    set_property("_NET_WM_PID", XA_CARDINAL, &pid, 1);
    std::array<char, 256> hostname{};
    if (gethostname(hostname.data(), hostname.size()) != 0 || hostname.back() != '\0')
        throw std::runtime_error("cannot obtain WM_CLIENT_MACHINE hostname");
    XTextProperty machine{reinterpret_cast<unsigned char*>(hostname.data()), XA_STRING, 8,
                          static_cast<unsigned long>(std::strlen(hostname.data()))};
    XSetWMClientMachine(display_, window_, &machine);
    XWMHints hints{};
    hints.flags = InputHint;
    hints.input = False;
    XSetWMHints(display_, window_, &hints);
    char instance[] = "nekomote_x11", class_name[] = "NekoMote";
    XClassHint class_hint{instance, class_name};
    XSetClassHint(display_, window_, &class_hint);
    XStoreName(display_, window_, "NekoMote X11 wallpaper");
    wm_delete_ = atom("WM_DELETE_WINDOW");
    wm_protocols_ = atom("WM_PROTOCOLS");
    XSetWMProtocols(display_, window_, &wm_delete_, 1);
    if (options_.input == InputMode::passthrough)
    {
        int shape_event = 0, shape_error = 0, major = 1, minor = 1;
        if (!XShapeQueryExtension(display_, &shape_event, &shape_error) ||
            !XShapeQueryVersion(display_, &major, &minor) || major < 1 || (major == 1 && minor < 1))
            throw std::runtime_error("passthrough requires X Shape 1.1 input regions");
        XShapeCombineRectangles(display_, window_, ShapeInput, 0, 0, nullptr, 0, ShapeSet,
                                Unsorted);
    }
    if (options_.backend == Backend::opengl)
    {
        auto create = reinterpret_cast<PFNGLXCREATECONTEXTATTRIBSARBPROC>(
            load_gl("glXCreateContextAttribsARB"));
        if (!create)
            throw std::runtime_error("GLX core context creation function unavailable");
        const std::array<int, 7> context_attributes{GLX_CONTEXT_MAJOR_VERSION_ARB,
                                                    3,
                                                    GLX_CONTEXT_MINOR_VERSION_ARB,
                                                    3,
                                                    GLX_CONTEXT_PROFILE_MASK_ARB,
                                                    GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                                                    None};
        XTransaction transaction(display_);
        context_ = create(display_, configs.get()[0], nullptr, True, context_attributes.data());
        if (transaction.finish() || !context_)
            throw std::runtime_error("cannot create OpenGL 3.3 core context");
        if (!glXMakeCurrent(display_, window_, context_))
            throw std::runtime_error("cannot make GLX context current");
        if (extension(extensions, "GLX_EXT_swap_control"))
        {
            auto interval =
                reinterpret_cast<PFNGLXSWAPINTERVALEXTPROC>(load_gl("glXSwapIntervalEXT"));
            if (interval)
                interval(display_, window_, 0);
        }
        else if (extension(extensions, "GLX_MESA_swap_control"))
        {
            auto interval =
                reinterpret_cast<PFNGLXSWAPINTERVALMESAPROC>(load_gl("glXSwapIntervalMESA"));
            if (interval && interval(0) != 0)
                std::cerr << "Cannot disable GLX swap interval; frame pacing may include vsync\n";
        }
        else
            std::cerr << "No GLX extension to disable vsync; frame pacing may include vsync\n";
    }
#if defined(NEKOMOTE_HAS_VULKAN)
    else
        initialize_vulkan();
#endif
    XMapWindow(display_, window_);
    XFlush(display_);
    if (setup.finish())
        throw std::runtime_error("X11 desktop window/graphics initialization failed");
    std::cout << "X11 wallpaper window=0x" << std::hex << window_ << std::dec
              << " backend=" << (options_.backend == Backend::opengl ? "opengl" : "vulkan")
              << " output=" << output_.name << " geometry=" << width_ << 'x' << height_ << '+'
              << output_.x << '+' << output_.y << std::endl;
}

void Host::Impl::initialize_wallpaper()
{
#if defined(NEKOMOTE_HAS_VULKAN)
    if (options_.backend == Backend::vulkan)
    {
        nekomote_vk_device_context context{};
        context.instance = vk_host_.instance;
        context.physical_device = vk_host_.physical_device;
        context.device = vk_host_.device;
        context.graphics_queue = vk_host_.graphics_queue;
        context.graphics_queue_family = vk_host_.graphics_family;
        context.color_format = VK_FORMAT_R8G8B8A8_UNORM;
        wallpaper_ = nekomote_wallpaper_create_vk(context);
    }
    else
#endif
        wallpaper_ = nekomote_wallpaper_create(load_gl);
    if (!wallpaper_)
    {
        check(nekomote_last_error(), "create wallpaper");
        throw std::runtime_error("create wallpaper returned a null handle");
    }
    const auto config =
        options_.config.empty() ? default_config_path(output_.name) : options_.config;
    check(nekomote_wallpaper_set_no_save(wallpaper_, options_.no_save), "set no-save");
    check(nekomote_wallpaper_set_config_path(wallpaper_, config.c_str()), "set configuration path");
    if (options_.frames)
        check(nekomote_wallpaper_set_seed(wallpaper_, 1), "set diagnostic random seed");
    check(nekomote_wallpaper_set_output_size(wallpaper_, width_, height_), "set output size");
    if (!options_.assets.empty())
    {
        check(nekomote_wallpaper_set_assets_dir(wallpaper_, options_.assets.c_str()),
              "set assets directory");
        if (options_.muted)
            check(nekomote_wallpaper_set_muted(wallpaper_, 1), "force muted audio");
        check(nekomote_wallpaper_load_initial(wallpaper_), "load initial wallpaper");
    }
    else
    {
        if (options_.muted)
            check(nekomote_wallpaper_set_muted(wallpaper_, 1), "force muted audio");
        check(nekomote_wallpaper_load_model(wallpaper_, options_.model.c_str(),
                                            options_.session.empty() ? nullptr
                                                                     : options_.session.c_str()),
              "load diagnostic model");
    }
    check(nekomote_wallpaper_set_ui_open(wallpaper_, options_.ui_open), "set control panel state");
    std::cerr << "X11 local pointer "
              << (options_.input == InputMode::interactive ? "enabled" : "passthrough")
              << "; keyboard focus/global pointer/automatic occlusion pause not requested\n";
}

#if defined(NEKOMOTE_HAS_VULKAN)
void Host::Impl::initialize_vulkan()
{
    const char* extensions[]{VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME};
    if (vk_host_c_create_instance(&vk_host_, extensions, 2) != 0)
        throw std::runtime_error("cannot create Vulkan X11 instance");
    VkXlibSurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    info.dpy = display_;
    info.window = window_;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const auto result = vkCreateXlibSurfaceKHR(vk_host_.instance, &info, nullptr, &surface);
    if (result != VK_SUCCESS)
        throw std::runtime_error("cannot create Vulkan X11 surface: " +
                                 std::to_string(static_cast<int>(result)));
    // select_device takes ownership of the surface even on failure.
    if (vk_host_c_select_device(&vk_host_, surface) != 0 || vk_host_c_create_device(&vk_host_) != 0)
        throw std::runtime_error("cannot initialize Vulkan X11 device");
}
#endif

void Host::Impl::set_mapped(bool mapped)
{
    if (mapped_ == mapped)
        return;
    mapped_ = mapped;
    check(nekomote_wallpaper_set_output_available(wallpaper_, mapped_), "set host availability");
    check(nekomote_wallpaper_advance(wallpaper_, 0), "flush host availability audio commands");
    last_frame_ = next_frame_ = Clock::now();
}

void Host::Impl::update_output()
{
    const auto available = outputs(display_, root_);
    const auto selected =
        std::find_if(available.begin(), available.end(), [&](const Output& value)
                     { return value.id == output_.id && value.name == output_.name; });
    if (selected == available.end())
    {
        std::cerr << "Selected XRandR output removed; stopping\n";
        closed_ = true;
        return;
    }
    if (selected->x != output_.x || selected->y != output_.y || selected->width != output_.width ||
        selected->height != output_.height)
    {
        output_ = *selected;
        XMoveResizeWindow(display_, window_, output_.x, output_.y,
                          static_cast<unsigned>(output_.width),
                          static_cast<unsigned>(output_.height));
        XFlush(display_);
    }
}

void Host::Impl::handle_event(const XEvent& event)
{
    if (event.type == randr_event_ + RRScreenChangeNotify || event.type == randr_event_ + RRNotify)
    {
        XEvent copy = event;
        XRRUpdateConfiguration(&copy);
        update_output();
        return;
    }
    if (event.xany.window != window_)
        return;
    switch (event.type)
    {
        case MapNotify:
            set_mapped(true);
            break;
        case UnmapNotify:
            set_mapped(false);
            break;
        case DestroyNotify:
            window_ = None;
            closed_ = true;
            break;
        case ConfigureNotify:
            if (event.xconfigure.width != width_ || event.xconfigure.height != height_)
            {
                width_ = event.xconfigure.width;
                height_ = event.xconfigure.height;
                if (width_ > 0 && height_ > 0)
                    check(nekomote_wallpaper_set_output_size(wallpaper_, width_, height_),
                          "resize wallpaper");
            }
            break;
        case ClientMessage:
            if (event.xclient.message_type == wm_protocols_ && event.xclient.format == 32 &&
                static_cast<Atom>(event.xclient.data.l[0]) == wm_delete_)
                closed_ = true;
            break;
        default:
            break;
    }
    if (options_.input != InputMode::interactive)
        return;
    switch (event.type)
    {
        case EnterNotify:
            check(
                nekomote_wallpaper_pointer_enter(wallpaper_, event.xcrossing.x, event.xcrossing.y),
                "pointer enter");
            break;
        case LeaveNotify:
            check(nekomote_wallpaper_pointer_leave(wallpaper_), "pointer leave");
            break;
        case MotionNotify:
            check(nekomote_wallpaper_pointer_move(wallpaper_, event.xmotion.x, event.xmotion.y,
                                                  timestamp_ms()),
                  "pointer motion");
            break;
        case ButtonPress:
        case ButtonRelease:
        {
            const auto button = event.xbutton.button;
            if (button == Button1 || button == Button3)
            {
                check(nekomote_wallpaper_pointer_move(wallpaper_, event.xbutton.x, event.xbutton.y,
                                                      timestamp_ms()),
                      "button position");
                check(nekomote_wallpaper_mouse_button(
                          wallpaper_, button == Button1 ? 0 : 1,
                          event.type == ButtonPress ? NEKOMOTE_MOUSE_PRESS : NEKOMOTE_MOUSE_RELEASE,
                          timestamp_ms()),
                      "pointer button");
            }
            else if (event.type == ButtonPress && (button == Button4 || button == Button5))
            {
                check(nekomote_wallpaper_pointer_move(wallpaper_, event.xbutton.x, event.xbutton.y,
                                                      timestamp_ms()),
                      "scroll position");
                check(nekomote_wallpaper_mouse_scroll(wallpaper_, button == Button4 ? 1.0 : -1.0),
                      "pointer scroll");
            }
            break;
        }
        default:
            break;
    }
}

void Host::Impl::capture()
{
    const auto row_bytes = static_cast<size_t>(width_) * 3;
    if (static_cast<size_t>(height_) > std::numeric_limits<size_t>::max() / row_bytes)
        throw std::runtime_error("capture dimensions overflow");
    std::vector<uint8_t> pixels(row_bytes * static_cast<size_t>(height_));
#if defined(NEKOMOTE_HAS_VULKAN)
    if (options_.backend == Backend::vulkan)
    {
        if (pixels.size() / 3 > std::numeric_limits<size_t>::max() / 4)
            throw std::runtime_error("capture RGBA dimensions overflow");
        std::vector<uint8_t> rgba(pixels.size() / 3 * 4);
        if (vk_host_c_capture(&vk_host_, width_, height_, rgba.data()) != 0)
            throw std::runtime_error("diagnostic Vulkan framebuffer readback failed");
        // The helper returns top-left straight RGBA. Composite on black to
        // match the opaque GLX framebuffer, then store bottom-up for the writer.
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x)
            {
                const auto source = (static_cast<size_t>(y) * width_ + x) * 4;
                const auto destination =
                    static_cast<size_t>(height_ - 1 - y) * row_bytes + static_cast<size_t>(x) * 3;
                for (size_t channel = 0; channel < 3; ++channel)
                    pixels[destination + channel] = static_cast<uint8_t>(
                        (static_cast<unsigned>(rgba[source + channel]) * rgba[source + 3] + 127) /
                        255);
            }
    }
    else
#endif
    {
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        if (glGetError() != GL_NO_ERROR)
            throw std::runtime_error("diagnostic framebuffer readback failed");
    }
    const int descriptor =
        open(options_.capture.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (descriptor < 0)
        throw std::runtime_error("cannot create diagnostic capture file");
    const auto close_stream = [](FILE* file) { std::fclose(file); };
    std::unique_ptr<FILE, decltype(close_stream)> stream(fdopen(descriptor, "wb"), close_stream);
    if (!stream)
    {
        close(descriptor);
        throw std::runtime_error("cannot create diagnostic capture stream");
    }
    if (std::fprintf(stream.get(), "P6\n%d %d\n255\n", width_, height_) < 0)
        throw std::runtime_error("cannot write capture header");
    for (int row = height_ - 1; row >= 0; --row)
        if (std::fwrite(pixels.data() + static_cast<size_t>(row) * row_bytes, 1, row_bytes,
                        stream.get()) != row_bytes)
            throw std::runtime_error("cannot write capture pixels");
    if (std::fclose(stream.release()) != 0)
        throw std::runtime_error("cannot close capture file");
}

void Host::Impl::render()
{
    const auto now = Clock::now();
    const double dt = options_.frames
                          ? 1.0 / 60.0
                          : std::min(std::chrono::duration<double>(now - last_frame_).count(), .25);
    check(nekomote_wallpaper_advance(wallpaper_, dt), "advance wallpaper");
#if defined(NEKOMOTE_HAS_VULKAN)
    if (options_.backend == Backend::vulkan)
    {
        nekomote_vk_target target{};
        if (vk_host_c_begin_frame(&vk_host_, width_, height_, options_.capture.empty(), &target) !=
            0)
            throw std::runtime_error("cannot begin Vulkan frame");
        check(nekomote_wallpaper_set_vk_target(wallpaper_, target), "set Vulkan target");
    }
#endif
    check(nekomote_wallpaper_render_frame(wallpaper_), "render wallpaper");
    check(nekomote_wallpaper_render_ui(wallpaper_, static_cast<float>(dt)), "render controls");
    if (!options_.capture.empty() && rendered_frames_ + 1 == options_.frames)
        capture();
#if defined(NEKOMOTE_HAS_VULKAN)
    if (options_.backend == Backend::vulkan)
    {
        if (vk_host_c_present(&vk_host_) < 0)
            throw std::runtime_error("cannot present Vulkan frame");
    }
    else
#endif
        glXSwapBuffers(display_, window_);
    ++rendered_frames_;
    last_frame_ = now;
    const int fps = nekomote_wallpaper_fps_limit(wallpaper_);
    if (fps < 0)
        throw std::runtime_error("invalid wallpaper FPS limit");
    next_frame_ = fps ? now + std::chrono::duration_cast<Clock::duration>(
                                  std::chrono::duration<double>(1.0 / fps))
                      : now;
    if (options_.frames && rendered_frames_ >= options_.frames)
        closed_ = true;
}

void Host::Impl::run()
{
    initialize_display();
    if (options_.list_outputs)
        return;
    validate_wm();
    create_window();
    initialize_wallpaper();
    last_frame_ = next_frame_ = Clock::now();
    while (!closed_ && !stop_requested)
    {
#if defined(NEKOMOTE_HAS_VULKAN)
        // Events can resize or advance the wallpaper (availability flush), so
        // retire the previous submission before both input and frame mutation.
        if (options_.backend == Backend::vulkan && vk_host_c_wait_ready(&vk_host_) != 0)
            throw std::runtime_error("cannot retire Vulkan frame");
#endif
        // A busy input producer cannot starve presentation indefinitely.
        for (size_t count = 0; count < 1024 && XPending(display_); ++count)
        {
            XEvent event{};
            XNextEvent(display_, &event);
            handle_event(event);
            if (closed_)
                break;
        }
        if (closed_ || stop_requested)
            break;
        if (mapped_ && width_ > 0 && height_ > 0 && Clock::now() >= next_frame_)
            render();
        if (closed_ || XPending(display_))
            continue;
        const auto wait =
            std::chrono::ceil<std::chrono::milliseconds>(next_frame_ - Clock::now()).count();
        // Bound signal latency even when unmapped. No per-frame XSync/glFinish.
        const int timeout = mapped_ ? static_cast<int>(std::clamp<int64_t>(wait, 0, 250)) : 250;
        pollfd descriptor{ConnectionNumber(display_), POLLIN, 0};
        const int result = poll(&descriptor, 1, timeout);
        if (result < 0 && errno != EINTR)
            throw std::runtime_error("poll X11 connection failed");
        if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))
            throw std::runtime_error("X11 connection closed");
    }
}

Host::Host(Options options) : impl_(std::make_unique<Impl>(std::move(options)))
{
}
Host::~Host() = default;
void Host::run()
{
    impl_->run();
}
} // namespace nekomote::x11
