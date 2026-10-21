#include "host.hpp"

#include "nekomote/abi.h"
#if defined(NEKOMOTE_HAS_VULKAN)
#include "nekomote/host_vulkan/host.h"
#endif

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace nekomote::layer_shell
{
namespace
{
using Clock = std::chrono::steady_clock;
volatile std::sig_atomic_t stop_requested = 0;

uint64_t timestamp_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch())
            .count());
}

void check(nekomote_error result, const char* operation)
{
    if (result != NEKOMOTE_OK)
    {
        const char* message = nekomote_error_message();
        throw std::runtime_error(std::string(operation) + ": " +
                                 (message ? message : "ABI failure"));
    }
}

void egl_check(EGLBoolean result, const char* operation)
{
    if (result != EGL_TRUE)
    {
        throw std::runtime_error(std::string(operation) + ": EGL error " +
                                 std::to_string(eglGetError()));
    }
}

nekomote_gl_proc load_gl(const char* name)
{
    return eglGetProcAddress(name);
}

void release_output(wl_output* output)
{
    if (wl_output_get_version(output) >= 3)
        wl_output_release(output);
    else
        wl_output_destroy(output);
}

void listen_check(int result)
{
    if (result < 0)
        throw std::runtime_error("cannot install Wayland listener");
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
        // Wallpaper renderers borrow the native device. Finish submitted work
        // before releasing their resources, and keep Wayland alive for the host.
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_host_.device)
        {
            const auto result = vkDeviceWaitIdle(vk_host_.device);
            if (result != VK_SUCCESS)
                std::cerr << "Vulkan shutdown wait failed: " << result << '\n';
        }
#endif
        if (wallpaper_)
        {
            nekomote_wallpaper_destroy(wallpaper_);
        }
#if defined(NEKOMOTE_HAS_VULKAN)
        vk_host_c_destroy(&vk_host_);
#endif
        if (egl_display_ != EGL_NO_DISPLAY)
        {
            eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (egl_surface_ != EGL_NO_SURFACE)
                eglDestroySurface(egl_display_, egl_surface_);
            if (egl_context_ != EGL_NO_CONTEXT)
                eglDestroyContext(egl_display_, egl_context_);
            eglTerminate(egl_display_);
        }
        if (egl_window_)
            wl_egl_window_destroy(egl_window_);
        if (frame_callback_)
            wl_callback_destroy(frame_callback_);
        if (pointer_)
            wl_pointer_release(pointer_);
        if (cursor_surface_)
            wl_surface_destroy(cursor_surface_);
        if (cursor_theme_)
            wl_cursor_theme_destroy(cursor_theme_);
        if (seat_)
            wl_seat_release(seat_);
        if (layer_surface_)
            zwlr_layer_surface_v1_destroy(layer_surface_);
        if (surface_)
            wl_surface_destroy(surface_);
        outputs_.clear();
        if (shell_)
            // The shell destroy request was added in v3; this host binds v1.
            wl_proxy_destroy(reinterpret_cast<wl_proxy*>(shell_));
        if (shm_)
            wl_shm_destroy(shm_);
        if (compositor_)
            wl_compositor_destroy(compositor_);
        if (registry_)
            wl_registry_destroy(registry_);
        if (display_)
            wl_display_disconnect(display_);
    }

    void run();

  private:
    struct Output
    {
        ~Output()
        {
            if (object)
                release_output(object);
        }
        Impl* host = nullptr;
        wl_output* object = nullptr;
        uint32_t global = 0;
        std::string name;
        int scale = 1;
        bool entered = false;
    };
    enum class InputKind
    {
        enter,
        leave,
        motion,
        button,
        scroll
    };
    struct Input
    {
        InputKind kind;
        double x = 0;
        double y = 0;
        uint64_t time = 0;
        int button = 0;
        nekomote_mouse_action action = NEKOMOTE_MOUSE_RELEASE;
    };
    template <typename Function>
    void callback(Function&& function) noexcept
    {
        if (failure_)
            return;
        try
        {
            function();
        }
        catch (...)
        {
            failure_ = std::current_exception();
        }
    }
    void rethrow_failure()
    {
        if (failure_)
            std::rethrow_exception(failure_);
    }
    void queue(Input input)
    {
        if (input_count_ == inputs_.size())
        {
            throw std::runtime_error("local pointer event queue overflow");
        }
        inputs_[input_count_++] = input;
    }
    void initialize_wayland();
    void create_surface();
    void initialize_egl();
    void initialize_graphics();
#if defined(NEKOMOTE_HAS_VULKAN)
    void initialize_vulkan();
