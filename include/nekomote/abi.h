/**
 * @file abi.h
 * @brief C ABI for NekoMote session engine and render backend.
 * @version 0.1.0
 * @since P8
 *
 * Design rules (AGENTS.md §4):
 * - No STL, exceptions, or C++ object layout cross this boundary.
 * - Opaque handles own memory; destroy functions release it.
 * - RenderFrame pointers are non-owning views, valid until the next
 *   advance/prepare_frame/mutation call on the owning session.
 * - All functions that can fail return nekomote_error; output is via
 *   out-parameters. Functions returning handles return NULL on failure.
 * - The caller must not call functions on a handle from a different
 *   graphics thread than the one that created it (renderer only).
 */

#ifndef NEKOMOTE_ABI_H
#define NEKOMOTE_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Error codes                                                        */
/* ------------------------------------------------------------------ */

typedef enum {
    NEKOMOTE_OK = 0,
    NEKOMOTE_ERROR_INVALID_INPUT,
    NEKOMOTE_ERROR_INVALID_REACTION,
    NEKOMOTE_ERROR_BUSY,
    NEKOMOTE_ERROR_INPUT_UNAVAILABLE,
    NEKOMOTE_ERROR_EVENT_QUEUE_FULL,
    NEKOMOTE_ERROR_PLAYBACK,
    NEKOMOTE_ERROR_FAULTED,
    NEKOMOTE_ERROR_OUT_OF_MEMORY,
    NEKOMOTE_ERROR_NOT_FOUND,
    NEKOMOTE_ERROR_UNSUPPORTED,
    NEKOMOTE_ERROR_IO,
} nekomote_error;

/* ------------------------------------------------------------------ */
/* Common types                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    float x, y;
} nekomote_vec2;

typedef enum {
    NEKOMOTE_POINTER_UNAVAILABLE = 0,
    NEKOMOTE_POINTER_LOCAL_SURFACE,
} nekomote_pointer_scope;

typedef enum {
    NEKOMOTE_AUDIO_DISABLED = 0,
    NEKOMOTE_AUDIO_DEVICE,
} nekomote_audio_mode;

typedef enum {
    NEKOMOTE_PLAYBACK_RUNNING = 0,
    NEKOMOTE_PLAYBACK_PAUSED,
} nekomote_playback_state;

typedef enum {
    NEKOMOTE_REACTION_IDLE = 0,
    NEKOMOTE_REACTION_PLAYING,
    NEKOMOTE_REACTION_RECOVERING,
} nekomote_reaction_phase;

typedef struct {
    uint32_t random_seed;
    nekomote_pointer_scope pointer;
    nekomote_audio_mode audio;
} nekomote_session_config;

/* ------------------------------------------------------------------ */
/* Render protocol (C view of RenderFrame)                             */
/* ------------------------------------------------------------------ */

typedef struct {
    float x, y, u, v;
    float color[4];
} nekomote_vertex;

typedef enum {
    NEKOMOTE_BLEND_OVER = 0,
    NEKOMOTE_BLEND_ADD,
    NEKOMOTE_BLEND_MULTIPLY,
    NEKOMOTE_BLEND_SCREEN,
} nekomote_blend_mode;

typedef enum {
    NEKOMOTE_PROTECTION_NONE = 0,
    NEKOMOTE_PROTECTION_PRESERVE_TRANSLUCENT,
} nekomote_color_protection;

typedef enum {
    NEKOMOTE_COMPOSITION_PREMULTIPLIED_OVER = 0,
    NEKOMOTE_COMPOSITION_PROTECTED_STRAIGHT,
} nekomote_color_composition;

typedef struct {
    uint32_t texture;
    uint32_t first_index;
    uint32_t index_count;
    nekomote_blend_mode blend;
    nekomote_color_protection protection;
    uint32_t mask;
} nekomote_draw_command;

typedef struct {
    uint32_t first_command;
    uint32_t command_count;
} nekomote_mask_definition;

typedef struct {
    const nekomote_vertex *vertices;
    size_t vertex_count;
    const uint32_t *indices;
    size_t index_count;
    const nekomote_draw_command *commands;
    size_t command_count;
    nekomote_color_composition composition;
    const nekomote_draw_command *mask_commands;
    size_t mask_command_count;
    const nekomote_mask_definition *masks;
    size_t mask_count;
} nekomote_render_frame;

/* ------------------------------------------------------------------ */
/* Texture info (from compiled program scene)                         */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t id;
    const char *name;
    const char *path;
    int width;
    int height;
} nekomote_texture_info;

/* ------------------------------------------------------------------ */
/* Asset loading                                                      */
/* ------------------------------------------------------------------ */

typedef struct nekomote_model nekomote_model;
typedef struct nekomote_session_asset nekomote_session_asset;

nekomote_model *nekomote_model_load(const char *path);
void nekomote_model_destroy(nekomote_model *model);

nekomote_session_asset *nekomote_session_asset_load(const char *path);
void nekomote_session_asset_destroy(nekomote_session_asset *asset);

/* ------------------------------------------------------------------ */
/* Session program (immutable, compiled)                              */
/* ------------------------------------------------------------------ */

typedef struct nekomote_program nekomote_program;

nekomote_program *nekomote_program_compile(
    const nekomote_model *model,
    const nekomote_session_asset *session);
void nekomote_program_destroy(nekomote_program *program);

