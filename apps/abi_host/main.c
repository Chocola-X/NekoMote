#include <nekomote/abi.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    nekomote_program *program;
    nekomote_session *session;
    nekomote_renderer *renderer;
    int width;
    int height;
    int mirror;
    float origin_x;
    float origin_y;
    float scale;
    int paused;
} AppState;

static AppState g_state;

static void on_resize(GLFWwindow *win, int w, int h)
{
    (void)win;
    g_state.width = w;
    g_state.height = h;
    nekomote_renderer_resize_output(g_state.renderer, w, h);
    nekomote_session_set_output_size(g_state.session, w, h);
}

static void on_mouse_move(GLFWwindow *win, double x, double y)
{
    (void)win;
    if (!g_state.session)
        return;
    float px = (float)x;
    float py = (float)y;
    if (g_state.mirror)
        px = (float)g_state.width - px;
    nekomote_session_pointer_move(g_state.session, px, py);
}

static void on_mouse_button(GLFWwindow *win, int button, int action, int mods)
{
    (void)mods;
    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS)
        return;
    double x, y;
    glfwGetCursorPos(win, &x, &y);
    float px = (float)x;
    float py = (float)y;
    if (g_state.mirror)
        px = (float)g_state.width - px;
    nekomote_session_touch(g_state.session, px, py);
}

