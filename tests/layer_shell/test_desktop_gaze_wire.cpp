#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <sys/socket.h>
#include <wayland-client.h>

#include <cstring>
#include <map>

#include "desktop_gaze.hpp"
#include "gaze_compositor.hpp"

namespace
{
struct Client
{
    nekomote::layer_shell::DesktopGaze gaze;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    struct Output
    {
        wl_output* proxy;
        int scale;
    };
    std::map<uint32_t, Output> outputs;
};

const wl_output_listener output_listener{
    [](void* data, wl_output* output, int, int, int, int, int, const char*, const char*,
       int transform) { static_cast<Client*>(data)->gaze.set_output_transform(output, transform); },
    [](void* data, wl_output* output, uint32_t flags, int w, int h, int)
    {
        if (flags & WL_OUTPUT_MODE_CURRENT)
            static_cast<Client*>(data)->gaze.set_output_mode(output, w, h);
    },
    [](void* data, wl_output* output) { static_cast<Client*>(data)->gaze.output_done(output); },
    [](void* data, wl_output* output, int scale)
    {
        for (auto& entry : static_cast<Client*>(data)->outputs)
            if (entry.second.proxy == output)
                entry.second.scale = scale;
    },
    nullptr,
    nullptr};
const wl_seat_listener seat_listener{[](void* data, wl_seat* seat, uint32_t caps)
                                     {
                                         auto& c = *static_cast<Client*>(data);
                                         if (caps & WL_SEAT_CAPABILITY_POINTER)
                                             c.pointer = wl_seat_get_pointer(seat);
                                     },
                                     [](void*, wl_seat*, const char*) {}};
const wl_registry_listener registry_listener{
    [](void* data, wl_registry* registry, uint32_t id, const char* type, uint32_t version)
    {
        auto& c = *static_cast<Client*>(data);
        c.gaze.global(registry, id, type, version);
        if (std::strcmp(type, wl_output_interface.name) == 0)
        {
            auto* o =
                static_cast<wl_output*>(wl_registry_bind(registry, id, &wl_output_interface, 3));
            c.outputs.emplace(id, Client::Output{o, 0});
            c.gaze.add_output(id, o);
            wl_output_add_listener(o, &output_listener, data);
        }
        else if (std::strcmp(type, wl_seat_interface.name) == 0)
        {
            c.seat = static_cast<wl_seat*>(wl_registry_bind(registry, id, &wl_seat_interface, 5));
            wl_seat_add_listener(c.seat, &seat_listener, data);
        }
    },
    [](void* data, wl_registry*, uint32_t id)
    {
        auto& c = *static_cast<Client*>(data);
        c.gaze.remove_global(id);
        if (auto it = c.outputs.find(id); it != c.outputs.end())
        {
            wl_output_release(it->second.proxy);
            c.outputs.erase(it);
        }
    }};

// Like wl_display_roundtrip, but service the other end explicitly on the same
// thread. The sync callback fences every request queued before this step.
void step(wl_display* display, GazeCompositor& server)
{
    static const wl_callback_listener listener{[](void* data, wl_callback* cb, uint32_t)
                                               {
                                                   *static_cast<bool*>(data) = true;
                                                   wl_callback_destroy(cb);
                                               }};
    bool done = false;
    auto* callback = wl_display_sync(display);
    REQUIRE(callback);
    REQUIRE(wl_callback_add_listener(callback, &listener, &done) == 0);
    REQUIRE(wl_display_flush(display) >= 0);
    server.dispatch();
    while (!done)
        REQUIRE(wl_display_dispatch(display) >= 0);
}
} // namespace