#endif
    void resize();
    void process_input();
    void render();
    void capture();
    void wait_events(int timeout);
    void set_pointer(uint32_t capabilities);
    void initialize_cursor();
    void update_cursor();
    void update_scale();
    static const wl_registry_listener& registry_listener();
    static const wl_output_listener& output_listener();
    static const wl_surface_listener& surface_listener();
    static const wl_seat_listener& seat_listener();
    static const wl_pointer_listener& pointer_listener();
    static const zwlr_layer_surface_v1_listener& layer_listener();
    static const wl_callback_listener& frame_listener();

    Options options_;
    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    wl_shm* shm_ = nullptr;
    zwlr_layer_shell_v1* shell_ = nullptr;
    wl_surface* surface_ = nullptr;
    zwlr_layer_surface_v1* layer_surface_ = nullptr;
    wl_seat* seat_ = nullptr;
    uint32_t seat_global_ = 0;
    wl_pointer* pointer_ = nullptr;
    wl_surface* cursor_surface_ = nullptr;
    wl_cursor_theme* cursor_theme_ = nullptr;
    wl_cursor* cursor_ = nullptr;
    int cursor_scale_ = 0;
    uint32_t pointer_enter_serial_ = 0;
    wl_callback* frame_callback_ = nullptr;
    std::vector<std::unique_ptr<Output>> outputs_;
    Output* selected_ = nullptr;
    EGLDisplay egl_display_ = EGL_NO_DISPLAY;
    EGLContext egl_context_ = EGL_NO_CONTEXT;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;
    wl_egl_window* egl_window_ = nullptr;
#if defined(NEKOMOTE_HAS_VULKAN)
    VkHostC vk_host_{};
#endif
    nekomote_wallpaper* wallpaper_ = nullptr;
    std::exception_ptr failure_;
    std::array<Input, 1024> inputs_{};
    size_t input_count_ = 0;
    uint32_t logical_width_ = 0;
    uint32_t logical_height_ = 0;
    int scale_ = 1;
    int pixel_width_ = 0;
    int pixel_height_ = 0;
    bool needs_resize_ = true;
    bool configured_ = false;
    bool closed_ = false;
    bool frame_ready_ = true;
    bool retry_frame_ = false;
    bool pointer_inside_ = false;
    uint64_t rendered_frames_ = 0;
    Clock::time_point last_frame_{};
    Clock::time_point next_frame_{};
};

const wl_registry_listener& Host::Impl::registry_listener()
{
    static const wl_registry_listener listener{
        [](void* data, wl_registry* registry, uint32_t name, const char* interface,
           uint32_t version)
        {
            auto& self = *static_cast<Impl*>(data);
            self.callback(
                [&]
                {
                    if (std::strcmp(interface, wl_compositor_interface.name) == 0 &&
                        !self.compositor_)
                    {
                        if (version < 3)
                            throw std::runtime_error("wl_compositor version 3 is required");
                        self.compositor_ = static_cast<wl_compositor*>(wl_registry_bind(
                            registry, name, &wl_compositor_interface, std::min(version, 4U)));
                    }
                    else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0 &&
                             !self.shell_)
                    {
                        self.shell_ = static_cast<zwlr_layer_shell_v1*>(
                            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1));
                    }
                    else if (std::strcmp(interface, wl_shm_interface.name) == 0 && !self.shm_)
                    {
                        self.shm_ = static_cast<wl_shm*>(
                            wl_registry_bind(registry, name, &wl_shm_interface, 1));
                        if (!self.shm_)
                            throw std::runtime_error("cannot bind cursor shared memory interface");
                        static const wl_shm_listener formats{[](void*, wl_shm*, uint32_t) {}};
                        listen_check(wl_shm_add_listener(self.shm_, &formats, nullptr));
                    }
                    else if (std::strcmp(interface, wl_output_interface.name) == 0)
                    {
                        auto output = std::make_unique<Output>();
                        output->host = &self;
                        output->global = name;
                        output->object = static_cast<wl_output*>(wl_registry_bind(
                            registry, name, &wl_output_interface, std::min(version, 4U)));
                        if (!output->object)
                            throw std::runtime_error("cannot bind wl_output");
                        listen_check(wl_output_add_listener(output->object, &output_listener(),
                                                            output.get()));
                        self.outputs_.push_back(std::move(output));
                    }
                    else if (std::strcmp(interface, wl_seat_interface.name) == 0 && !self.seat_ &&
                             version >= 5)
                    {
                        self.seat_global_ = name;
                        self.seat_ = static_cast<wl_seat*>(
                            wl_registry_bind(registry, name, &wl_seat_interface, 5));
                        if (!self.seat_)
                            throw std::runtime_error("cannot bind wl_seat");
                        listen_check(wl_seat_add_listener(self.seat_, &seat_listener(), &self));
                    }
                });
        },
        [](void* data, wl_registry*, uint32_t name)
        {
            auto& self = *static_cast<Impl*>(data);
            self.callback(
                [&]
                {
                    std::erase_if(self.outputs_,
                                  [&](const auto& output)
                                  {
                                      if (output->global != name)
                                          return false;
                                      if (self.selected_ == output.get() || output->entered)
                                      {
                                          std::cerr << "wallpaper output removed; closing\n";
                                          self.closed_ = true;
                                          self.selected_ = nullptr;
                                      }
                                      return true;
                                  });
                    if (self.seat_ && self.seat_global_ == name)
                    {
                        self.set_pointer(0);
                        wl_seat_release(self.seat_);
                        self.seat_ = nullptr;
                    }
                });
        }};
    return listener;
}

