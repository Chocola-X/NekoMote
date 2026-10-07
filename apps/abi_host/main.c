/*
 * nekomote_abi_host — C ABI 诊断验证工具（显式静音）。
 *
 * 验证 C ABI 的模型加载、编译、会话、渲染管线。
 * 正式声音和壁纸会话由 C++ player 提供；本工具不复制 PCM/Pulse/线程服务。
 * 设备失败不能被当作静音成功；本工具始终设 NEKOMOTE_AUDIO_DISABLED。
 *
 * 两种运行模式：
 *   固定步模式（--steps N，N>0）：每步 dt=1/60 秒，不依赖墙钟，N 步后自动退出。
 *     截图在最后一帧 render 后、swap 前读回，保证确定像素。
 *   交互模式（--steps 0，默认）：用墙钟 dt，运行直到 Esc 或窗口关闭。
 *     截图在退出前读回，像素可能受 swap 后 framebuffer 不确定影响，仅作粗略诊断。
 *
 * 用法：
 *   nekomote_abi_host --model <model.nkm.json> --session <session.nks.json> [options]
 *
 * 选项：
 *   --width <N>       窗口宽度（默认 800，1..65535）
 *   --height <N>      窗口高度（默认 600，1..65535）
 *   --steps <N>       固定步数后自动退出（默认 0=交互模式，墙钟 dt）
 *   --seed <N>        随机种子（默认 42，0..4294967295）
 *   --reaction <name> 指定初始反应名称
 *   --mute            显式静音（本工具始终静音，此标志仅作记录）
 *   --no-save         不保存任何配置（本工具不写系统设置）
 *   --capture <path>  退出前截图保存为 PNG（GL 读回，仅诊断）
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

#define MAX_PATH 4096
#define FIXED_DT (1.0 / 60.0)

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
/* Strict numeric parsing                                             */
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
    if (v < 1 || v > 65535)
    {
        fprintf(stderr, "[abi_host] window size out of range 1..65535: '%s'\n", s);
        return 0;
    }
    *out = (int)v;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Path helpers                                                       */
/* ------------------------------------------------------------------ */

static int dir_of(const char* file_path, char* out, size_t out_size)
{
    if (out_size == 0)
        return 0;
    const char* slash = strrchr(file_path, '/');
    if (slash)
    {
        size_t len = (size_t)(slash - file_path);
        if (len >= out_size)
        {
            fprintf(stderr, "[abi_host] model directory path truncated: %s\n", file_path);
            return 0;
        }
        memcpy(out, file_path, len);
        out[len] = '\0';
    }
    else
    {
        if (out_size < 2)
        {
            fprintf(stderr, "[abi_host] buffer too small for default directory\n");
            return 0;
        }
        out[0] = '.';
        out[1] = '\0';
    }
    return 1;
}

