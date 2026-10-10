/*
 * nekomote_abi_host — C ABI wallpaper verification tool (muted).
 *
 * Uses the high-level nekomote_wallpaper ABI to achieve player-equivalent
 * wallpaper functionality: asset scanning, character switching, background,
 * control panel UI, and crossfade — all through the C ABI.
 *
 * Audio is always disabled; use nekomote_player for full playback.
 *
 * Two run modes:
 *   Fixed-step (--steps N, N>0): dt=1/60s, N steps then exit. Screenshot
 *     is read back before swap for stable pixels.
 *   Interactive (--steps 0, default): wall-clock dt, runs until Esc/close.
 *
 * Usage:
 *   nekomote_abi_host --assets <dir> [options]
 *   nekomote_abi_host --model <m.nkm.json> --session <s.nks.json> [options]
 *
 * Options:
 *   --width <N>       Window width (default 800, 1..8192)
 *   --height <N>      Window height (default 600, 1..8192)
 *   --steps <N>       Fixed-step mode: N steps at dt=1/60s, then exit
 *   --seed <N>        Random seed (default 42)
 *   --no-save         Do not save config
 *   --capture <path>  Save final frame as PNG (requires --steps)
 *   --ui-open         Start with UI panel open
 */

// clang-format off: glad must precede GLFW
#include <glad/gl.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <nekomote/abi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define FIXED_DT (1.0 / 60.0)
#define MAX_WINDOW_SIZE 8192
#define MAX_STEPS 100000

/* ------------------------------------------------------------------ */
/* Error checking                                                     */
/* ------------------------------------------------------------------ */