const wl_output_listener& Host::Impl::output_listener()
{
    static const auto listener = []
    {
        wl_output_listener value{};
        value.geometry = [](void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t,
                            const char*, const char*, int32_t) {};
        value.mode = [](void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {};
        value.done = [](void* data, wl_output*)
        {
            auto& output = *static_cast<Output*>(data);
            output.host->callback([&] { output.host->update_scale(); });
        };
        value.scale = [](void* data, wl_output*, int32_t factor)
        {
            auto& output = *static_cast<Output*>(data);
            output.host->callback(
                [&]
                {
                    if (factor <= 0)
                        throw std::runtime_error("invalid output scale");
                    output.scale = factor;
                });
        };
        value.name = [](void* data, wl_output*, const char* name)
        {
            auto& output = *static_cast<Output*>(data);
            output.host->callback([&] { output.name = name ? name : ""; });
        };
        value.description = [](void*, wl_output*, const char*) {};
        return value;
    }();
    return listener;
}

const wl_surface_listener& Host::Impl::surface_listener()
{
    static const auto listener = []
    {
        wl_surface_listener value{};
        value.enter = [](void* data, wl_surface*, wl_output* target)
        {
            auto& self = *static_cast<Impl*>(data);
            self.callback(
                [&]
                {
                    for (const auto& output : self.outputs_)
                        if (output->object == target)
                            output->entered = true;
                    self.update_scale();
                });
        };
        value.leave = [](void* data, wl_surface*, wl_output* target)
        {
            auto& self = *static_cast<Impl*>(data);
            self.callback(
                [&]
                {
                    for (const auto& output : self.outputs_)
                        if (output->object == target)
                            output->entered = false;
                    self.update_scale();
                });
        };
        return value;
    }();
    return listener;
}

void Host::Impl::update_scale()
{
    int factor = selected_ ? selected_->scale : 1;
    if (!selected_)
    {
        for (const auto& output : outputs_)
            if (output->entered)
                factor = std::max(factor, output->scale);
    }
    if (factor != scale_)
    {
        scale_ = factor;
        needs_resize_ = true;
        if (cursor_surface_)
            update_cursor();
    }
}

const wl_seat_listener& Host::Impl::seat_listener()
{
    static const wl_seat_listener listener{[](void* data, wl_seat*, uint32_t capabilities)
                                           {
                                               auto& self = *static_cast<Impl*>(data);
                                               self.callback([&]
                                                             { self.set_pointer(capabilities); });
                                           },
                                           [](void*, wl_seat*, const char*) {}};
    return listener;
}

void Host::Impl::set_pointer(uint32_t capabilities)
{
    const bool available = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0 &&
                           options_.input == InputMode::interactive;
    if (available && !pointer_)
    {
        pointer_ = wl_seat_get_pointer(seat_);
        if (!pointer_)
            throw std::runtime_error("cannot create wl_pointer");
        listen_check(wl_pointer_add_listener(pointer_, &pointer_listener(), this));
        if (surface_)
            initialize_cursor();
        std::cerr << "local pointer capability available\n";
    }
    else if (!available && pointer_)
    {
        if (pointer_inside_)
            queue({InputKind::leave});
        pointer_inside_ = false;
        wl_pointer_release(pointer_);
        pointer_ = nullptr;
        std::cerr << "local pointer capability removed\n";
    }
}

void Host::Impl::initialize_cursor()
{
    if (!shm_)
        throw std::runtime_error("interactive cursor requires wl_shm; use --input passthrough");
    if (!cursor_surface_)
    {
        cursor_surface_ = wl_compositor_create_surface(compositor_);
        if (!cursor_surface_)
            throw std::runtime_error("cannot create cursor surface");
    }
    if (cursor_scale_ == scale_)
        return;
    constexpr int kCursorLogicalSize = 24;
    if (scale_ > INT_MAX / kCursorLogicalSize)
        throw std::runtime_error("cursor scale overflows image dimensions");
    auto* theme = wl_cursor_theme_load(nullptr, kCursorLogicalSize * scale_, shm_);
    if (!theme)
        throw std::runtime_error("cannot load default cursor theme");
    auto* cursor = wl_cursor_theme_get_cursor(theme, "left_ptr");
    if (!cursor || !cursor->image_count || !cursor->images[0])
    {
        wl_cursor_theme_destroy(theme);
        throw std::runtime_error("default cursor theme has no left_ptr image");
    }
    if (cursor_theme_)
        wl_cursor_theme_destroy(cursor_theme_);
    cursor_theme_ = theme;
    cursor_ = cursor;
    cursor_scale_ = scale_;
}

void Host::Impl::update_cursor()
{
    initialize_cursor();
    if (!pointer_inside_ || !pointer_)
        return;
    const auto* image = cursor_->images[0];
    // Themes may substitute a nearby nominal size. Wayland requires both
    // buffer dimensions to be exact multiples of the committed buffer scale.
    const auto factor = static_cast<uint32_t>(scale_);
    if (!image->width || !image->height || image->width > static_cast<uint32_t>(INT_MAX) ||
        image->height > static_cast<uint32_t>(INT_MAX) || image->width % factor != 0 ||
        image->height % factor != 0)
        throw std::runtime_error("cursor image dimensions " + std::to_string(image->width) + "x" +
                                 std::to_string(image->height) + " are incompatible with scale " +
                                 std::to_string(scale_));
    auto* buffer = wl_cursor_image_get_buffer(cursor_->images[0]);
    if (!buffer || image->hotspot_x > static_cast<uint32_t>(INT_MAX) ||
        image->hotspot_y > static_cast<uint32_t>(INT_MAX))
        throw std::runtime_error("cursor image cannot be used as a Wayland buffer");
    wl_surface_set_buffer_scale(cursor_surface_, scale_);
    wl_pointer_set_cursor(pointer_, pointer_enter_serial_, cursor_surface_,
                          static_cast<int>(image->hotspot_x) / scale_,
                          static_cast<int>(image->hotspot_y) / scale_);
    wl_surface_attach(cursor_surface_, buffer, 0, 0);
    wl_surface_damage(cursor_surface_, 0, 0, INT_MAX, INT_MAX);
    wl_surface_commit(cursor_surface_);
}

const wl_pointer_listener& Host::Impl::pointer_listener()
{
    static const auto listener = []
    {
        wl_pointer_listener value{};
        value.enter = [](void* data, wl_pointer*, uint32_t serial, wl_surface* target, wl_fixed_t x,
                         wl_fixed_t y)
        {
            auto& self = *static_cast<Impl*>(data);
            self.callback(
                [&]
                {
                    if (target != self.surface_)
                        return;
                    self.pointer_inside_ = true;
                    self.pointer_enter_serial_ = serial;
                    self.update_cursor();
                    self.queue({InputKind::enter, wl_fixed_to_double(x), wl_fixed_to_double(y)});
                });
        };
        value.leave = [](void* data, wl_pointer*, uint32_t, wl_surface*)
        {
            auto& self = *static_cast<Impl*>(data);
            self.callback(
                [&]
                {
                    if (self.pointer_inside_)
                        self.queue({InputKind::leave});
                    self.pointer_inside_ = false;
                });
        };
        value.motion = [](void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y)
        {
            auto& self = *static_cast<Impl*>(data);
            self.callback(
                [&]
                {
                    if (self.pointer_inside_)
                        self.queue({InputKind::motion, wl_fixed_to_double(x), wl_fixed_to_double(y),
                                    timestamp_ms()});
                });
        };
        value.button =
            [](void* data, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state)
        {
            auto& self = *static_cast<Impl*>(data);
            self.callback(
                [&]
                {
                    if (!self.pointer_inside_ || (button != BTN_LEFT && button != BTN_RIGHT))
                        return;
                    self.queue({InputKind::button, 0, 0, timestamp_ms(), button == BTN_LEFT ? 0 : 1,
                                state == WL_POINTER_BUTTON_STATE_PRESSED ? NEKOMOTE_MOUSE_PRESS
                                                                         : NEKOMOTE_MOUSE_RELEASE});
                });
        };
        value.axis = [](void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t amount)
        {
            auto& self = *static_cast<Impl*>(data);
            self.callback(
                [&]
                {
                    // Wayland vertical axis is down-positive, ABI zoom is up-positive.
                    // The conventional wheel step is 10 surface units.
                    if (self.pointer_inside_ && axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
                        self.queue({InputKind::scroll, -wl_fixed_to_double(amount) / 10.0});
                });
        };
        value.frame = [](void*, wl_pointer*) {};
        value.axis_source = [](void*, wl_pointer*, uint32_t) {};
        value.axis_stop = [](void*, wl_pointer*, uint32_t, uint32_t) {};
        value.axis_discrete = [](void*, wl_pointer*, uint32_t, int32_t) {};
        return value;
    }();
    return listener;
}

const zwlr_layer_surface_v1_listener& Host::Impl::layer_listener()
{
    static const zwlr_layer_surface_v1_listener listener{
        [](void* data, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t width,
           uint32_t height)
        {
            auto& self = *static_cast<Impl*>(data);
            self.callback(
                [&]
                {
                    if (!width || !height)
                        throw std::runtime_error("compositor supplied an empty background size");
                    zwlr_layer_surface_v1_ack_configure(layer, serial);
                    self.logical_width_ = width;
                    self.logical_height_ = height;
                    self.configured_ = true;
                    self.needs_resize_ = true;
                });
        },
        [](void* data, zwlr_layer_surface_v1*) { static_cast<Impl*>(data)->closed_ = true; }};
    return listener;
}

const wl_callback_listener& Host::Impl::frame_listener()
{
    static const wl_callback_listener listener{[](void* data, wl_callback* callback, uint32_t)
                                               {
                                                   auto& self = *static_cast<Impl*>(data);
                                                   wl_callback_destroy(callback);
                                                   self.frame_callback_ = nullptr;
                                                   self.frame_ready_ = true;
                                               }};
    return listener;
}

void Host::Impl::initialize_wayland()
{
    display_ = wl_display_connect(nullptr);
    if (!display_)
        throw std::runtime_error("cannot connect to the selected Wayland display");
    registry_ = wl_display_get_registry(display_);
    if (!registry_)
        throw std::runtime_error("cannot create Wayland registry");
    listen_check(wl_registry_add_listener(registry_, &registry_listener(), this));
    // The first roundtrip binds globals; the second receives names/scales/capabilities.
    for (int count = 0; count < 2; ++count)
    {
        if (wl_display_roundtrip(display_) < 0)
            throw std::runtime_error("Wayland registry roundtrip failed");
        rethrow_failure();
    }
}

void Host::Impl::create_surface()
{
    if (!compositor_ || !shell_)
        throw std::runtime_error("compositor does not provide wlr-layer-shell");
    if (!options_.output.empty())
    {
        for (const auto& output : outputs_)
            if (output->object && output->name == options_.output)
                selected_ = output.get();
        if (!selected_)
            throw std::runtime_error("output not found: " + options_.output +
                                     " (named outputs require wl_output version 4)");
    }
    update_scale();
    surface_ = wl_compositor_create_surface(compositor_);
    if (!surface_)
        throw std::runtime_error("cannot create Wayland surface");
    listen_check(wl_surface_add_listener(surface_, &surface_listener(), this));
    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
        shell_, surface_, selected_ ? selected_->object : nullptr,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "nekomote");
    if (!layer_surface_)
        throw std::runtime_error("cannot create layer surface");
    listen_check(zwlr_layer_surface_v1_add_listener(layer_surface_, &layer_listener(), this));
    zwlr_layer_surface_v1_set_anchor(
        layer_surface_, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(layer_surface_, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surface_, 0);
    if (options_.input == InputMode::passthrough)
    {
        auto* region = wl_compositor_create_region(compositor_);
        if (!region)
            throw std::runtime_error("cannot create empty input region");
        wl_surface_set_input_region(surface_, region);
        wl_region_destroy(region);
    }
    else if (pointer_)
        initialize_cursor();
    wl_surface_commit(surface_);
}

void Host::Impl::initialize_egl()
{
    egl_display_ = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, display_, nullptr);
    if (egl_display_ == EGL_NO_DISPLAY)
        throw std::runtime_error("cannot obtain Wayland EGL display");
    egl_check(eglInitialize(egl_display_, nullptr, nullptr), "initialize EGL");
    egl_check(eglBindAPI(EGL_OPENGL_API), "select desktop OpenGL");
    constexpr EGLint config_attributes[] = {EGL_SURFACE_TYPE,
                                            EGL_WINDOW_BIT,
                                            EGL_RENDERABLE_TYPE,
                                            EGL_OPENGL_BIT,
                                            EGL_RED_SIZE,
                                            8,
                                            EGL_GREEN_SIZE,
                                            8,
                                            EGL_BLUE_SIZE,
                                            8,
                                            EGL_ALPHA_SIZE,
                                            8,
                                            EGL_NONE};
    EGLConfig config = nullptr;
    EGLint count = 0;
    egl_check(eglChooseConfig(egl_display_, config_attributes, &config, 1, &count),
              "choose EGL config");
    if (count != 1)
        throw std::runtime_error("no desktop OpenGL EGL window configuration");
    constexpr EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION_KHR,
                                             3,
                                             EGL_CONTEXT_MINOR_VERSION_KHR,
                                             3,
                                             EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                                             EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
                                             EGL_NONE};
    egl_context_ = eglCreateContext(egl_display_, config, EGL_NO_CONTEXT, context_attributes);
    if (egl_context_ == EGL_NO_CONTEXT)
        throw std::runtime_error("cannot create desktop OpenGL 3.3 context");
    resize();
    egl_window_ = wl_egl_window_create(surface_, pixel_width_, pixel_height_);
    if (!egl_window_)
        throw std::runtime_error("cannot create Wayland EGL window");
    egl_surface_ = eglCreateWindowSurface(
        egl_display_, config, reinterpret_cast<EGLNativeWindowType>(egl_window_), nullptr);
    if (egl_surface_ == EGL_NO_SURFACE)
        throw std::runtime_error("cannot create EGL window surface");
    egl_check(eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_),
              "make EGL context current");
    // wl_surface.frame owns pacing. Avoid an additional swap-interval wait.
    egl_check(eglSwapInterval(egl_display_, 0), "set EGL swap interval");
    wallpaper_ = nekomote_wallpaper_create(load_gl);
}