size_t nekomote_program_texture_count(const nekomote_program *program);
nekomote_texture_info nekomote_program_texture(
    const nekomote_program *program, size_t index);

size_t nekomote_program_audio_count(const nekomote_program *program);
const char *nekomote_program_audio_path(
    const nekomote_program *program, size_t index);

uint32_t nekomote_program_find_reaction(
    const nekomote_program *program, const char *label);
const char *nekomote_program_reaction_label(
    const nekomote_program *program, uint32_t reaction_id);

int nekomote_program_gaze_available(const nekomote_program *program);
int nekomote_program_touch_available(const nekomote_program *program);
int nekomote_program_mirror(const nekomote_program *program);

/* ------------------------------------------------------------------ */
/* Session instance (mutable, per-screen)                             */
/* ------------------------------------------------------------------ */

typedef struct nekomote_session nekomote_session;

nekomote_session *nekomote_session_create(
    const nekomote_program *program,
    nekomote_session_config config);
void nekomote_session_destroy(nekomote_session *session);

nekomote_error nekomote_session_start(nekomote_session *session);
nekomote_error nekomote_session_set_origin(
    nekomote_session *session, float x, float y);
nekomote_error nekomote_session_set_scale(
    nekomote_session *session, float scale);
nekomote_error nekomote_session_pointer_move(
    nekomote_session *session, float x, float y);
nekomote_error nekomote_session_pointer_leave(nekomote_session *session);
nekomote_error nekomote_session_pointer_enter(
    nekomote_session *session, float x, float y);
nekomote_error nekomote_session_touch(
    nekomote_session *session, float x, float y);
nekomote_error nekomote_session_react(
    nekomote_session *session, uint32_t reaction_id);
nekomote_error nekomote_session_set_audio_level(
    nekomote_session *session, float rms);
nekomote_error nekomote_session_set_playback_state(
    nekomote_session *session, nekomote_playback_state state);
nekomote_error nekomote_session_advance(
    nekomote_session *session, double elapsed_seconds);
nekomote_error nekomote_session_prepare_frame(nekomote_session *session);
nekomote_error nekomote_session_reset(nekomote_session *session);

nekomote_render_frame nekomote_session_frame(const nekomote_session *session);
nekomote_reaction_phase nekomote_session_reaction_phase(
    const nekomote_session *session);
uint32_t nekomote_session_active_reaction(const nekomote_session *session);

/* Background layer */
nekomote_error nekomote_session_set_output_size(
    nekomote_session *session, int width, int height);
nekomote_error nekomote_session_set_background(
    nekomote_session *session, uint32_t texture_id, int width, int height);
nekomote_error nekomote_session_fade_background(
    nekomote_session *session, uint32_t texture_id,
    int width, int height, double duration_seconds);
void nekomote_session_clear_background(nekomote_session *session);
nekomote_render_frame nekomote_session_background_frame(
    const nekomote_session *session);

/* ------------------------------------------------------------------ */
/* Renderer (OpenGL backend, C wrapper)                               */
/* ------------------------------------------------------------------ */

typedef struct nekomote_renderer nekomote_renderer;

typedef void (*nekomote_gl_proc)(void);
typedef nekomote_gl_proc (*nekomote_gl_loader)(const char *);

typedef struct {
    uint32_t framebuffer;
    int width;
    int height;
} nekomote_render_target;

typedef struct {
    float origin_x, origin_y;
    float scale;
    float clear[4];
    float alpha;
    int flip_x;
} nekomote_render_view;

typedef enum {
    NEKOMOTE_TARGET_CLEAR = 0,
    NEKOMOTE_TARGET_PRESERVE,
} nekomote_target_contents;

typedef struct {
    size_t vertices, indices, commands, textures;
    size_t mask_commands, masks;
} nekomote_renderer_capacity;

nekomote_renderer *nekomote_renderer_create(nekomote_gl_loader loader);
void nekomote_renderer_destroy(nekomote_renderer *renderer);

void nekomote_renderer_reserve(
    nekomote_renderer *renderer, nekomote_renderer_capacity capacity);
void nekomote_renderer_upload_texture(
    nekomote_renderer *renderer, uint32_t id,
    int width, int height, const uint8_t *pixels);
void nekomote_renderer_prepare(
    nekomote_renderer *renderer, nekomote_render_frame frame);
void nekomote_renderer_resize_output(
    nekomote_renderer *renderer, int width, int height);
void nekomote_renderer_render(
    nekomote_renderer *renderer,
    nekomote_render_target target,
    nekomote_render_view view,
    nekomote_target_contents contents);

/* ------------------------------------------------------------------ */
/* Image loading (PNG)                                                */
/* ------------------------------------------------------------------ */

typedef struct nekomote_image nekomote_image;

nekomote_image *nekomote_image_load_png(const char *path);
void nekomote_image_destroy(nekomote_image *image);

/* Access image data (valid until nekomote_image_destroy) */
int nekomote_image_width(const nekomote_image *image);
int nekomote_image_height(const nekomote_image *image);
const uint8_t *nekomote_image_pixels(const nekomote_image *image);

/* ------------------------------------------------------------------ */
/* Version                                                            */
/* ------------------------------------------------------------------ */

const char *nekomote_version(void);

#ifdef __cplusplus
}
#endif

#endif /* NEKOMOTE_ABI_H */
