/**
 * @file abi.h
 * @brief C ABI for NekoMote session engine and render backend.
 * @version 0.3.0
 * @since P8
 *
 * Design rules (AGENTS.md §4):
 * - No STL, exceptions, or C++ object layout cross this boundary.
 * - Opaque handles own memory; destroy functions release it.
 * - All functions that can fail return nekomote_error; output is via
 *   out-parameters. Functions returning handles return NULL on failure.
 * - On failure, call nekomote_last_error() / nekomote_error_message()
 *   to retrieve the diagnostic. Error state is per-thread.
 * - RenderFrame pointers returned by nekomote_session_frame and
 *   nekomote_session_background_frame are non-owning views into storage
 *   owned by the session. They are valid until the next call that
 *   mutates the session (advance, prepare_frame, reset, set_origin,
 *   set_scale, set_output_size, set_background, fade_background,
 *   clear_background, react, touch, pointer_*, set_audio_level,
 *   set_playback_state, start). The renderer is not affected.
 * - Strings returned by nekomote_program_texture, audio_path,
 *   reaction_label are non-owning views into storage owned by the
 *   program. They are valid until nekomote_program_destroy.
 * - nekomote_image_pixels is valid until nekomote_image_destroy.
 * - Each CPU session is single-threaded: input, advance, frame preparation
 *   and view queries must not run concurrently. Immutable programs may be
 *   shared by independently owned sessions.
 * - The caller must not call renderer functions on a handle from a
 *   different graphics thread than the one that created it.
 * - The program handle must outlive all session instances created from
 *   it. Destroying a program while sessions reference it is undefined.
 * - Enum arguments outside their declared range are rejected with
 *   NEKOMOTE_ERROR_INVALID_INPUT; they are not silently clamped.
 */