#if defined(NEKOMOTE_HAS_VULKAN)
void Host::Impl::initialize_vulkan()
{
    const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME};
    if (vk_host_c_create_instance(&vk_host_, extensions, 2) != 0)
        throw std::runtime_error("cannot create Vulkan instance");
    VkWaylandSurfaceCreateInfoKHR surface_info{};
    surface_info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    surface_info.display = display_;
    surface_info.surface = surface_;
    const auto result =
        vkCreateWaylandSurfaceKHR(vk_host_.instance, &surface_info, nullptr, &vk_host_.surface);
    if (result != VK_SUCCESS)
        throw std::runtime_error("cannot create Vulkan Wayland surface: " + std::to_string(result));
    if (vk_host_c_select_device(&vk_host_, vk_host_.surface) != 0 ||
        vk_host_c_create_device(&vk_host_) != 0)
        throw std::runtime_error("cannot initialize Vulkan device for Wayland surface");
    const nekomote_vk_device_context context{vk_host_.instance,        vk_host_.physical_device,
                                             vk_host_.device,          vk_host_.graphics_queue,
                                             vk_host_.graphics_family, VK_FORMAT_R8G8B8A8_UNORM};
    wallpaper_ = nekomote_wallpaper_create_vk(context);
}
#endif

void Host::Impl::initialize_graphics()
{
    resize();
#if defined(NEKOMOTE_HAS_VULKAN)
    if (options_.backend == Backend::vulkan)
        initialize_vulkan();
    else
#endif
        initialize_egl();
    if (!wallpaper_)
        check(nekomote_last_error(), "create wallpaper");
    if (!wallpaper_)
        throw std::runtime_error("create wallpaper returned a null handle");
    const auto config_path =
        options_.config.empty() ? default_config_path(options_.output) : options_.config;
    check(nekomote_wallpaper_set_no_save(wallpaper_, options_.no_save), "set no-save");
    check(nekomote_wallpaper_set_config_path(wallpaper_, config_path.c_str()),
          "set configuration path");
    if (options_.frames)
        check(nekomote_wallpaper_set_seed(wallpaper_, 1), "set diagnostic random seed");
    check(nekomote_wallpaper_set_output_size(wallpaper_, pixel_width_, pixel_height_),
          "set output size");
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
    if (options_.ui_open)
        check(nekomote_wallpaper_set_ui_open(wallpaper_, 1), "open shared controls");
    std::cerr << "layer-shell " << (options_.backend == Backend::vulkan ? "Vulkan" : "OpenGL")
              << " background " << pixel_width_ << 'x' << pixel_height_ << ", scale " << scale_
              << ", local pointer "
              << (options_.input == InputMode::passthrough
                      ? "disabled (passthrough)"
                      : (pointer_ ? "available" : "unavailable (requires seat v5 with pointer)"))
              << "; global pointer and automatic occlusion pause unavailable\n";
}

