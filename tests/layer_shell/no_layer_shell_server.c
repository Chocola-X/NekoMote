/* Synthetic Wayland server intentionally advertising no layer-shell protocol.
 * This is a negative fixture, not a compositor or a rendering implementation. */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

static void reject_surface(struct wl_client* client, struct wl_resource* resource, uint32_t id)
{
    (void)client;
    (void)id;
    wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_METHOD,
                           "negative fixture does not create surfaces or regions");
}

static void bind_compositor(struct wl_client* client, void* data, uint32_t version, uint32_t id)
{
    (void)data;
    static const struct wl_compositor_interface implementation = {reject_surface, reject_surface};
    struct wl_resource* resource =
        wl_resource_create(client, &wl_compositor_interface, (int)version, id);
    if (!resource)
    {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &implementation, NULL, NULL);
}

static int terminate_server(int signal_number, void* data)
{
    (void)signal_number;
    wl_display_terminate(data);
    return 0;
}

int main(int argc, char** argv)
{
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    struct stat status;
    if (argc != 3 || !argv[2][0] || strchr(argv[2], '/') || !runtime || runtime[0] != '/' ||
        strcmp(runtime, argv[1]) != 0 || !strrchr(runtime, '/') ||
        strcmp(strrchr(runtime, '/'), "/runtime") != 0 || stat(runtime, &status) != 0 ||
        !S_ISDIR(status.st_mode) || status.st_uid != getuid() || (status.st_mode & 0777) != 0700)
    {
        fprintf(stderr,
                "usage: XDG_RUNTIME_DIR=<private mode-0700-dir> %s <runtime-dir> "
                "<socket-name>\n",
                argv[0]);
        return 2;
    }
    struct wl_display* display = wl_display_create();
    if (!display)
        return 1;
    if (wl_display_add_socket(display, argv[2]) != 0)
    {
        fprintf(stderr, "cannot create private socket: %s\n", strerror(errno));
        wl_display_destroy(display);
        return 1;
    }
    // Advertise a valid compositor so the host reaches the layer-shell-specific
    // capability rejection; no surface request should precede that check.
    if (!wl_global_create(display, &wl_compositor_interface, 4, NULL, bind_compositor))
    {
        wl_display_destroy(display);
        return 1;
    }
    struct wl_event_loop* loop = wl_display_get_event_loop(display);
    struct wl_event_source* interrupt =
        wl_event_loop_add_signal(loop, SIGINT, terminate_server, display);
    struct wl_event_source* terminate =
        wl_event_loop_add_signal(loop, SIGTERM, terminate_server, display);
    if (!interrupt || !terminate)
    {
        if (interrupt)
            wl_event_source_remove(interrupt);
        if (terminate)
            wl_event_source_remove(terminate);
        wl_display_destroy(display);
        return 1;
    }
    puts("ready");
    fflush(stdout);
    wl_display_run(display);
    wl_event_source_remove(interrupt);
    wl_event_source_remove(terminate);
    wl_display_destroy_clients(display);
    wl_display_destroy(display);
    return 0;
}
