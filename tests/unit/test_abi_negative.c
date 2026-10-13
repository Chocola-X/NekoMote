/*
 * test_abi_negative.c — C ABI negative tests.
 *
 * Pure C, no real assets, no GPU context, no display server.
 * Verifies NULL rejection, error diagnostic API, and failed-init RAII.
 * Live-handle validation is in abi_synthetic_boundary and renderer_gpu.
 * Safe to run under ASan/LSan in headless CI.
 */

#include <assert.h>
#include <limits.h>
#include <nekomote/abi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_run = 0;
static int g_fail = 0;

#define TEST(name)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        g_run++;                                                                                   \
        printf("  %s ... ", name);                                                                 \
        fflush(stdout);                                                                            \
    } while (0)
#define PASS() printf("ok\n")
#define FAIL(msg)                                                                                  \
    do                                                                                             \
    {                                                                                              \
        printf("FAIL: %s\n", msg);                                                                 \
        g_fail++;                                                                                  \
    } while (0)
#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            FAIL(msg);                                                                             \
            return;                                                                                \
        }                                                                                          \
    } while (0)

/* Stub GL loader that returns NULL for every proc (simulates no GL). */
static nekomote_gl_proc null_loader(const char* name)
{
    (void)name;
    return NULL;
}

/* ---- Error diagnostic API ---- */

static void test_error_diagnostic(void)
{
    TEST("error: clear on fresh state");
    nekomote_clear_error();
    CHECK(nekomote_last_error() == NEKOMOTE_OK, "should be OK after clear");
    CHECK(nekomote_error_message() == NULL, "message should be NULL when no error");
    PASS();

    TEST("error: model_load NULL sets error");
    nekomote_clear_error();
    (void)nekomote_model_load(NULL);
    CHECK(nekomote_last_error() != NEKOMOTE_OK, "should have error after null load");
    CHECK(nekomote_error_message() != NULL, "should have message");
    PASS();

    TEST("error: clear resets");
    nekomote_clear_error();
    CHECK(nekomote_last_error() == NEKOMOTE_OK, "should be OK after clear");
    PASS();
}

/* ---- NULL safety ---- */

static void test_null_safety(void)
{
    TEST("null: model_load(NULL)");
    CHECK(nekomote_model_load(NULL) == NULL, "should reject NULL");
    PASS();

    TEST("null: session_asset_load(NULL)");
    CHECK(nekomote_session_asset_load(NULL) == NULL, "should reject NULL");
    PASS();

    TEST("null: program_compile(NULL,NULL)");
    CHECK(nekomote_program_compile(NULL, NULL) == NULL, "should reject NULL");
    PASS();

    TEST("null: session_create(NULL)");
    nekomote_session_config cfg = {0};
    CHECK(nekomote_session_create(NULL, cfg) == NULL, "should reject NULL");
    PASS();

    TEST("null: renderer_create(NULL)");
    CHECK(nekomote_renderer_create(NULL) == NULL, "should reject NULL");
    PASS();

    TEST("null: wallpaper_fps_limit(NULL)");
    CHECK(nekomote_wallpaper_fps_limit(NULL) == -1, "should reject NULL");
    PASS();

    TEST("null: image_load_png(NULL)");
    CHECK(nekomote_image_load_png(NULL) == NULL, "should reject NULL");
    PASS();

    TEST("null: destroy accepts NULL");
    nekomote_model_destroy(NULL);
    nekomote_session_asset_destroy(NULL);
    nekomote_program_destroy(NULL);
    nekomote_session_destroy(NULL);
    nekomote_renderer_destroy(NULL);
    nekomote_image_destroy(NULL);
    PASS();

    TEST("null: session functions on NULL");
    nekomote_session* s = NULL;
    CHECK(nekomote_session_start(s) == NEKOMOTE_ERROR_INVALID_INPUT, "start(NULL)");
    CHECK(nekomote_session_advance(s, 0.016) == NEKOMOTE_ERROR_INVALID_INPUT, "advance(NULL)");
    CHECK(nekomote_session_prepare_frame(s) == NEKOMOTE_ERROR_INVALID_INPUT, "prepare(NULL)");
    CHECK(nekomote_session_reset(s) == NEKOMOTE_ERROR_INVALID_INPUT, "reset(NULL)");
    CHECK(nekomote_session_set_origin(s, 0, 0) == NEKOMOTE_ERROR_INVALID_INPUT, "set_origin(NULL)");
    CHECK(nekomote_session_set_scale(s, 1) == NEKOMOTE_ERROR_INVALID_INPUT, "set_scale(NULL)");
    CHECK(nekomote_session_set_output_size(s, 800, 600) == NEKOMOTE_ERROR_INVALID_INPUT,
          "set_output_size(NULL)");
    CHECK(nekomote_session_set_background(s, 1, 800, 600) == NEKOMOTE_ERROR_INVALID_INPUT,
          "set_background(NULL)");
    CHECK(nekomote_session_fade_background(s, 1, 800, 600, 1.0) == NEKOMOTE_ERROR_INVALID_INPUT,
          "fade_background(NULL)");
    PASS();

    TEST("null: renderer functions on NULL");
    nekomote_renderer* r = NULL;
    nekomote_renderer_capacity cap = {0};
    nekomote_render_frame frame = {0};
    nekomote_render_target target = {0, 800, 600};
    nekomote_render_view view = {0};
    view.scale = 1.0f;
    CHECK(nekomote_renderer_reserve(r, cap) == NEKOMOTE_ERROR_INVALID_INPUT, "reserve(NULL)");
    CHECK(nekomote_renderer_upload_texture(r, 1, 64, 64, (const uint8_t*)"x", 1) ==
              NEKOMOTE_ERROR_INVALID_INPUT,
          "upload(NULL)");
    CHECK(nekomote_renderer_prepare(r, frame) == NEKOMOTE_ERROR_INVALID_INPUT, "prepare(NULL)");
    CHECK(nekomote_renderer_resize_output(r, 800, 600) == NEKOMOTE_ERROR_INVALID_INPUT,
          "resize(NULL)");
    CHECK(nekomote_renderer_render(r, target, view, NEKOMOTE_TARGET_CLEAR) ==
              NEKOMOTE_ERROR_INVALID_INPUT,
          "render(NULL)");
    PASS();
}