void Host::Impl::resize()
{
    if (!needs_resize_)
        return;
    if (!logical_width_ || !logical_height_ ||
        logical_width_ > static_cast<uint32_t>(INT_MAX / scale_) ||
        logical_height_ > static_cast<uint32_t>(INT_MAX / scale_))
        throw std::runtime_error("output size overflows native dimensions");
    pixel_width_ = static_cast<int>(logical_width_) * scale_;
    pixel_height_ = static_cast<int>(logical_height_) * scale_;
    wl_surface_set_buffer_scale(surface_, scale_);
    if (egl_window_)
        wl_egl_window_resize(egl_window_, pixel_width_, pixel_height_, 0, 0);
    if (wallpaper_)
        check(nekomote_wallpaper_set_output_size(wallpaper_, pixel_width_, pixel_height_),
              "resize wallpaper");
    needs_resize_ = false;
}

void Host::Impl::process_input()
{
    for (size_t index = 0; index < input_count_; ++index)
    {
        const auto& input = inputs_[index];
        switch (input.kind)
        {
            case InputKind::enter:
                check(nekomote_wallpaper_pointer_enter(wallpaper_, input.x * scale_,
                                                       input.y * scale_),
                      "pointer enter");
                break;
            case InputKind::leave:
                check(nekomote_wallpaper_pointer_leave(wallpaper_), "pointer leave");
                break;
            case InputKind::motion:
                check(nekomote_wallpaper_pointer_move(wallpaper_, input.x * scale_,
                                                      input.y * scale_, input.time),
                      "pointer motion");
                break;
            case InputKind::button:
                check(nekomote_wallpaper_mouse_button(wallpaper_, input.button, input.action,
                                                      input.time),
                      "pointer button");
                break;
            case InputKind::scroll:
                check(nekomote_wallpaper_mouse_scroll(wallpaper_, input.x), "pointer scroll");
                break;
        }
    }
    input_count_ = 0;
}