TEST_CASE("Desktop gaze consumes cursor metadata over a private Wayland connection")
{
    int fd[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fd) == 0);
    GazeCompositor server(fd[1]);
    std::unique_ptr<wl_display, decltype(&wl_display_disconnect)> display(
        wl_display_connect_to_fd(fd[0]), wl_display_disconnect);
    REQUIRE(display);
    Client c;
    auto* registry = wl_display_get_registry(display.get());
    REQUIRE(wl_registry_add_listener(registry, &registry_listener, &c) == 0);
    auto sync = [&] { step(display.get(), server); };
    sync(); // globals
    sync(); // bind outputs, seat and managers
    sync(); // Seat exists, but session state is initially unknown (same as passthrough).
    REQUIRE(c.pointer);
    REQUIRE_FALSE(c.gaze.available());
    REQUIRE_FALSE(server.left.cursor);
    REQUIRE_FALSE(server.right.cursor);
    c.gaze.set_pointer(c.pointer); // First confirmed unlock.
    sync();
    REQUIRE(c.gaze.available());
    REQUIRE(c.outputs.size() == 2);
    auto left = c.outputs.begin()->first;
    auto right = c.outputs.rbegin()->first;
    auto no_sample = [&]
    {
        for (const auto& [id, out] : c.outputs)
        {
            CAPTURE(id);
            REQUIRE(out.scale == 2);
            REQUIRE_FALSE(c.gaze.sample(out.proxy, out.scale));
        }
    };
    auto expect = [&](uint32_t id, double x, double y)
    {
        const auto& out = c.outputs.at(id);
        auto sample = c.gaze.sample(out.proxy, out.scale);
        CAPTURE(id, x, y);
        REQUIRE(sample);
        REQUIRE(sample->first == Catch::Approx(x));
        REQUIRE(sample->second == Catch::Approx(y));
    };
    REQUIRE(server.left.cursor);
    REQUIRE(server.right.cursor);
    no_sample();
    server.left.enter();
    sync();
    no_sample(); // enter alone is not a position
    server.left.position(1599, 160);
    sync();
    expect(left, 1998.75, 200);
    expect(right, -1.25, 0);
    server.right.enter();
    server.right.position(18, 9);
    server.left.position(1600, 160); // newer but outside the left output
    sync();
    expect(left, 2020, 210);
    expect(right, 20, 10);
    server.left.leave();
    server.right.leave();
    sync();
    no_sample();

    // Unplug the active source, then re-advertise it with a new registry id.
    server.right.enter();
    server.right.position(900, 540);
    sync();
    expect(left, 3000, 800);
    server.unplug(server.right);
    sync();
    sync(); // process the client's destructor requests
    REQUIRE_FALSE(server.right.cursor);
    REQUIRE(server.left.cursor);
    REQUIRE(c.outputs.size() == 1);
    no_sample();
    server.advertise(server.right);
    sync();
    sync();
    REQUIRE(c.outputs.size() == 2);
    REQUIRE_FALSE(c.outputs.contains(right));
    right = c.outputs.rbegin()->first;
    REQUIRE(server.right.cursor);
    no_sample();
    server.right.enter();
    server.right.position(180, 90);
    sync();
    expect(left, 2200, 300);

    // Mirror host permission revoke/resume. A position already in flight on
    // the old session must not survive its destruction or seed the next one.
    server.right.position(900, 540);
    c.gaze.set_pointer(nullptr);
    REQUIRE_FALSE(c.gaze.available());
    no_sample();
    sync();
    REQUIRE_FALSE(server.left.cursor);
    REQUIRE_FALSE(server.right.cursor);
    c.gaze.set_pointer(c.pointer);
    sync();
    REQUIRE(c.gaze.available());
    REQUIRE(server.left.cursor);
    REQUIRE(server.right.cursor);
    no_sample();
    server.right.enter();
    sync();
    no_sample();
    server.right.position(36, 18);
    sync();
    expect(left, 2040, 220);
    expect(right, 40, 20);

    c.gaze.reset();
    wl_pointer_release(c.pointer);
    wl_seat_release(c.seat);
    for (const auto& entry : c.outputs)
        wl_output_release(entry.second.proxy);
    wl_registry_destroy(registry);
    sync();
    REQUIRE_FALSE(server.left.cursor);
    REQUIRE_FALSE(server.right.cursor);
}
