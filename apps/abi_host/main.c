/*
 * nekomote_abi_host — C ABI 诊断验证工具（显式静音）。
 *
 * 验证 C ABI 的模型加载、编译、会话、渲染管线。
 * 正式声音和壁纸会话由 C++ player 提供；本工具不复制 PCM/Pulse/线程服务。
 * 设备失败不能被当作静音成功；本工具始终设 NEKOMOTE_AUDIO_DISABLED。
 *
 * 用法：
 *   nekomote_abi_host --model <model.nkm.json> --session <session.nks.json> [options]
 *
 * 选项：
 *   --width <N>       窗口宽度（默认 800）
 *   --height <N>      窗口高度（默认 600）
 *   --steps <N>       固定步数后自动退出（默认 0=无限）
 *   --seed <N>        随机种子（默认 42）
 *   --reaction <name> 指定初始反应名称
 *   --mute            显式静音（本工具始终静音，此标志仅作记录）
 *   --no-save         不保存任何配置（本工具不写系统设置）
 *   --capture <path>  退出前截图保存为 PNG（GL 读回，仅诊断）
 */

// clang-format off: glad must precede GLFW
#include <glad/gl.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <math.h>
#include <nekomote/abi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PATH 1024

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
/* Path helpers                                                       */
/* ------------------------------------------------------------------ */

static void dir_of(const char* file_path, char* out, size_t out_size)
{
    if (out_size == 0)
        return;
    const char* slash = strrchr(file_path, '/');
    if (slash)
    {
        size_t len = (size_t)(slash - file_path);
        if (len >= out_size)
            len = out_size - 1;
        memcpy(out, file_path, len);
        out[len] = '\0';
    }
    else
    {
        out[0] = '.';
        out[1] = '\0';
    }
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
            continue;
        nekomote_image* img = nekomote_image_load_png(full_path);
        if (!img)
        {
            fprintf(stderr, "[abi_host] texture %zu: failed to load %s\n", i, full_path);
            continue;
        }
        nekomote_error ue = nekomote_renderer_upload_texture(
            renderer, tex.id, nekomote_image_width(img), nekomote_image_height(img),
            nekomote_image_pixels(img), nekomote_image_byte_count(img));
        nekomote_image_destroy(img);
        if (!check("upload_texture", ue))
            continue;
        uploaded++;
    }
    return uploaded;
}

/* ------------------------------------------------------------------ */
/* Screenshot (GL readback, diagnostic only)                          */
/* ------------------------------------------------------------------ */

static int save_screenshot(const char* path, int width, int height)
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

    FILE* f = fopen(path, "wb");
    if (!f)
    {
        fprintf(stderr, "[abi_host] cannot open capture file: %s\n", path);
        free(pixels);
        return 0;
    }
    /* Flip vertically (OpenGL origin is bottom-left) */
    size_t row_bytes = (size_t)width * 4;
    uint8_t* row = malloc(row_bytes);
    for (int y = 0; y < height / 2; y++)
    {
        uint8_t* top = pixels + (size_t)y * row_bytes;
        uint8_t* bot = pixels + (size_t)(height - 1 - y) * row_bytes;
        memcpy(row, top, row_bytes);
        memcpy(top, bot, row_bytes);
        memcpy(bot, row, row_bytes);
    }
    free(row);

    /* Write minimal PNG via stbiw not available; write raw RGBA as .rgba */
    /* Actually, write a simple binary dump for diagnostic use */
    /* The capture path extension determines format; we use .rgba for raw */
    size_t written = fwrite(pixels, 1, pixel_count, f);
    fclose(f);
    free(pixels);
    if (written != pixel_count)
    {
        fprintf(stderr, "[abi_host] short write to capture file\n");
        return 0;
    }
    printf("[abi_host] captured %dx%d RGBA to %s (%zu bytes)\n", width, height, path, pixel_count);
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
    fprintf(stderr, "  --width <N>       Window width (default 800)\n");
    fprintf(stderr, "  --height <N>      Window height (default 600)\n");
    fprintf(stderr, "  --steps <N>       Exit after N frames (default 0=run until Esc)\n");
    fprintf(stderr, "  --seed <N>        Random seed (default 42)\n");
    fprintf(stderr, "  --reaction <name> Initial reaction label\n");
    fprintf(stderr, "  --mute            Explicit mute (always muted; audio disabled)\n");
    fprintf(stderr, "  --no-save         Do not write any config (never writes)\n");
    fprintf(stderr, "  --capture <path>  Save final frame RGBA to file (GL readback)\n");
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
            width = atoi(argv[++i]);
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)
            height = atoi(argv[++i]);
        else if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc)
            steps = atol(argv[++i]);
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            seed = (uint32_t)strtoul(argv[++i], NULL, 10);
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
    if (width <= 0 || height <= 0)
    {
        fprintf(stderr, "Error: invalid width/height\n");
        return 2;
    }

    printf("nekomote_abi_host — C ABI diagnostic tool (muted)\n");
    printf("  version: %s\n", nekomote_version());
    printf("  model: %s\n", model_path);
    printf("  session: %s\n", session_path);
    printf("  audio: disabled (diagnostic mode)\n\n");

    /* ---- Load assets (program before session, per ABI contract) ---- */
    char model_dir[MAX_PATH];
    dir_of(model_path, model_dir, sizeof(model_dir));

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
    glfwSwapInterval(1);

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
    printf("  textures uploaded: %d\n", uploaded);

    /* ---- Main loop ---- */
    printf("\nRunning. Esc=quit%s\n\n", steps > 0 ? " (auto-exit after steps)" : "");

    double last_time = glfwGetTime();
    long frame_count = 0;
    int exit_code = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        double now = glfwGetTime();
        double dt = now - last_time;
        last_time = now;
        if (dt > 0.25)
            dt = 0.25;

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
        glfwSwapBuffers(window);
        frame_count++;

        if (frame_count % 300 == 0)
        {
            printf("  frame %ld  dt=%.2fms  verts=%zu  cmds=%zu\n", frame_count, dt * 1000.0,
                   frame.vertex_count, frame.command_count);
        }

        if (steps > 0 && frame_count >= steps)
        {
            printf("  reached %ld steps, exiting\n", steps);
            break;
        }
    }

    /* ---- Screenshot (GL readback, diagnostic only) ---- */
    if (capture_path && exit_code == 0)
    {
        if (!save_screenshot(capture_path, fb_width, fb_height))
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