void Host::Impl::capture()
{
    const auto row_bytes = static_cast<size_t>(pixel_width_) * 3;
    if (static_cast<size_t>(pixel_height_) > std::numeric_limits<size_t>::max() / row_bytes)
        throw std::runtime_error("capture dimensions overflow");
    std::vector<uint8_t> pixels(row_bytes * static_cast<size_t>(pixel_height_));
#if defined(NEKOMOTE_HAS_VULKAN)
    if (options_.backend == Backend::vulkan)
    {
        const auto width = static_cast<size_t>(pixel_width_);
        const auto height = static_cast<size_t>(pixel_height_);
        if (height > std::numeric_limits<size_t>::max() / width / 4)
            throw std::runtime_error("Vulkan capture dimensions overflow");
        std::vector<uint8_t> rgba(width * height * 4);
        if (vk_host_c_capture(&vk_host_, pixel_width_, pixel_height_, rgba.data()) != 0)
            throw std::runtime_error("diagnostic Vulkan framebuffer readback failed");
        // The host returns top-left straight RGBA. PPM has no alpha; composite
        // over black to match the RGB bytes returned by the GL framebuffer.
        for (size_t index = 0; index < width * height; ++index)
            for (size_t channel = 0; channel < 3; ++channel)
                pixels[index * 3 + channel] = static_cast<uint8_t>(
                    (static_cast<unsigned>(rgba[index * 4 + channel]) * rgba[index * 4 + 3] +
                     127U) /
                    255U);
    }
    else
#endif
    {
        using ReadPixels = void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
        using PixelStore = void (*)(GLenum, GLint);
        using GetError = GLenum (*)();
        const auto read_pixels = reinterpret_cast<ReadPixels>(eglGetProcAddress("glReadPixels"));
        const auto pixel_store = reinterpret_cast<PixelStore>(eglGetProcAddress("glPixelStorei"));
        const auto get_error = reinterpret_cast<GetError>(eglGetProcAddress("glGetError"));
        if (!read_pixels || !pixel_store || !get_error)
            throw std::runtime_error("capture GL functions unavailable");
        pixel_store(GL_PACK_ALIGNMENT, 1);
        read_pixels(0, 0, pixel_width_, pixel_height_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        if (get_error() != GL_NO_ERROR)
            throw std::runtime_error("diagnostic framebuffer readback failed");
        // Normalize GL's bottom-left rows to the same top-left output contract.
        for (int row = 0; row < pixel_height_ / 2; ++row)
            std::swap_ranges(pixels.data() + static_cast<size_t>(row) * row_bytes,
                             pixels.data() + static_cast<size_t>(row + 1) * row_bytes,
                             pixels.data() +
                                 static_cast<size_t>(pixel_height_ - 1 - row) * row_bytes);
    }
    const int descriptor =
        open(options_.capture.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (descriptor < 0)
        throw std::runtime_error("cannot open diagnostic capture file");
    const auto close_stream = [](FILE* file) { std::fclose(file); };
    std::unique_ptr<FILE, decltype(close_stream)> stream(fdopen(descriptor, "wb"), close_stream);
    if (!stream)
    {
        close(descriptor);
        throw std::runtime_error("cannot create diagnostic capture stream");
    }
    if (std::fprintf(stream.get(), "P6\n%d %d\n255\n", pixel_width_, pixel_height_) < 0)
        throw std::runtime_error("cannot write diagnostic capture header");
    for (int row = 0; row < pixel_height_; ++row)
        if (std::fwrite(pixels.data() + static_cast<size_t>(row) * row_bytes, 1, row_bytes,
                        stream.get()) != row_bytes)
            throw std::runtime_error("cannot write diagnostic capture pixels");
    if (std::fclose(stream.release()) != 0)
        throw std::runtime_error("cannot write diagnostic capture file");
}

void Host::Impl::render()
{
#if defined(NEKOMOTE_HAS_VULKAN)
    if (options_.backend == Backend::vulkan && vk_host_c_wait_ready(&vk_host_) != 0)
        throw std::runtime_error("wait for Vulkan frame failed");
#endif
    const auto now = Clock::now();
    // Pass the same bounded delta to model/background advancement and UI.
    // This does not change application policies that use their own clock.
    constexpr double kResumeBudgetSeconds = 0.25;
    const double dt =
        retry_frame_ ? 0.0
        : options_.frames
            ? 1.0 / 60.0
            : (rendered_frames_ ? std::min(std::chrono::duration<double>(now - last_frame_).count(),
                                           kResumeBudgetSeconds)
                                : 0.0);
    if (!retry_frame_)
        check(nekomote_wallpaper_advance(wallpaper_, dt), "advance wallpaper");
#if defined(NEKOMOTE_HAS_VULKAN)
    if (options_.backend == Backend::vulkan)
    {
        nekomote_vk_target target{};
        if (vk_host_c_begin_frame(&vk_host_, pixel_width_, pixel_height_, 1, &target) != 0)
            throw std::runtime_error("begin Vulkan frame failed");
        check(nekomote_wallpaper_set_vk_target(wallpaper_, target), "set Vulkan target");
    }
#endif
    check(nekomote_wallpaper_render_frame(wallpaper_), "render wallpaper");
    check(nekomote_wallpaper_render_ui(wallpaper_, static_cast<float>(dt)), "render controls");
    const bool capture_frame = !options_.capture.empty() && rendered_frames_ + 1 == options_.frames;
    if (capture_frame && options_.backend == Backend::opengl)
        capture();
    frame_callback_ = wl_surface_frame(surface_);
    if (!frame_callback_)
        throw std::runtime_error("cannot allocate Wayland frame callback");
    listen_check(wl_callback_add_listener(frame_callback_, &frame_listener(), this));
    frame_ready_ = false;
#if defined(NEKOMOTE_HAS_VULKAN)
    if (options_.backend == Backend::vulkan)
    {
        const int result = vk_host_c_present(&vk_host_);
        if (result < 0)
            throw std::runtime_error("present Vulkan wallpaper failed");
        if (result > 0)
        {
            // OUT_OF_DATE did not commit a buffer, so no frame callback is
            // guaranteed. Retry the same application state on the new chain.
            wl_callback_destroy(frame_callback_);
            frame_callback_ = nullptr;
            frame_ready_ = true;
            retry_frame_ = true;
            last_frame_ = now;
            next_frame_ = now;
            return;
        }
    }
    else
#endif
        egl_check(eglSwapBuffers(egl_display_, egl_surface_), "present wallpaper");
#if defined(NEKOMOTE_HAS_VULKAN)
    // Capture only an accepted present, avoiding a stale file on WSI retry.
    if (capture_frame && options_.backend == Backend::vulkan)
        capture();
#endif
    retry_frame_ = false;
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

void Host::Impl::wait_events(int timeout)
{
    if (wl_display_prepare_read(display_) != 0)
    {
        if (wl_display_dispatch_pending(display_) < 0)
            throw std::runtime_error("dispatch Wayland events failed");
        rethrow_failure();
        // Dispatch may have made a frame ready or changed output geometry.
        // Return to scheduling instead of polling with the previous deadline.
        return;
    }
    bool needs_write = false;
    if (wl_display_flush(display_) < 0)
    {
        if (errno != EAGAIN)
        {
            wl_display_cancel_read(display_);
            throw std::runtime_error("flush Wayland connection failed");
        }
        needs_write = true;
    }
    pollfd descriptor{wl_display_get_fd(display_),
                      static_cast<short>(POLLIN | (needs_write ? POLLOUT : 0)), 0};
    const int result = poll(&descriptor, 1, timeout);
    if (result > 0 && (descriptor.revents & POLLIN))
    {
        if (wl_display_read_events(display_) < 0)
            throw std::runtime_error("read Wayland connection failed");
    }
    else
        wl_display_cancel_read(display_);
    if (result < 0 && errno != EINTR)
        throw std::runtime_error("poll Wayland connection failed");
    if (result > 0 && (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)))
        throw std::runtime_error("Wayland compositor disconnected");
    // POLLOUT is retried by the next prepare_read/flush iteration. Pending
    // dispatch happens at the loop head, after the prepared read is resolved.
}

void Host::Impl::run()
{
    initialize_wayland();
    if (options_.list_outputs)
    {
        for (const auto& output : outputs_)
            if (!output->name.empty())
                std::cout << output->name << '\n';
        return;
    }
    create_surface();
    const auto started = Clock::now();
    while (!closed_ && !stop_requested)
    {
        if (wl_display_dispatch_pending(display_) < 0)
            throw std::runtime_error("dispatch Wayland events failed");
        rethrow_failure();
        if (closed_ || stop_requested)
            break;
        if (configured_)
        {
            if (!wallpaper_)
                initialize_graphics();
#if defined(NEKOMOTE_HAS_VULKAN)
            // Input and resize can replace GPU resources too, so synchronize
            // before any wallpaper mutation, not just command recording.
            if (options_.backend == Backend::vulkan && vk_host_c_wait_ready(&vk_host_) != 0)
                throw std::runtime_error("wait for Vulkan frame before input/resize failed");
#endif
            resize();
            process_input();
            if (frame_ready_ && Clock::now() >= next_frame_)
                render();
        }
        if (closed_ || stop_requested)
            break;
        if (options_.frames && Clock::now() - started >= std::chrono::seconds(30))
            throw std::runtime_error("diagnostic frame deadline exceeded");
        int timeout = 1000;
        if (configured_ && frame_ready_)
        {
            const auto remaining =
                std::chrono::ceil<std::chrono::milliseconds>(next_frame_ - Clock::now()).count();
            timeout = static_cast<int>(std::clamp<int64_t>(remaining, 0, 1000));
        }
        wait_events(timeout);
    }
    rethrow_failure();
}

Host::Host(Options options) : impl_(std::make_unique<Impl>(std::move(options)))
{
}
Host::~Host() = default;
void Host::run()
{
    impl_->run();
}
} // namespace nekomote::layer_shell
