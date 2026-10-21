#include <assert.h>
#include <errno.h>
#include <nekomote/abi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define TEST(name)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        g_tests_run++;                                                                             \
        printf("  %s ... ", name);                                                                 \
        fflush(stdout);                                                                            \
    } while (0)

#define PASS() printf("ok\n")
#define FAIL(msg)                                                                                  \
    do                                                                                             \
    {                                                                                              \
        printf("FAIL: %s\n", msg);                                                                 \
        g_tests_failed++;                                                                          \
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

static const char* g_model_path;
static const char* g_session_path;

static void test_version(void)
{
    TEST("version");
    const char* v = nekomote_version();
    CHECK(v != NULL, "version is null");
    CHECK(strcmp(v, "0.7.0") == 0, "version mismatch");
    PASS();
}

static void test_null_safety(void)
{
    TEST("null safety");
    CHECK(nekomote_model_load(NULL) == NULL, "model_load(NULL) should fail");
    CHECK(nekomote_session_asset_load(NULL) == NULL, "session_asset_load(NULL) should fail");
    CHECK(nekomote_program_compile(NULL, NULL) == NULL, "program_compile(NULL,NULL) should fail");
    CHECK(nekomote_session_create(NULL, (nekomote_session_config){0}) == NULL,
          "session_create(NULL) should fail");
    CHECK(nekomote_renderer_create(NULL) == NULL, "renderer_create(NULL) should fail");
    CHECK(nekomote_image_load_png(NULL) == NULL, "image_load_png(NULL) should fail");

    nekomote_model_destroy(NULL);
    nekomote_session_asset_destroy(NULL);
    nekomote_program_destroy(NULL);
    nekomote_session_destroy(NULL);
    nekomote_renderer_destroy(NULL);
    nekomote_image_destroy(NULL);
    PASS();
}

static void test_full_pipeline(void)
{
    TEST("load model");
    nekomote_model* model = nekomote_model_load(g_model_path);
    CHECK(model != NULL, "failed to load model");
    PASS();
    if (!model)
        return;

    TEST("load session asset");
    nekomote_session_asset* session = nekomote_session_asset_load(g_session_path);
    CHECK(session != NULL, "failed to load session asset");
    PASS();
    if (!session)
    {
        nekomote_model_destroy(model);
        return;
    }

    TEST("compile program");
    nekomote_program* program = nekomote_program_compile(model, session);
    CHECK(program != NULL, "failed to compile program");
    PASS();
    if (!program)
    {
        nekomote_model_destroy(model);
        nekomote_session_asset_destroy(session);
        return;
    }

    TEST("texture count > 0");
    size_t tex_count = nekomote_program_texture_count(program);
    CHECK(tex_count > 0, "no textures in program");
    PASS();

    TEST("texture info[0]");
    nekomote_texture_info tex = nekomote_program_texture(program, 0);
    CHECK(tex.id != 0, "texture id is 0");
    CHECK(tex.path != NULL, "texture path is null");
    PASS();

    TEST("texture info out of bounds");
    nekomote_texture_info bad = nekomote_program_texture(program, tex_count + 100);
    CHECK(bad.id == 0, "out-of-bounds texture should return id=0");
    PASS();

    TEST("gaze/touch/mirror queries");
    (void)nekomote_program_gaze_available(program);
    (void)nekomote_program_touch_available(program);
    (void)nekomote_program_mirror(program);
    PASS();

    TEST("find reaction");
    uint32_t react_id = nekomote_program_find_reaction(program, "b01");
    (void)react_id;
    PASS();

    TEST("create session");
    nekomote_session_config config = {0};
    config.random_seed = 42;
    config.pointer = NEKOMOTE_POINTER_LOCAL_SURFACE;
    config.audio = NEKOMOTE_AUDIO_DISABLED;
    nekomote_session* sess = nekomote_session_create(program, config);
    CHECK(sess != NULL, "failed to create session");
    PASS();
    if (!sess)
        goto cleanup_program;

    TEST("start");
    CHECK(nekomote_session_start(sess) == NEKOMOTE_OK, "start failed");
    PASS();

    TEST("set_origin");
    CHECK(nekomote_session_set_origin(sess, 0.0f, 0.0f) == NEKOMOTE_OK, "set_origin failed");
    PASS();

    TEST("set_scale");
    CHECK(nekomote_session_set_scale(sess, 1.0f) == NEKOMOTE_OK, "set_scale failed");
    PASS();

    TEST("advance 16ms");
    CHECK(nekomote_session_advance(sess, 0.016) == NEKOMOTE_OK, "advance failed");
    PASS();

    TEST("prepare_frame");
    CHECK(nekomote_session_prepare_frame(sess) == NEKOMOTE_OK, "prepare_frame failed");
    PASS();

    TEST("frame has data");
    nekomote_render_frame frame = nekomote_session_frame(sess);
    CHECK(frame.vertex_count > 0, "frame has no vertices");
    CHECK(frame.index_count > 0, "frame has no indices");
    CHECK(frame.command_count > 0, "frame has no commands");
    CHECK(frame.vertices != NULL, "frame vertices is null");
    CHECK(frame.indices != NULL, "frame indices is null");
    CHECK(frame.commands != NULL, "frame commands is null");
    PASS();

    TEST("advance 10 frames");
    int ok = 1;
    for (int i = 0; i < 10; i++)
    {
        if (nekomote_session_advance(sess, 0.016) != NEKOMOTE_OK)
        {
            ok = 0;
            break;
        }
        if (nekomote_session_prepare_frame(sess) != NEKOMOTE_OK)
        {
            ok = 0;
            break;
        }
    }
    CHECK(ok, "advance/prepare_frame loop failed");
    PASS();

    TEST("pointer_move");
    CHECK(nekomote_session_pointer_move(sess, 100.0f, 200.0f) == NEKOMOTE_OK,
          "pointer_move failed");
    PASS();

    TEST("pointer_leave");
    CHECK(nekomote_session_pointer_leave(sess) == NEKOMOTE_OK, "pointer_leave failed");
    PASS();

    TEST("touch");
    CHECK(nekomote_session_touch(sess, 50.0f, 50.0f) == NEKOMOTE_OK, "touch failed");
    PASS();

    TEST("reset");
    CHECK(nekomote_session_reset(sess) == NEKOMOTE_OK, "reset failed");
    PASS();

    TEST("reaction_phase");
    nekomote_reaction_phase phase = nekomote_session_reaction_phase(sess);
    (void)phase;
    PASS();

    TEST("active_reaction");
    (void)nekomote_session_active_reaction(sess);
    PASS();

    TEST("background: set_output_size");
    CHECK(nekomote_session_set_output_size(sess, 800, 600) == NEKOMOTE_OK,
          "set_output_size failed");
    PASS();

    TEST("background: clear");
    nekomote_session_clear_background(sess);
    PASS();

    TEST("background_frame empty");
    nekomote_render_frame bg = nekomote_session_background_frame(sess);
    CHECK(bg.vertex_count == 0, "background frame should be empty");
    PASS();

    TEST("set_playback_state paused");
    CHECK(nekomote_session_set_playback_state(sess, NEKOMOTE_PLAYBACK_PAUSED) == NEKOMOTE_OK,
          "pause failed");
    PASS();

    TEST("set_playback_state running");
    CHECK(nekomote_session_set_playback_state(sess, NEKOMOTE_PLAYBACK_RUNNING) == NEKOMOTE_OK,
          "resume failed");
    PASS();

    TEST("set_audio_level");
    CHECK(nekomote_session_set_audio_level(sess, 0.5f) == NEKOMOTE_OK, "set_audio_level failed");
    PASS();

    nekomote_session_destroy(sess);

cleanup_program:
    nekomote_program_destroy(program);
    nekomote_session_asset_destroy(session);
    nekomote_model_destroy(model);
}

static void test_frame_data_layout(void)
{
    TEST("frame data layout");
    nekomote_model* model = nekomote_model_load(g_model_path);
    if (!model)
    {
        FAIL("model load failed");
        return;
    }
    nekomote_session_asset* session = nekomote_session_asset_load(g_session_path);
    if (!session)
    {
        FAIL("session load failed");
        nekomote_model_destroy(model);
        return;
    }
    nekomote_program* program = nekomote_program_compile(model, session);
    if (!program)
    {
        FAIL("compile failed");
        goto cleanup;
    }

    nekomote_session_config config = {0};
    nekomote_session* sess = nekomote_session_create(program, config);
    if (!sess)
    {
        FAIL("session create failed");
        goto cleanup_prog;
    }
    nekomote_session_start(sess);
    nekomote_session_advance(sess, 0.016);
    nekomote_session_prepare_frame(sess);
    nekomote_render_frame frame = nekomote_session_frame(sess);

    if (frame.vertex_count > 0)
    {
        const nekomote_vertex* v = &frame.vertices[0];
        (void)v->x;
        (void)v->y;
        (void)v->u;
        (void)v->v;
        (void)v->color[0];
        (void)v->color[1];
        (void)v->color[2];
        (void)v->color[3];
    }

    if (frame.command_count > 0)
    {
        const nekomote_draw_command* cmd = &frame.commands[0];
        (void)cmd->texture;
        (void)cmd->first_index;
        (void)cmd->index_count;
        (void)cmd->blend;
        (void)cmd->protection;
        (void)cmd->mask;
    }

    PASS();
    nekomote_session_destroy(sess);
cleanup_prog:
    nekomote_program_destroy(program);
cleanup:
    nekomote_session_asset_destroy(session);
    nekomote_model_destroy(model);
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <model.nkm> <session.nks.json>\n", argv[0]);
        return 2;
    }
    g_model_path = argv[1];
    g_session_path = argv[2];

    const char* paths[2] = {g_model_path, g_session_path};
    for (int i = 0; i < 2; ++i)
    {
        FILE* input = fopen(paths[i], "rb");
        if (!input)
        {
            if (errno == ENOENT)
            {
                fprintf(stderr, "SKIP: optional integration asset missing: %s\n", paths[i]);
                return 77;
            }
            return 1;
        }
        fclose(input);
    }

    printf("C ABI unit tests\n");
    printf("  model: %s\n", g_model_path);
    printf("  session: %s\n\n", g_session_path);

    test_version();
    test_null_safety();
    test_full_pipeline();
    test_frame_data_layout();

    printf("\n%d tests run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