#ifndef NEKOMOTE_ABI_H
#define NEKOMOTE_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ------------------------------------------------------------------ */
    /* Error codes                                                        */
    /* ------------------------------------------------------------------ */

    /* ABI codes and modes have an explicit width. Constants below define the
     * accepted values; the int32_t boundary can safely reject any other value. */
    typedef int32_t nekomote_error;
    enum
    {
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
        NEKOMOTE_ERROR_RENDERER,
    };

    /* Per-thread error diagnostic. nekomote_last_error returns the most
     * recent error code set by the calling thread; NEKOMOTE_OK if no error
     * was recorded since the last clear. nekomote_error_message returns a
     * pointer to a NUL-terminated UTF-8 string (may be NULL) describing
     * the last error, valid until the next ABI call on the same thread.
     * nekomote_clear_error resets the per-thread state. */
    nekomote_error nekomote_last_error(void);
    const char* nekomote_error_message(void);
    void nekomote_clear_error(void);

    /* ------------------------------------------------------------------ */
    /* Common types                                                       */
    /* ------------------------------------------------------------------ */

    typedef struct
    {
        float x, y;
    } nekomote_vec2;

    typedef int32_t nekomote_pointer_scope;
    enum
    {
        NEKOMOTE_POINTER_UNAVAILABLE = 0,
        NEKOMOTE_POINTER_LOCAL_SURFACE,
    };

    typedef int32_t nekomote_audio_mode;
    enum
    {
        NEKOMOTE_AUDIO_DISABLED = 0,
        NEKOMOTE_AUDIO_DEVICE,
    };

    typedef int32_t nekomote_playback_state;
    enum
    {
        NEKOMOTE_PLAYBACK_RUNNING = 0,
        NEKOMOTE_PLAYBACK_PAUSED,
    };

    typedef int32_t nekomote_reaction_phase;
    enum
    {
        NEKOMOTE_REACTION_IDLE = 0,
        NEKOMOTE_REACTION_PLAYING,
        NEKOMOTE_REACTION_RECOVERING,
    };

    typedef int32_t nekomote_audio_command_kind;
    enum
    {
        NEKOMOTE_AUDIO_PLAY = 0,
        NEKOMOTE_AUDIO_STOP,
        NEKOMOTE_AUDIO_PAUSE,
        NEKOMOTE_AUDIO_RESUME,
    };

    typedef struct
    {
        nekomote_audio_command_kind kind;
        uint32_t clip_id;
    } nekomote_audio_command;

    typedef struct
    {
        uint32_t random_seed;
        nekomote_pointer_scope pointer;
        nekomote_audio_mode audio;
    } nekomote_session_config;

    /* ------------------------------------------------------------------ */
    /* Render protocol (C view of RenderFrame)                             */
    /* ------------------------------------------------------------------ */

    typedef struct
    {
        float x, y, u, v;
        float color[4];
    } nekomote_vertex;

    typedef int32_t nekomote_blend_mode;
    enum
    {
        NEKOMOTE_BLEND_OVER = 0,
        NEKOMOTE_BLEND_ADD,
        NEKOMOTE_BLEND_MULTIPLY,
        NEKOMOTE_BLEND_SCREEN,
    };

    typedef int32_t nekomote_color_protection;
    enum
    {
        NEKOMOTE_PROTECTION_NONE = 0,
        NEKOMOTE_PROTECTION_PRESERVE_TRANSLUCENT,
    };

    typedef int32_t nekomote_color_composition;
    enum
    {
        NEKOMOTE_COMPOSITION_PREMULTIPLIED_OVER = 0,
        NEKOMOTE_COMPOSITION_PROTECTED_STRAIGHT,
    };

    typedef struct
    {
        uint32_t texture;
        uint32_t first_index;
        uint32_t index_count;
        nekomote_blend_mode blend;
        nekomote_color_protection protection;
        uint32_t mask;
    } nekomote_draw_command;

    typedef struct
    {
        uint32_t first_command;
        uint32_t command_count;
    } nekomote_mask_definition;

    typedef struct
    {
        const nekomote_vertex* vertices;
        size_t vertex_count;
        const uint32_t* indices;
        size_t index_count;
        const nekomote_draw_command* commands;
        size_t command_count;
        nekomote_color_composition composition;
        const nekomote_draw_command* mask_commands;
        size_t mask_command_count;
        const nekomote_mask_definition* masks;
        size_t mask_count;
    } nekomote_render_frame;

    /* ------------------------------------------------------------------ */
    /* Texture info (from compiled program scene)                         */
    /* ------------------------------------------------------------------ */

    typedef struct
    {
        uint32_t id;
        const char* name;
        const char* path;
        int width;
        int height;
    } nekomote_texture_info;

    /* ------------------------------------------------------------------ */
    /* Asset loading                                                      */
    /* ------------------------------------------------------------------ */

    typedef struct nekomote_model nekomote_model;
    typedef struct nekomote_session_asset nekomote_session_asset;

    nekomote_model* nekomote_model_load(const char* path);
    void nekomote_model_destroy(nekomote_model* model);

    nekomote_session_asset* nekomote_session_asset_load(const char* path);
    void nekomote_session_asset_destroy(nekomote_session_asset* asset);

    /* ------------------------------------------------------------------ */
    /* Session program (immutable, compiled)                              */
    /* ------------------------------------------------------------------ */

    typedef struct nekomote_program nekomote_program;

    nekomote_program* nekomote_program_compile(const nekomote_model* model,
                                               const nekomote_session_asset* session);
    void nekomote_program_destroy(nekomote_program* program);

    size_t nekomote_program_texture_count(const nekomote_program* program);
    nekomote_texture_info nekomote_program_texture(const nekomote_program* program, size_t index);

    size_t nekomote_program_audio_count(const nekomote_program* program);
    const char* nekomote_program_audio_path(const nekomote_program* program, size_t index);

    uint32_t nekomote_program_find_reaction(const nekomote_program* program, const char* label);
    const char* nekomote_program_reaction_label(const nekomote_program* program,
                                                uint32_t reaction_id);

    int nekomote_program_gaze_available(const nekomote_program* program);
    int nekomote_program_touch_available(const nekomote_program* program);
    int nekomote_program_mirror(const nekomote_program* program);

    /* ------------------------------------------------------------------ */
    /* Session instance (mutable, per-screen)                             */
    /* ------------------------------------------------------------------ */

    typedef struct nekomote_session nekomote_session;

    nekomote_session* nekomote_session_create(const nekomote_program* program,
                                              nekomote_session_config config);
    void nekomote_session_destroy(nekomote_session* session);

    nekomote_error nekomote_session_start(nekomote_session* session);
    nekomote_error nekomote_session_set_origin(nekomote_session* session, float x, float y);
    nekomote_error nekomote_session_set_scale(nekomote_session* session, float scale);
    /* Input XY is centered output pixels, Y down, in presented direction.
     * Hosts convert logical/window pixels to framebuffer pixels, but must not
     * mirror pointer or touch coordinates. Session owns authored-region mapping. */
    nekomote_error nekomote_session_pointer_move(nekomote_session* session, float x, float y);
    nekomote_error nekomote_session_pointer_leave(nekomote_session* session);
    nekomote_error nekomote_session_pointer_enter(nekomote_session* session, float x, float y);
    nekomote_error nekomote_session_touch(nekomote_session* session, float x, float y);
    nekomote_error nekomote_session_react(nekomote_session* session, uint32_t reaction_id);
    nekomote_error nekomote_session_set_audio_level(nekomote_session* session, float rms);
    int nekomote_session_pop_audio_command(nekomote_session* session, nekomote_audio_command* out);
    nekomote_error nekomote_session_set_playback_state(nekomote_session* session,
                                                       nekomote_playback_state state);
    nekomote_error nekomote_session_advance(nekomote_session* session, double elapsed_seconds);
    nekomote_error nekomote_session_prepare_frame(nekomote_session* session);
    nekomote_error nekomote_session_reset(nekomote_session* session);

    nekomote_render_frame nekomote_session_frame(const nekomote_session* session);
    nekomote_reaction_phase nekomote_session_reaction_phase(const nekomote_session* session);
    uint32_t nekomote_session_active_reaction(const nekomote_session* session);

    /* Background layer */
    nekomote_error nekomote_session_set_output_size(nekomote_session* session, int width,
                                                    int height);
    nekomote_error nekomote_session_set_background(nekomote_session* session, uint32_t texture_id,
                                                   int width, int height);
    nekomote_error nekomote_session_fade_background(nekomote_session* session, uint32_t texture_id,
                                                    int width, int height, double duration_seconds);
    void nekomote_session_clear_background(nekomote_session* session);
    nekomote_render_frame nekomote_session_background_frame(const nekomote_session* session);

    /* ------------------------------------------------------------------ */
    /* Renderer (OpenGL backend, C wrapper)                               */
    /* ------------------------------------------------------------------ */

    typedef struct nekomote_renderer nekomote_renderer;

    typedef void (*nekomote_gl_proc)(void);
    typedef nekomote_gl_proc (*nekomote_gl_loader)(const char*);

    typedef struct
    {
        uint32_t framebuffer;
        int width;
        int height;
    } nekomote_render_target;

    typedef struct
    {
        float origin_x, origin_y;
        float scale;
        float clear[4];
        float alpha;
        int flip_x;
    } nekomote_render_view;

    typedef int32_t nekomote_target_contents;
    enum
    {
        NEKOMOTE_TARGET_CLEAR = 0,
        NEKOMOTE_TARGET_PRESERVE,
    };

    typedef struct
    {
        size_t vertices, indices, commands, textures;
        size_t mask_commands, masks;
    } nekomote_renderer_capacity;

    /* Read-only renderer capacity derived from the compiled scene and texture
     * count. Returns all-zero and sets NEKOMOTE_ERROR_INVALID_INPUT if program
     * is NULL. The caller may pass this to nekomote_renderer_reserve instead of
     * hard-coding capacities. */
    nekomote_renderer_capacity nekomote_program_renderer_capacity(const nekomote_program* program);

    nekomote_renderer* nekomote_renderer_create(nekomote_gl_loader loader);
    void nekomote_renderer_destroy(nekomote_renderer* renderer);

    nekomote_error nekomote_renderer_reserve(nekomote_renderer* renderer,
                                             nekomote_renderer_capacity capacity);
    /* Packed RGBA8: pixel_bytes must equal width * height * 4. */
    nekomote_error nekomote_renderer_upload_texture(nekomote_renderer* renderer, uint32_t id,
                                                    int width, int height, const uint8_t* pixels,
                                                    size_t pixel_bytes);
    /* prepare failure invalidates the frame. render then fails without drawing
     * until a later successful prepare; previously prepared pixels are not reused. */
    nekomote_error nekomote_renderer_prepare(nekomote_renderer* renderer,
                                             nekomote_render_frame frame);
    nekomote_error nekomote_renderer_resize_output(nekomote_renderer* renderer, int width,
                                                   int height);
    nekomote_error nekomote_renderer_render(nekomote_renderer* renderer,
                                            nekomote_render_target target,
                                            nekomote_render_view view,
                                            nekomote_target_contents contents);

    /* ------------------------------------------------------------------ */
    /* Image loading (PNG)                                                */
    /* ------------------------------------------------------------------ */

    typedef struct nekomote_image nekomote_image;

    nekomote_image* nekomote_image_load_png(const char* path);
    void nekomote_image_destroy(nekomote_image* image);

    /* Access image data (valid until nekomote_image_destroy) */
    int nekomote_image_width(const nekomote_image* image);
    int nekomote_image_height(const nekomote_image* image);
    size_t nekomote_image_byte_count(const nekomote_image* image);
    const uint8_t* nekomote_image_pixels(const nekomote_image* image);

    /* ------------------------------------------------------------------ */
    /* Version                                                            */
    /* ------------------------------------------------------------------ */

    const char* nekomote_version(void);

    /* ------------------------------------------------------------------ */
    /* Wallpaper instance (high-level: Controller + UI + GL renderers)     */
    /* ------------------------------------------------------------------ */

    typedef struct nekomote_wallpaper nekomote_wallpaper;

    typedef int32_t nekomote_key;
    enum
    {
        NEKOMOTE_KEY_ESCAPE = 1,
        NEKOMOTE_KEY_SPACE,
        NEKOMOTE_KEY_TAB,
        NEKOMOTE_KEY_F,
        NEKOMOTE_KEY_R,
        NEKOMOTE_KEY_B,
        NEKOMOTE_KEY_N,
        NEKOMOTE_KEY_C,
        NEKOMOTE_KEY_DIGIT_1,
        NEKOMOTE_KEY_DIGIT_2,
        NEKOMOTE_KEY_DIGIT_3,
        NEKOMOTE_KEY_DIGIT_4,
        NEKOMOTE_KEY_DIGIT_5,
        NEKOMOTE_KEY_DIGIT_6,
        NEKOMOTE_KEY_DIGIT_7,
        NEKOMOTE_KEY_DIGIT_8,
        NEKOMOTE_KEY_DIGIT_9,
    };

    typedef int32_t nekomote_mouse_action;
    enum
    {
        NEKOMOTE_MOUSE_RELEASE = 0,
        NEKOMOTE_MOUSE_PRESS,
    };

    /* Create/destroy. loader is the GL function pointer loader (e.g.
     * glfwGetProcAddress). The instance owns GPU renderers; all methods
     * and destruction require a current GL context on the graphics thread. */
    nekomote_wallpaper* nekomote_wallpaper_create(nekomote_gl_loader loader);
    void nekomote_wallpaper_destroy(nekomote_wallpaper* wp);

    /* Setup */
    nekomote_error nekomote_wallpaper_set_assets_dir(nekomote_wallpaper* wp,
                                                     const char* dir);
    nekomote_error nekomote_wallpaper_set_no_save(nekomote_wallpaper* wp, int no_save);
    nekomote_error nekomote_wallpaper_set_muted(nekomote_wallpaper* wp, int muted);
    nekomote_error nekomote_wallpaper_set_seed(nekomote_wallpaper* wp, uint32_t seed);
    nekomote_error nekomote_wallpaper_set_output_size(nekomote_wallpaper* wp, int width,
                                                      int height);
    nekomote_error nekomote_wallpaper_set_paused(nekomote_wallpaper* wp, int paused);
    nekomote_error nekomote_wallpaper_load_initial(nekomote_wallpaper* wp);
    nekomote_error nekomote_wallpaper_load_model(nekomote_wallpaper* wp, const char* model_path,
                                                 const char* session_path);

    /* Per-frame: advance handles candidate/background polling and fade.
     * render_frame draws background + character (with crossfade) to the
     * current GL framebuffer. render_ui draws the control panel.
     * Host swaps buffers after render_frame + render_ui. */
    nekomote_error nekomote_wallpaper_advance(nekomote_wallpaper* wp, double dt);
    nekomote_error nekomote_wallpaper_render_frame(nekomote_wallpaper* wp);
    nekomote_error nekomote_wallpaper_render_ui(nekomote_wallpaper* wp, float dt);

    /* Input. Coordinates are logical window pixels; the instance converts
     * to centered framebuffer pixels using output_size. */
    nekomote_error nekomote_wallpaper_pointer_move(nekomote_wallpaper* wp, double x, double y);
    nekomote_error nekomote_wallpaper_pointer_enter(nekomote_wallpaper* wp, double x, double y);
    nekomote_error nekomote_wallpaper_pointer_leave(nekomote_wallpaper* wp);
    nekomote_error nekomote_wallpaper_touch(nekomote_wallpaper* wp, float x, float y);
    nekomote_error nekomote_wallpaper_mouse_button(nekomote_wallpaper* wp, int button,
                                                    nekomote_mouse_action action);
    nekomote_error nekomote_wallpaper_mouse_scroll(nekomote_wallpaper* wp, double amount);
    nekomote_error nekomote_wallpaper_key(nekomote_wallpaper* wp, nekomote_key key,
                                          nekomote_mouse_action action);

    /* Actions */
    nekomote_error nekomote_wallpaper_switch_character(nekomote_wallpaper* wp, int index);
    nekomote_error nekomote_wallpaper_switch_background(nekomote_wallpaper* wp, int index);
    nekomote_error nekomote_wallpaper_clear_background(nekomote_wallpaper* wp);
    nekomote_error nekomote_wallpaper_fit(nekomote_wallpaper* wp);
    nekomote_error nekomote_wallpaper_reset(nekomote_wallpaper* wp);

    /* Catalog queries. Strings are valid until the next call on the same
     * wallpaper instance. */
    int nekomote_wallpaper_character_count(const nekomote_wallpaper* wp);
    const char* nekomote_wallpaper_character_name(const nekomote_wallpaper* wp, int index);
    int nekomote_wallpaper_background_count(const nekomote_wallpaper* wp);
    const char* nekomote_wallpaper_background_name(const nekomote_wallpaper* wp, int index);
    int nekomote_wallpaper_current_character(const nekomote_wallpaper* wp);
    int nekomote_wallpaper_current_background(const nekomote_wallpaper* wp);
    const char* nekomote_wallpaper_current_character_name(const nekomote_wallpaper* wp);
    int nekomote_wallpaper_has_background(const nekomote_wallpaper* wp);
    int nekomote_wallpaper_paused(const nekomote_wallpaper* wp);

#ifdef __cplusplus
}
#endif

#endif /* NEKOMOTE_ABI_H */
