#include "gaze_compositor.hpp"

#include <wayland-server.h>

#include <cstdio>
#include <cstdlib>
#include <utility>

#include "ext-image-capture-source-v1-server-protocol.h"
#include "ext-image-copy-capture-v1-server-protocol.h"
#include "xdg-output-unstable-v1-server-protocol.h"

namespace
{
using Output = GazeCompositor::Output;
void require(bool ok, const char* message)
{
    if (!ok)
    {
        std::fprintf(stderr, "gaze compositor: %s\n", message);
        std::abort();
    }
}
wl_resource* resource(wl_client* client, uint32_t id, const wl_interface& type, int version,
                      const void* implementation, void* data = nullptr,
                      wl_resource_destroy_func_t cleanup = nullptr)
{
    auto* r = wl_resource_create(client, &type, version, id);
    require(r, type.name);
    wl_resource_set_implementation(r, implementation, data, cleanup);
    return r;
}
void destroy(wl_client*, wl_resource* r)
{
    wl_resource_destroy(r);
}

const struct wl_output_interface output_impl{destroy};
void bind_output(wl_client* c, void* data, uint32_t version, uint32_t id)
{
    auto& out = *static_cast<Output*>(data);
    auto* r = resource(c, id, wl_output_interface, static_cast<int>(version), &output_impl, data);
    wl_output_send_geometry(r, 0, 0, 300, 170, WL_OUTPUT_SUBPIXEL_UNKNOWN, "mock", "output",
                            WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(r, WL_OUTPUT_MODE_CURRENT, out.width, out.height, 60000);
    wl_output_send_scale(r, 2);
    wl_output_send_done(r);
}
const struct wl_pointer_interface pointer_impl{nullptr, destroy};
const struct wl_seat_interface seat_impl{
    [](wl_client* c, wl_resource* r, uint32_t id)
    { resource(c, id, wl_pointer_interface, wl_resource_get_version(r), &pointer_impl); }, nullptr,
    nullptr, destroy};
void bind_seat(wl_client* c, void*, uint32_t version, uint32_t id)
{
    auto* r = resource(c, id, wl_seat_interface, static_cast<int>(version), &seat_impl);
    wl_seat_send_capabilities(r, WL_SEAT_CAPABILITY_POINTER);
    wl_seat_send_name(r, "mock");
}
const struct zxdg_output_v1_interface layout_impl{destroy};
const struct zxdg_output_manager_v1_interface layout_manager{
    destroy, [](wl_client* c, wl_resource* manager, uint32_t id, wl_resource* output)
    {
        auto& out = *static_cast<Output*>(wl_resource_get_user_data(output));
        auto* r = resource(c, id, zxdg_output_v1_interface, wl_resource_get_version(manager),
                           &layout_impl);
        zxdg_output_v1_send_logical_position(r, out.x, out.y);
        zxdg_output_v1_send_logical_size(r, 1000, 600);
        wl_output_send_done(output); // xdg-output v3 transaction boundary
    }};
const struct ext_image_capture_source_v1_interface source_impl{destroy};
const struct ext_output_image_capture_source_manager_v1_interface source_manager{
    [](wl_client* c, wl_resource*, uint32_t id, wl_resource* output)
    {
        resource(c, id, ext_image_capture_source_v1_interface, 1, &source_impl,
                 wl_resource_get_user_data(output));
    },
    destroy};
// Reject both routes to image frames at the request that would enable them.
const struct ext_image_copy_capture_cursor_session_v1_interface cursor_impl{
    destroy,
    [](wl_client*, wl_resource*, uint32_t) { require(false, "requested cursor image capture"); }};
const struct ext_image_copy_capture_manager_v1_interface copy_manager{
    [](wl_client*, wl_resource*, uint32_t, wl_resource*, uint32_t)
    { require(false, "requested output image capture"); },
    [](wl_client* c, wl_resource*, uint32_t id, wl_resource* source, wl_resource*)
    {
        auto* out = static_cast<Output*>(wl_resource_get_user_data(source));
        require(!out->cursor, "duplicate cursor session");
        out->cursor =
            resource(c, id, ext_image_copy_capture_cursor_session_v1_interface, 1, &cursor_impl,
                     out, [](wl_resource* r)
                     { static_cast<Output*>(wl_resource_get_user_data(r))->cursor = nullptr; });
    },
    destroy};

template <const wl_interface& Type, const auto& Impl>
void bind_manager(wl_client* c, void*, uint32_t version, uint32_t id)
{
    resource(c, id, Type, static_cast<int>(version), &Impl);
}
} // namespace

GazeCompositor::GazeCompositor(int fd) : display(wl_display_create())
{
    require(display, "display allocation failed");
    advertise(left);
    advertise(right);
    auto global = [&](const wl_interface& type, int version, wl_global_bind_func_t bind)
    { require(wl_global_create(display, &type, version, nullptr, bind), type.name); };
    global(wl_seat_interface, 5, bind_seat);
    global(zxdg_output_manager_v1_interface, 3,
           bind_manager<zxdg_output_manager_v1_interface, layout_manager>);
    global(ext_output_image_capture_source_manager_v1_interface, 1,
           bind_manager<ext_output_image_capture_source_manager_v1_interface, source_manager>);
    global(ext_image_copy_capture_manager_v1_interface, 1,
           bind_manager<ext_image_copy_capture_manager_v1_interface, copy_manager>);
    require(wl_client_create(display, fd), "client allocation failed");
}
GazeCompositor::~GazeCompositor()
{
    wl_display_destroy_clients(display);
    wl_display_destroy(display);
}
void GazeCompositor::dispatch()
{
    require(wl_event_loop_dispatch(wl_display_get_event_loop(display), 0) >= 0, "dispatch failed");
    wl_display_flush_clients(display);
}
void GazeCompositor::advertise(Output& out)
{
    out.global = wl_global_create(display, &wl_output_interface, 3, &out, bind_output);
    require(out.global, "output allocation failed");
}
void GazeCompositor::unplug(Output& out)
{
    wl_global_destroy(std::exchange(out.global, nullptr));
}
void GazeCompositor::Output::enter() const
{
    ext_image_copy_capture_cursor_session_v1_send_enter(cursor);
}
void GazeCompositor::Output::position(int px, int py) const
{
    ext_image_copy_capture_cursor_session_v1_send_position(cursor, px, py);
}
void GazeCompositor::Output::leave() const
{
    ext_image_copy_capture_cursor_session_v1_send_leave(cursor);
}