static void on_key(GLFWwindow *win, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;
    if (action != GLFW_PRESS)
        return;
    switch (key)
    {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(win, GLFW_TRUE);
        break;
    case GLFW_KEY_SPACE:
        g_state.paused = !g_state.paused;
        nekomote_session_set_playback_state(
            g_state.session,
            g_state.paused ? NEKOMOTE_PLAYBACK_PAUSED : NEKOMOTE_PLAYBACK_RUNNING);
        break;
    case GLFW_KEY_R:
        nekomote_session_reset(g_state.session);
        break;
    case GLFW_KEY_F:
        g_state.scale = 1.0f;
        g_state.origin_x = 0.0f;
        g_state.origin_y = 0.0f;
        nekomote_session_set_scale(g_state.session, g_state.scale);
        nekomote_session_set_origin(g_state.session, g_state.origin_x, g_state.origin_y);
        break;
    }
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <model.nkm.json> <session.nks.json> [options]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --width <N>     Window width (default 800)\n");
    fprintf(stderr, "  --height <N>    Window height (default 600)\n");
    fprintf(stderr, "  --fps <N>       Target FPS (default 60)\n");
    fprintf(stderr, "  --scale <F>     Initial scale (default 1.0)\n");
    fprintf(stderr, "  --origin-x <F>  Origin X offset (default 0)\n");
    fprintf(stderr, "  --origin-y <F>  Origin Y offset (default 0)\n");
    fprintf(stderr, "Keys: ESC=quit, Space=pause, R=reset, F=fit\n");
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        print_usage(argv[0]);
        return 2;
    }

    const char *model_path = argv[1];
    const char *session_path = argv[2];
    int width = 800;
    int height = 600;
    int fps = 60;
    g_state.scale = 1.0f;
    g_state.origin_x = 0.0f;
    g_state.origin_y = 0.0f;

    for (int i = 3; i < argc; i++)
    {
        if (strcmp(argv[i], "--width") == 0 && i + 1 < argc)
            width = atoi(argv[++i]);
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)
            height = atoi(argv[++i]);
        else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc)
            fps = atoi(argv[++i]);
        else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
            g_state.scale = (float)atof(argv[++i]);
        else if (strcmp(argv[i], "--origin-x") == 0 && i + 1 < argc)
            g_state.origin_x = (float)atof(argv[++i]);
        else if (strcmp(argv[i], "--origin-y") == 0 && i + 1 < argc)
            g_state.origin_y = (float)atof(argv[++i]);
    }

    printf("nekomote_abi_host — C ABI verification host\n");
    fflush(stdout);
    printf("  model:   %s\n", model_path);
    printf("  session: %s\n", session_path);
    printf("  window:  %dx%d @ %d fps\n", width, height, fps);
    printf("  version: %s\n\n", nekomote_version());

    /* Load assets through C ABI */
    nekomote_model *model = nekomote_model_load(model_path);
    if (!model)
    {
        fprintf(stderr, "Error: failed to load model\n");
        return 1;
    }
    nekomote_session_asset *session_asset = nekomote_session_asset_load(session_path);
    if (!session_asset)
    {
        fprintf(stderr, "Error: failed to load session asset\n");
        nekomote_model_destroy(model);
        return 1;
    }

    /* Compile program */
    nekomote_program *program = nekomote_program_compile(model, session_asset);
    if (!program)
    {
        fprintf(stderr, "Error: failed to compile program\n");
        nekomote_session_asset_destroy(session_asset);
        nekomote_model_destroy(model);
        return 1;
    }

    g_state.program = program;
    g_state.mirror = nekomote_program_mirror(program);
    printf("  mirror:  %s\n", g_state.mirror ? "yes" : "no");
    printf("  gaze:    %s\n", nekomote_program_gaze_available(program) ? "yes" : "no");
    printf("  touch:   %s\n", nekomote_program_touch_available(program) ? "yes" : "no");
    printf("  textures: %zu\n", nekomote_program_texture_count(program));

    /* Create session */
    nekomote_session_config config = {0};
    config.random_seed = 42;
    config.pointer = NEKOMOTE_POINTER_LOCAL_SURFACE;
    config.audio = NEKOMOTE_AUDIO_DISABLED;
    nekomote_session *session = nekomote_session_create(program, config);
    if (!session)
    {
        fprintf(stderr, "Error: failed to create session\n");
        goto cleanup_program;
    }
    g_state.session = session;
    nekomote_session_set_origin(session, g_state.origin_x, g_state.origin_y);
    nekomote_session_set_scale(session, g_state.scale);
    nekomote_session_start(session);

    /* Init GLFW */
    if (!glfwInit())
    {
        fprintf(stderr, "Error: glfwInit failed\n");
        goto cleanup_session;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    GLFWwindow *window = glfwCreateWindow(width, height, "NekoMote ABI Host", NULL, NULL);
    if (!window)
    {
        fprintf(stderr, "Error: glfwCreateWindow failed\n");
        glfwTerminate();
        goto cleanup_session;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGL(glfwGetProcAddress))
    {
        fprintf(stderr, "Error: gladLoadGL failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        goto cleanup_session;
    }
    printf("  GL:      %s\n", glGetString(GL_VERSION));

    /* Create renderer through C ABI */
    nekomote_renderer *renderer = nekomote_renderer_create(glfwGetProcAddress);
    if (!renderer)
    {
        fprintf(stderr, "Error: failed to create renderer\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        goto cleanup_session;
    }
    g_state.renderer = renderer;

    /* Reserve capacity and upload textures */
    {
        size_t tex_count = nekomote_program_texture_count(program);
        nekomote_renderer_capacity cap = {0};
        cap.vertices = 65536;
        cap.indices = 131072;
        cap.commands = 1024;
        cap.textures = tex_count;
        cap.mask_commands = 256;
        cap.masks = 64;
        nekomote_renderer_reserve(renderer, cap);

        char dir_buf[1024];
        const char *last_slash = strrchr(model_path, '/');
        if (last_slash)
        {
            size_t dir_len = (size_t)(last_slash - model_path);
            if (dir_len >= sizeof(dir_buf))
                dir_len = sizeof(dir_buf) - 1;
            memcpy(dir_buf, model_path, dir_len);
            dir_buf[dir_len] = '\0';
        }
        else
        {
            dir_buf[0] = '.';
            dir_buf[1] = '\0';
        }

        int uploaded = 0;
        for (size_t i = 0; i < tex_count; i++)
        {
            nekomote_texture_info tex = nekomote_program_texture(program, i);
            if (!tex.path || tex.id == 0)
                continue;
            char full_path[2048];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_buf, tex.path);
            nekomote_image *img = nekomote_image_load_png(full_path);
            if (!img)
            {
                fprintf(stderr, "  Warning: failed to load texture %zu: %s\n", i, full_path);
                continue;
            }
            nekomote_renderer_upload_texture(
                renderer, tex.id,
                nekomote_image_width(img), nekomote_image_height(img),
                nekomote_image_pixels(img));
            nekomote_image_destroy(img);
            uploaded++;
        }
        printf("  uploaded: %d/%zu textures\n", uploaded, tex_count);
    }

    /* Set up callbacks and state */
    g_state.width = width;
    g_state.height = height;
    g_state.paused = 0;
    nekomote_renderer_resize_output(renderer, width, height);
    nekomote_session_set_output_size(session, width, height);
    glfwSetFramebufferSizeCallback(window, on_resize);
    glfwSetCursorPosCallback(window, on_mouse_move);
    glfwSetMouseButtonCallback(window, on_mouse_button);
    glfwSetKeyCallback(window, on_key);

    printf("\nRunning. ESC=quit, Space=pause, R=reset, F=fit\n\n");

    /* Main loop */
    double last_time = glfwGetTime();
    long frame_count = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        double now = glfwGetTime();
        double dt = now - last_time;
        last_time = now;

        if (dt > 0.25)
            dt = 0.25;

        nekomote_session_advance(session, dt);
        nekomote_session_prepare_frame(session);
        nekomote_render_frame frame = nekomote_session_frame(session);

        nekomote_renderer_prepare(renderer, frame);
        nekomote_renderer_resize_output(renderer, g_state.width, g_state.height);

        nekomote_render_target target = {0};
        target.width = g_state.width;
        target.height = g_state.height;

        nekomote_render_view view = {0};
        view.origin_x = g_state.origin_x;
        view.origin_y = g_state.origin_y;
        view.scale = g_state.scale;
        view.clear[0] = 0.05f;
        view.clear[1] = 0.05f;
        view.clear[2] = 0.08f;
        view.clear[3] = 1.0f;
        view.alpha = 1.0f;
        view.flip_x = g_state.mirror;

        nekomote_renderer_render(renderer, target, view, NEKOMOTE_TARGET_CLEAR);

        glfwSwapBuffers(window);
        frame_count++;

        if (frame_count % 300 == 0)
            printf("  frame %ld  dt=%.3fms  verts=%zu  cmds=%zu\n",
                   frame_count, dt * 1000.0,
                   frame.vertex_count, frame.command_count);
    }

    printf("\nRendered %ld frames. Shutting down.\n", frame_count);

    glfwDestroyWindow(window);
    glfwTerminate();
    nekomote_renderer_destroy(renderer);
cleanup_session:
    nekomote_session_destroy(session);
cleanup_program:
    nekomote_program_destroy(program);
    nekomote_session_asset_destroy(session_asset);
    nekomote_model_destroy(model);
    return 0;
}