static int check(const char* where, nekomote_error e)
{
    if (e != NEKOMOTE_OK)
    {
        fprintf(stderr, "[abi_host] %s: error %d (%s)\n", where, (int)e,
                nekomote_error_message() ? nekomote_error_message() : "unknown");
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Parsing                                                            */
/* ------------------------------------------------------------------ */

static int parse_long(const char* s, long* out)
{
    if (!s || !*s)
    {
        fprintf(stderr, "[abi_host] empty numeric argument\n");
        return 0;
    }
    errno = 0;
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (errno == ERANGE || *end != '\0' || end == s)
    {
        fprintf(stderr, "[abi_host] invalid integer: '%s'\n", s);
        return 0;
    }
    *out = v;
    return 1;
}

static int parse_uint32(const char* s, uint32_t* out)
{
    if (!s || !*s)
    {
        fprintf(stderr, "[abi_host] empty numeric argument\n");
        return 0;
    }
    if (s[0] == '-')
    {
        fprintf(stderr, "[abi_host] negative value rejected: '%s'\n", s);
        return 0;
    }
    errno = 0;
    char* end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (errno == ERANGE || *end != '\0' || end == s || v > UINT32_MAX)
    {
        fprintf(stderr, "[abi_host] invalid uint32: '%s'\n", s);
        return 0;
    }
    *out = (uint32_t)v;
    return 1;
}

static int parse_window_size(const char* s, int* out)
{
    long v;
    if (!parse_long(s, &v))
        return 0;
    if (v < 1 || v > MAX_WINDOW_SIZE)
    {
        fprintf(stderr, "[abi_host] window size out of range 1..%d: '%s'\n", MAX_WINDOW_SIZE, s);
        return 0;
    }
    *out = (int)v;
    return 1;
}

/* ------------------------------------------------------------------ */
/* GLFW key → ABI key mapping                                         */
/* ------------------------------------------------------------------ */

static nekomote_key glfw_to_abi_key(int key)
{
    switch (key)
    {
        case GLFW_KEY_ESCAPE: return NEKOMOTE_KEY_ESCAPE;
        case GLFW_KEY_SPACE: return NEKOMOTE_KEY_SPACE;
        case GLFW_KEY_TAB: return NEKOMOTE_KEY_TAB;
        case GLFW_KEY_F: return NEKOMOTE_KEY_F;
        case GLFW_KEY_R: return NEKOMOTE_KEY_R;
        case GLFW_KEY_B: return NEKOMOTE_KEY_B;
        case GLFW_KEY_N: return NEKOMOTE_KEY_N;
        case GLFW_KEY_C: return NEKOMOTE_KEY_C;
        case GLFW_KEY_1: return NEKOMOTE_KEY_DIGIT_1;
        case GLFW_KEY_2: return NEKOMOTE_KEY_DIGIT_2;
        case GLFW_KEY_3: return NEKOMOTE_KEY_DIGIT_3;
        case GLFW_KEY_4: return NEKOMOTE_KEY_DIGIT_4;
        case GLFW_KEY_5: return NEKOMOTE_KEY_DIGIT_5;
        case GLFW_KEY_6: return NEKOMOTE_KEY_DIGIT_6;
        case GLFW_KEY_7: return NEKOMOTE_KEY_DIGIT_7;
        case GLFW_KEY_8: return NEKOMOTE_KEY_DIGIT_8;
        case GLFW_KEY_9: return NEKOMOTE_KEY_DIGIT_9;
    }
    return (nekomote_key)0;
}

/* ------------------------------------------------------------------ */
/* Host state                                                         */
/* ------------------------------------------------------------------ */

typedef struct
{
    nekomote_wallpaper* wp;
    GLFWwindow* window;
    int fb_width;
    int fb_height;
    int win_width;
    int win_height;
} Host;

static Host g_host;

static void to_centered(double x, double y, float* cx, float* cy)
{
    if (g_host.win_width <= 0 || g_host.win_height <= 0)
    {
        *cx = 0;
        *cy = 0;
        return;
    }
    *cx = (float)(x * g_host.fb_width / g_host.win_width - g_host.fb_width * 0.5);
    *cy = (float)(y * g_host.fb_height / g_host.win_height - g_host.fb_height * 0.5);
}

/* ------------------------------------------------------------------ */
/* GLFW callbacks                                                     */
/* ------------------------------------------------------------------ */

static void on_cursor_pos(GLFWwindow* w, double x, double y)
{
    (void)w;
    nekomote_wallpaper_pointer_move(g_host.wp, x, y);
}

static void on_cursor_enter(GLFWwindow* w, int entered)
{
    (void)w;
    if (entered)
    {
        double x = 0, y = 0;
        glfwGetCursorPos(w, &x, &y);
        nekomote_wallpaper_pointer_enter(g_host.wp, x, y);
    }
    else
        nekomote_wallpaper_pointer_leave(g_host.wp);
}

static void on_mouse_button(GLFWwindow* w, int button, int action, int)
{
    (void)w;
    nekomote_mouse_action ma = (action == GLFW_PRESS) ? NEKOMOTE_MOUSE_PRESS : NEKOMOTE_MOUSE_RELEASE;
    nekomote_wallpaper_mouse_button(g_host.wp, button, ma);
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        double x = 0, y = 0;
        glfwGetCursorPos(w, &x, &y);
        float cx, cy;
        to_centered(x, y, &cx, &cy);
        nekomote_wallpaper_touch(g_host.wp, cx, cy);
    }
}

static void on_scroll(GLFWwindow* w, double, double amount)
{
    (void)w;
    nekomote_wallpaper_mouse_scroll(g_host.wp, amount);
}

static void on_key(GLFWwindow* w, int key, int, int action, int)
{
    (void)w;
    nekomote_key ak = glfw_to_abi_key(key);
    if (ak == 0)
        return;
    nekomote_mouse_action ma = (action == GLFW_PRESS) ? NEKOMOTE_MOUSE_PRESS : NEKOMOTE_MOUSE_RELEASE;
    nekomote_wallpaper_key(g_host.wp, ak, ma);
}

static void on_framebuffer_size(GLFWwindow* w, int width, int height)
{
    (void)w;
    g_host.fb_width = width;
    g_host.fb_height = height;
    glfwGetWindowSize(w, &g_host.win_width, &g_host.win_height);
    nekomote_wallpaper_set_output_size(g_host.wp, width, height);
}

/* ------------------------------------------------------------------ */
/* Minimal PNG encoder (RGBA8, zlib compressed)                       */
/* ------------------------------------------------------------------ */

static void png_write_chunk(FILE* f, const char* type, const uint8_t* data, uint32_t len)
{
    uint8_t header[4];
    header[0] = (uint8_t)(len >> 24);
    header[1] = (uint8_t)(len >> 16);
    header[2] = (uint8_t)(len >> 8);
    header[3] = (uint8_t)len;
    fwrite(header, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len > 0)
        fwrite(data, 1, len, f);
    uint32_t crc = (uint32_t)crc32(0L, Z_NULL, 0);
    crc = (uint32_t)crc32(crc, (const Bytef*)type, 4);
    if (len > 0)
        crc = (uint32_t)crc32(crc, (const Bytef*)data, len);
    uint8_t crc_bytes[4];
    crc_bytes[0] = (uint8_t)(crc >> 24);
    crc_bytes[1] = (uint8_t)(crc >> 16);
    crc_bytes[2] = (uint8_t)(crc >> 8);
    crc_bytes[3] = (uint8_t)crc;
    fwrite(crc_bytes, 1, 4, f);
}

static int save_screenshot_png(const char* path, int width, int height)
{
    size_t pixel_count = (size_t)width * (size_t)height * 4;
    uint8_t* pixels = malloc(pixel_count);
    if (!pixels)
        return 0;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        free(pixels);
        return 0;
    }
    size_t row_bytes = (size_t)width * 4;
    uint8_t* row = malloc(row_bytes);
    if (!row)
    {
        free(pixels);
        return 0;
    }
    for (int y = 0; y < height / 2; y++)
    {
        uint8_t* top = pixels + (size_t)y * row_bytes;
        uint8_t* bot = pixels + (size_t)(height - 1 - y) * row_bytes;
        memcpy(row, top, row_bytes);
        memcpy(top, bot, row_bytes);
        memcpy(bot, row, row_bytes);
    }
    free(row);
    size_t raw_len = (size_t)height * (row_bytes + 1);
    uint8_t* raw = malloc(raw_len);
    if (!raw)
    {
        free(pixels);
        return 0;
    }
    for (int y = 0; y < height; y++)
    {
        raw[(size_t)y * (row_bytes + 1)] = 0;
        memcpy(raw + (size_t)y * (row_bytes + 1) + 1, pixels + (size_t)y * row_bytes, row_bytes);
    }
    free(pixels);
    uLongf compressed_len = compressBound(raw_len);
    uint8_t* compressed = malloc(compressed_len);
    if (!compressed)
    {
        free(raw);
        return 0;
    }
    if (compress(compressed, &compressed_len, raw, raw_len) != Z_OK)
    {
        free(raw);
        free(compressed);
        return 0;
    }
    free(raw);
    FILE* f = fopen(path, "wb");
    if (!f)
    {
        free(compressed);
        return 0;
    }
    static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(signature, 1, 8, f);
    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(width >> 24);
    ihdr[1] = (uint8_t)(width >> 16);
    ihdr[2] = (uint8_t)(width >> 8);
    ihdr[3] = (uint8_t)width;
    ihdr[4] = (uint8_t)(height >> 24);
    ihdr[5] = (uint8_t)(height >> 16);
    ihdr[6] = (uint8_t)(height >> 8);
    ihdr[7] = (uint8_t)height;
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    png_write_chunk(f, "IHDR", ihdr, 13);
    png_write_chunk(f, "IDAT", compressed, (uint32_t)compressed_len);
    png_write_chunk(f, "IEND", NULL, 0);
    free(compressed);
    fclose(f);
    printf("[abi_host] captured %dx%d PNG to %s\n", width, height, path);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Usage                                                              */
/* ------------------------------------------------------------------ */

static void print_usage(const char* prog)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s --assets <dir> [options]\n", prog);
    fprintf(stderr, "  %s --model <m.nkm.json> --session <s.nks.json> [options]\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  --width <N>       Window width (default 800, 1..8192)\n");
    fprintf(stderr, "  --height <N>      Window height (default 600, 1..8192)\n");
    fprintf(stderr, "  --steps <N>       Fixed-step: N steps at dt=1/60s, then exit\n");
    fprintf(stderr, "  --seed <N>        Random seed (default 42)\n");
    fprintf(stderr, "  --no-save         Do not save config\n");
    fprintf(stderr, "  --capture <path>  Save final frame as PNG (requires --steps)\n");
    fprintf(stderr, "  --ui-open         Start with UI panel open\n");
    fprintf(stderr, "\nThis is a C ABI wallpaper verification tool (always muted).\n");
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char** argv)
{
    const char* assets_dir = NULL;
    const char* model_path = NULL;
    const char* session_path = NULL;
    const char* capture_path = NULL;
    int width = 800;
    int height = 600;
    long steps = 0;
    uint32_t seed = 42;
    int no_save = 0;
    int ui_open = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc)
            assets_dir = argv[++i];
        else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc)
            model_path = argv[++i];
        else if (strcmp(argv[i], "--session") == 0 && i + 1 < argc)
            session_path = argv[++i];
        else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc)
        {
            if (!parse_window_size(argv[++i], &width))
                return 2;
        }
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)
        {
            if (!parse_window_size(argv[++i], &height))
                return 2;
        }
        else if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc)
        {
            if (!parse_long(argv[++i], &steps))
                return 2;
            if (steps < 0 || steps > MAX_STEPS)
                return 2;
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            if (!parse_uint32(argv[++i], &seed))
                return 2;
        }
        else if (strcmp(argv[i], "--no-save") == 0)
            no_save = 1;
        else if (strcmp(argv[i], "--capture") == 0 && i + 1 < argc)
            capture_path = argv[++i];
        else if (strcmp(argv[i], "--ui-open") == 0)
            ui_open = 1;
        else if (strcmp(argv[i], "--mute") == 0)
            ; /* always muted */
        else
        {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        }
    }

    if (!assets_dir && (!model_path || !session_path))
    {
        print_usage(argv[0]);
        return 2;
    }

    int fixed_mode = steps > 0;
    if (capture_path && !fixed_mode)
    {
        fprintf(stderr, "[abi_host] --capture requires --steps > 0\n");
        return 2;
    }

    printf("nekomote_abi_host — C ABI wallpaper tool (muted)\n");
    printf("  version: %s\n", nekomote_version());
    printf("  mode: %s\n", fixed_mode ? "fixed-step (dt=1/60s)" : "interactive");
    printf("  audio: disabled\n\n");

    /* ---- Init GLFW ---- */
    if (!glfwInit())
    {
        fprintf(stderr, "Error: glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, capture_path ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(width, height, "NekoMote ABI Host", NULL, NULL);
    if (!window)
    {
        fprintf(stderr, "Error: glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(fixed_mode ? 0 : 1);

    if (!gladLoadGL(glfwGetProcAddress))
    {
        fprintf(stderr, "Error: gladLoadGL failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    printf("  GL: %s\n", glGetString(GL_VERSION));

    glfwGetFramebufferSize(window, &g_host.fb_width, &g_host.fb_height);
    glfwGetWindowSize(window, &g_host.win_width, &g_host.win_height);

    /* ---- Create wallpaper instance ---- */
    nekomote_wallpaper* wp = nekomote_wallpaper_create(glfwGetProcAddress);
    if (!wp)
    {
        fprintf(stderr, "Error: wallpaper create failed: %s\n",
                nekomote_error_message() ? nekomote_error_message() : "unknown");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    g_host.wp = wp;
    g_host.window = window;

    if (!check("set_seed", nekomote_wallpaper_set_seed(wp, seed)))
        goto fail;
    if (!check("set_muted", nekomote_wallpaper_set_muted(wp, 1)))
        goto fail;
    if (no_save)
        nekomote_wallpaper_set_no_save(wp, 1);
    if (!check("set_output_size",
               nekomote_wallpaper_set_output_size(wp, g_host.fb_width, g_host.fb_height)))
        goto fail;

    if (assets_dir)
    {
        if (!check("set_assets_dir", nekomote_wallpaper_set_assets_dir(wp, assets_dir)))
            goto fail;
        if (!check("load_initial", nekomote_wallpaper_load_initial(wp)))
            goto fail;
        printf("  characters: %d  backgrounds: %d\n",
               nekomote_wallpaper_character_count(wp),
               nekomote_wallpaper_background_count(wp));
        printf("  current: %s\n", nekomote_wallpaper_current_character_name(wp));
    }
    else
    {
        if (!check("load_model",
                   nekomote_wallpaper_load_model(wp, model_path, session_path)))
            goto fail;
    }

    /* ---- Set up callbacks ---- */
    if (!capture_path)
    {
        glfwSetWindowUserPointer(window, &g_host);
        glfwSetCursorPosCallback(window, on_cursor_pos);
        glfwSetCursorEnterCallback(window, on_cursor_enter);
        glfwSetMouseButtonCallback(window, on_mouse_button);
        glfwSetScrollCallback(window, on_scroll);
        glfwSetKeyCallback(window, on_key);
        glfwSetFramebufferSizeCallback(window, on_framebuffer_size);
    }

    printf("\nRunning. Esc=quit%s\n\n", fixed_mode ? " (fixed-step auto-exit)" : "");

    double last_time = glfwGetTime();
    long frame_count = 0;
    int exit_code = 0;
    int captured = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        double dt;
        if (fixed_mode)
            dt = FIXED_DT;
        else
        {
            double now = glfwGetTime();
            dt = now - last_time;
            last_time = now;
            if (dt > 0.25)
                dt = 0.25;
        }

        if (!check("advance", nekomote_wallpaper_advance(wp, dt)))
        {
            exit_code = 1;
            break;
        }
        if (!check("render_frame", nekomote_wallpaper_render_frame(wp)))
        {
            exit_code = 1;
            break;
        }
        if (!capture_path || ui_open)
            nekomote_wallpaper_render_ui(wp, (float)dt);

        if (capture_path && fixed_mode && frame_count + 1 >= steps && !captured)
        {
            if (!save_screenshot_png(capture_path, g_host.fb_width, g_host.fb_height))
                exit_code = 1;
            captured = 1;
        }

        glfwSwapBuffers(window);
        frame_count++;

        if (frame_count % 300 == 0)
            printf("  frame %ld  dt=%.2fms\n", frame_count, dt * 1000.0);

        if (fixed_mode && frame_count >= steps)
        {
            printf("  reached %ld fixed steps, exiting\n", steps);
            break;
        }
    }

    if (fixed_mode && frame_count != steps && exit_code == 0)
        exit_code = 1;
    if (capture_path && !captured && exit_code == 0)
    {
        fprintf(stderr, "[abi_host] fixed-step capture did not complete\n");
        exit_code = 1;
    }

    printf("\nRendered %ld frames. Shutting down.\n", frame_count);

    nekomote_wallpaper_destroy(wp);
    glfwDestroyWindow(window);
    glfwTerminate();
    return exit_code;

fail:
    nekomote_wallpaper_destroy(wp);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
}