/* ---- RAII: no leak on failed init (R4) ---- */

static void test_no_leak_on_failed_init(void)
{
    TEST("raii: renderer_create with NULL loader returns NULL");
    nekomote_clear_error();
    nekomote_renderer* r = nekomote_renderer_create(NULL);
    CHECK(r == NULL, "should return NULL");
    CHECK(nekomote_last_error() == NEKOMOTE_ERROR_INVALID_INPUT, "should set error");
    PASS();

    TEST("raii: renderer_create with null_loader stub returns NULL");
    nekomote_clear_error();
    r = nekomote_renderer_create(null_loader);
    /* Renderer constructor will fail because GL procs return NULL.
     * This tests R4: the wrapper must not leak when inner construction
     * throws. Under ASan/LSan, any leak would be reported. */
    CHECK(r == NULL, "should return NULL when GL unavailable");
    CHECK(nekomote_last_error() != NEKOMOTE_OK, "should set error");
    /* Error should be RENDERER or FAULTED, not INVALID_INPUT */
    CHECK(nekomote_last_error() == NEKOMOTE_ERROR_RENDERER ||
              nekomote_last_error() == NEKOMOTE_ERROR_FAULTED ||
              nekomote_last_error() == NEKOMOTE_ERROR_OUT_OF_MEMORY,
          "should be renderer/faulted error");
    PASS();

    TEST("raii: no leak after repeated failed creates");
    for (int i = 0; i < 100; i++)
    {
        nekomote_renderer* ri = nekomote_renderer_create(null_loader);
        CHECK(ri == NULL, "failed construction unexpectedly returned a handle");
    }
    PASS();
}

/* ---- Vulkan ABI negative tests ---- */

static void test_vk_null_safety(void)
{
    TEST("vk: create_vk with null instance");
    nekomote_clear_error();
    nekomote_vk_device_context ctx = {0};
    nekomote_wallpaper* wp = nekomote_wallpaper_create_vk(ctx);
    CHECK(wp == NULL, "should reject all-null context");
    CHECK(nekomote_last_error() == NEKOMOTE_ERROR_INVALID_INPUT, "should set INVALID_INPUT");
    PASS();

    TEST("vk: create_vk with partial null (missing device)");
    nekomote_clear_error();
    ctx.instance = (void*)0x1;
    ctx.physical_device = (void*)0x2;
    ctx.device = NULL;
    ctx.graphics_queue = (void*)0x4;
    wp = nekomote_wallpaper_create_vk(ctx);
    CHECK(wp == NULL, "should reject partial null");
    CHECK(nekomote_last_error() == NEKOMOTE_ERROR_INVALID_INPUT, "should set INVALID_INPUT");
    PASS();

    TEST("vk: set_vk_target on NULL wallpaper");
    nekomote_clear_error();
    nekomote_vk_target target = {0};
    CHECK(nekomote_wallpaper_set_vk_target(NULL, target) == NEKOMOTE_ERROR_INVALID_INPUT,
          "target setter must return INVALID_INPUT");
    CHECK(nekomote_last_error() == NEKOMOTE_ERROR_INVALID_INPUT, "should reject NULL wallpaper");
    PASS();

    TEST("vk: create_vk with fake handles does not crash");
    nekomote_clear_error();
    nekomote_vk_device_context ctx2 = {0};
    ctx2.instance = (void*)0x1;
    ctx2.physical_device = (void*)0x2;
    ctx2.device = (void*)0x3;
    ctx2.graphics_queue = (void*)0x4;
    ctx2.graphics_queue_family = 0;
    ctx2.color_format = 37;
    wp = nekomote_wallpaper_create_vk(ctx2);
    /* create_vk only validates NULL members, not handle validity.
     * Fake handles may produce a wallpaper that fails on first GPU
     * operation. The key invariant: no crash here. Clean up if live. */
    if (wp)
        nekomote_wallpaper_destroy(wp);
    PASS();
}

/* ---- Version ---- */

static void test_version(void)
{
    TEST("version");
    const char* v = nekomote_version();
    CHECK(v != NULL, "version is null");
    CHECK(strcmp(v, "0.6.0") == 0, "version mismatch");
    PASS();
}

int main(void)
{
    printf("C ABI negative tests (no assets, no GPU)\n\n");

    test_version();
    test_error_diagnostic();
    test_null_safety();
    test_no_leak_on_failed_init();
    test_vk_null_safety();

    printf("\n%d tests run, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