static int join_path(char* out, size_t out_size, const char* dir, const char* name)
{
    int n = snprintf(out, out_size, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= out_size)
    {
        fprintf(stderr, "[abi_host] path truncated: %s/%s\n", dir, name);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Texture upload                                                     */
/* ------------------------------------------------------------------ */

static int upload_textures(nekomote_program* program, nekomote_renderer* renderer,
                           const char* model_dir)
{
    size_t tex_count = nekomote_program_texture_count(program);
    int uploaded = 0;
    for (size_t i = 0; i < tex_count; i++)
    {
        nekomote_texture_info tex = nekomote_program_texture(program, i);
        if (!tex.path || tex.id == 0)
            continue;
        char full_path[MAX_PATH];
        if (!join_path(full_path, sizeof(full_path), model_dir, tex.path))
            return -1;
        nekomote_image* img = nekomote_image_load_png(full_path);
        if (!img)
        {
            fprintf(stderr, "[abi_host] texture %zu: failed to load %s\n", i, full_path);
            return -1;
        }
        nekomote_error ue = nekomote_renderer_upload_texture(
            renderer, tex.id, nekomote_image_width(img), nekomote_image_height(img),
            nekomote_image_pixels(img), nekomote_image_byte_count(img));
        nekomote_image_destroy(img);
        if (!check("upload_texture", ue))
            return -1;
        uploaded++;
    }
    return uploaded;
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
        fprintf(stderr, "[abi_host] glReadPixels error: 0x%x\n", err);
        free(pixels);
        return 0;
    }

    /* Flip vertically (OpenGL origin is bottom-left) */
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

    /* Build scanlines with filter byte 0 per row */
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
        fprintf(stderr, "[abi_host] zlib compress failed\n");
        free(raw);
        free(compressed);
        return 0;
    }
    free(raw);

    FILE* f = fopen(path, "wb");
    if (!f)
    {
        fprintf(stderr, "[abi_host] cannot open capture file: %s\n", path);
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
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 6;  /* color type RGBA */
    ihdr[10] = 0; /* compression */
    ihdr[11] = 0; /* filter */
    ihdr[12] = 0; /* interlace */
    png_write_chunk(f, "IHDR", ihdr, 13);
    png_write_chunk(f, "IDAT", compressed, (uint32_t)compressed_len);
    png_write_chunk(f, "IEND", NULL, 0);

    free(compressed);
    fclose(f);
    printf("[abi_host] captured %dx%d PNG to %s (%zu bytes compressed)\n", width, height, path,
           (size_t)compressed_len);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Usage                                                              */
/* ------------------------------------------------------------------ */

static void print_usage(const char* prog)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s --model <model.nkm.json> --session <session.nks.json> [options]\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  --width <N>       Window width (default 800, range 1..65535)\n");
    fprintf(stderr, "  --height <N>      Window height (default 600, range 1..65535)\n");
    fprintf(stderr, "  --steps <N>       Fixed-step mode: N author steps at dt=1/60s, then exit\n");
    fprintf(stderr, "                     (default 0=interactive mode, wall-clock dt)\n");
    fprintf(stderr, "  --seed <N>        Random seed (default 42, range 0..4294967295)\n");
    fprintf(stderr, "  --reaction <name> Initial reaction label\n");
    fprintf(stderr, "  --mute            Explicit mute (always muted; audio disabled)\n");
    fprintf(stderr, "  --no-save         Do not write any config (never writes)\n");
    fprintf(stderr, "  --capture <path>  Save final frame as PNG (GL readback, diagnostic only)\n");
    fprintf(stderr, "\nFixed-step mode (--steps N, N>0) is deterministic: each step uses\n");
    fprintf(stderr, "dt=1/60s, no wall-clock dependence. Screenshot is read back before\n");
    fprintf(stderr, "swap for stable pixels. Interactive mode uses wall-clock dt.\n");
    fprintf(stderr, "\nThis is a C ABI diagnostic tool, not a wallpaper plugin.\n");
    fprintf(stderr, "Audio is always disabled; use nekomote_player for full playback.\n");
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char** argv)
{
    const char* model_path = NULL;
    const char* session_path = NULL;
    const char* reaction_name = NULL;
    const char* capture_path = NULL;
    int width = 800;
    int height = 600;
    long steps = 0;
    uint32_t seed = 42;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--model") == 0 && i + 1 < argc)
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
            if (steps < 0)
            {
                fprintf(stderr, "[abi_host] --steps must be >= 0\n");
                return 2;
            }
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            if (!parse_uint32(argv[++i], &seed))
                return 2;
        }
        else if (strcmp(argv[i], "--reaction") == 0 && i + 1 < argc)
            reaction_name = argv[++i];
        else if (strcmp(argv[i], "--capture") == 0 && i + 1 < argc)
            capture_path = argv[++i];
        else if (strcmp(argv[i], "--mute") == 0)
            ; /* always muted */
        else if (strcmp(argv[i], "--no-save") == 0)
            ; /* never writes */
        else
        {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        }
    }

    if (!model_path || !session_path)
    {
        print_usage(argv[0]);
        return 2;
    }

    int fixed_mode = steps > 0;
    printf("nekomote_abi_host — C ABI diagnostic tool (muted)\n");
    printf("  version: %s\n", nekomote_version());
    printf("  model: %s\n", model_path);
    printf("  session: %s\n", session_path);
    printf("  mode: %s\n", fixed_mode ? "fixed-step (dt=1/60s)" : "interactive (wall-clock)");
    printf("  audio: disabled (diagnostic mode)\n\n");

    /* ---- Load assets (program before session, per ABI contract) ---- */
    char model_dir[MAX_PATH];
    if (!dir_of(model_path, model_dir, sizeof(model_dir)))
        return 2;

    nekomote_model* model = nekomote_model_load(model_path);
    if (!model)
    {
        fprintf(stderr, "Error: model load failed: %s\n",
                nekomote_error_message() ? nekomote_error_message() : "unknown");
        return 1;
    }

    nekomote_session_asset* session_asset = nekomote_session_asset_load(session_path);
    if (!session_asset)
    {
        fprintf(stderr, "Error: session asset load failed: %s\n",
                nekomote_error_message() ? nekomote_error_message() : "unknown");
        nekomote_model_destroy(model);
        return 1;
    }

    nekomote_program* program = nekomote_program_compile(model, session_asset);
    if (!program)
    {
        fprintf(stderr, "Error: program compile failed: %s\n",
                nekomote_error_message() ? nekomote_error_message() : "unknown");
        nekomote_session_asset_destroy(session_asset);
        nekomote_model_destroy(model);
        return 1;
    }

    printf("  textures: %zu  gaze: %d  touch: %d  mirror: %d  audio_clips: %zu\n",
           nekomote_program_texture_count(program), nekomote_program_gaze_available(program),
           nekomote_program_touch_available(program), nekomote_program_mirror(program),
           nekomote_program_audio_count(program));

    /* ---- Init GLFW and GL (needed for renderer) ---- */
    if (!glfwInit())
    {
        fprintf(stderr, "Error: glfwInit failed\n");
        nekomote_program_destroy(program);
        nekomote_session_asset_destroy(session_asset);
        nekomote_model_destroy(model);
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(width, height, "NekoMote ABI Host", NULL, NULL);
    if (!window)
    {
        fprintf(stderr, "Error: glfwCreateWindow failed\n");
        glfwTerminate();
        nekomote_program_destroy(program);
        nekomote_session_asset_destroy(session_asset);
        nekomote_model_destroy(model);
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(fixed_mode ? 0 : 1);

    if (!gladLoadGL(glfwGetProcAddress))
    {
        fprintf(stderr, "Error: gladLoadGL failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        nekomote_program_destroy(program);
        nekomote_session_asset_destroy(session_asset);
        nekomote_model_destroy(model);
        return 1;
    }
    printf("  GL: %s\n", glGetString(GL_VERSION));

    int fb_width, fb_height;
    glfwGetFramebufferSize(window, &fb_width, &fb_height);

    /* ---- Create session (audio always disabled) ---- */
    nekomote_session_config config = {0};
    config.random_seed = seed;
    config.pointer = NEKOMOTE_POINTER_LOCAL_SURFACE;
    config.audio = NEKOMOTE_AUDIO_DISABLED;
    nekomote_session* session = nekomote_session_create(program, config);
    if (!session)
    {
        fprintf(stderr, "Error: session create failed: %s\n",
                nekomote_error_message() ? nekomote_error_message() : "unknown");
        glfwDestroyWindow(window);
        glfwTerminate();
        nekomote_program_destroy(program);
        nekomote_session_asset_destroy(session_asset);
        nekomote_model_destroy(model);
        return 1;
    }

    if (!check("session_start", nekomote_session_start(session)))
        goto fail_session;
    if (!check("set_output_size", nekomote_session_set_output_size(session, fb_width, fb_height)))
        goto fail_session;

    if (reaction_name)
    {
        uint32_t rid = nekomote_program_find_reaction(program, reaction_name);
        if (rid == 0)
        {
            fprintf(stderr, "Error: reaction '%s' not found\n", reaction_name);
            goto fail_session;
        }
        if (!check("react", nekomote_session_react(session, rid)))
            goto fail_session;
        printf("  reaction: %s (id=%u)\n", reaction_name, rid);
    }

    /* ---- Create renderer and reserve capacity from program ---- */
    nekomote_renderer* renderer = nekomote_renderer_create(glfwGetProcAddress);
    if (!renderer)
    {
        fprintf(stderr, "Error: renderer create failed\n");
        goto fail_session;
    }

    nekomote_renderer_capacity cap = nekomote_program_renderer_capacity(program);
    if (cap.vertices == 0 && cap.indices == 0)
    {
        fprintf(stderr, "Error: program renderer capacity is zero\n");
        goto fail_renderer;
    }
    if (!check("reserve", nekomote_renderer_reserve(renderer, cap)))
        goto fail_renderer;

    int uploaded = upload_textures(program, renderer, model_dir);
    if (uploaded < 0)
    {
        fprintf(stderr, "Error: texture upload failed; aborting\n");
        goto fail_renderer;
    }
    printf("  textures uploaded: %d\n", uploaded);

    /* ---- Main loop ---- */
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

        if (!check("advance", nekomote_session_advance(session, dt)))
        {
            exit_code = 1;
            break;
        }
        if (!check("prepare_frame", nekomote_session_prepare_frame(session)))
        {
            exit_code = 1;
            break;
        }

        nekomote_render_frame frame = nekomote_session_frame(session);
        if (!check("prepare", nekomote_renderer_prepare(renderer, frame)))
        {
            exit_code = 1;
            break;
        }
        if (!check("resize_output", nekomote_renderer_resize_output(renderer, fb_width, fb_height)))
        {
            exit_code = 1;
            break;
        }

        nekomote_render_target target = {0};
        target.width = fb_width;
        target.height = fb_height;

        nekomote_render_view view = {0};
        view.origin_x = (float)fb_width * 0.5f;
        view.origin_y = (float)fb_height * 0.5f;
        view.scale = 1.0f;
        view.clear[0] = 0.06f;
        view.clear[1] = 0.07f;
        view.clear[2] = 0.10f;
        view.clear[3] = 1.0f;
        view.alpha = 1.0f;
        view.flip_x = nekomote_program_mirror(program);

        if (!check("render",
                   nekomote_renderer_render(renderer, target, view, NEKOMOTE_TARGET_CLEAR)))
        {
            exit_code = 1;
            break;
        }

        /* In fixed-step mode, capture before swap for stable pixels */
        if (capture_path && fixed_mode && frame_count + 1 >= steps && !captured)
        {
            if (!save_screenshot_png(capture_path, fb_width, fb_height))
                exit_code = 1;
            captured = 1;
        }

        glfwSwapBuffers(window);
        frame_count++;

        if (frame_count % 300 == 0)
        {
            printf("  frame %ld  dt=%.2fms  verts=%zu  cmds=%zu\n", frame_count, dt * 1000.0,
                   frame.vertex_count, frame.command_count);
        }

        if (fixed_mode && frame_count >= steps)
        {
            printf("  reached %ld fixed steps, exiting\n", steps);
            break;
        }
    }

    /* In interactive mode, capture after loop (pixels may be less stable) */
    if (capture_path && !captured && exit_code == 0)
    {
        if (!save_screenshot_png(capture_path, fb_width, fb_height))
            exit_code = 1;
    }

    printf("\nRendered %ld frames. Shutting down.\n", frame_count);

    /* ---- Cleanup: GPU resources before context, session before program ---- */
    nekomote_renderer_destroy(renderer);
    nekomote_session_destroy(session);
    nekomote_program_destroy(program);
    nekomote_session_asset_destroy(session_asset);
    nekomote_model_destroy(model);
    glfwDestroyWindow(window);
    glfwTerminate();
    return exit_code;

fail_renderer:
    nekomote_renderer_destroy(renderer);
fail_session:
    nekomote_session_destroy(session);
    nekomote_program_destroy(program);
    nekomote_session_asset_destroy(session_asset);
    nekomote_model_destroy(model);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
}
