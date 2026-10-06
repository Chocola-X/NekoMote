#include <nekomote/abi.h>

#include <nekomote/application/session.hpp>
#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/image.hpp>
#include <nekomote/asset_io/session_asset.hpp>
#include <nekomote/render_gl/renderer.hpp>
#include <nekomote/render_frame.hpp>

#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

nekomote_error to_c_error(nekomote::application::SessionError e)
{
    switch (e)
    {
    case nekomote::application::SessionError::None:
        return NEKOMOTE_OK;
    case nekomote::application::SessionError::InvalidInput:
        return NEKOMOTE_ERROR_INVALID_INPUT;
    case nekomote::application::SessionError::InvalidReaction:
        return NEKOMOTE_ERROR_INVALID_REACTION;
    case nekomote::application::SessionError::Busy:
        return NEKOMOTE_ERROR_BUSY;
    case nekomote::application::SessionError::InputUnavailable:
        return NEKOMOTE_ERROR_INPUT_UNAVAILABLE;
    case nekomote::application::SessionError::EventQueueFull:
        return NEKOMOTE_ERROR_EVENT_QUEUE_FULL;
    case nekomote::application::SessionError::Playback:
        return NEKOMOTE_ERROR_PLAYBACK;
    case nekomote::application::SessionError::Faulted:
        return NEKOMOTE_ERROR_FAULTED;
    }
    return NEKOMOTE_ERROR_FAULTED;
}

nekomote_render_frame to_c_frame(const nekomote::RenderFrame &f)
{
    nekomote_render_frame r;
    r.vertices = reinterpret_cast<const nekomote_vertex *>(f.vertices.data());
    r.vertex_count = f.vertices.size();
    r.indices = f.indices.data();
    r.index_count = f.indices.size();
    r.commands = reinterpret_cast<const nekomote_draw_command *>(f.commands.data());
    r.command_count = f.commands.size();
    r.composition = f.composition == nekomote::ColorComposition::PremultipliedOver
                        ? NEKOMOTE_COMPOSITION_PREMULTIPLIED_OVER
                        : NEKOMOTE_COMPOSITION_PROTECTED_STRAIGHT;
    r.mask_commands = reinterpret_cast<const nekomote_draw_command *>(f.mask_commands.data());
    r.mask_command_count = f.mask_commands.size();
    r.masks = reinterpret_cast<const nekomote_mask_definition *>(f.masks.data());
    r.mask_count = f.masks.size();
    return r;
}

nekomote::application::SessionConfig from_c_config(nekomote_session_config c)
{
    nekomote::application::SessionConfig cfg;
    cfg.animation.random_seed = c.random_seed;
    cfg.reaction_seed = c.random_seed;
    cfg.pointer = c.pointer == NEKOMOTE_POINTER_LOCAL_SURFACE
                      ? nekomote::application::PointerScope::LocalSurface
                      : nekomote::application::PointerScope::Unavailable;
    cfg.audio = c.audio == NEKOMOTE_AUDIO_DEVICE
                    ? nekomote::application::AudioMode::Device
                    : nekomote::application::AudioMode::Disabled;
    return cfg;
}

nekomote::render_gl::View from_c_view(nekomote_render_view v)
{
    nekomote::render_gl::View view;
    view.origin_x = v.origin_x;
    view.origin_y = v.origin_y;
    view.scale = v.scale;
    view.clear = {v.clear[0], v.clear[1], v.clear[2], v.clear[3]};
    view.alpha = v.alpha;
    view.flip_x = v.flip_x != 0;
    return view;
}

} // namespace

struct nekomote_model
{
    nekomote::model::ModelAsset asset;
    bool ok{false};
};

struct nekomote_session_asset
{
    nekomote::model::SessionAsset asset;
    bool ok{false};
};

struct nekomote_program
{
    std::unique_ptr<const nekomote::application::SessionProgram> program;
};

struct nekomote_session
{
    std::unique_ptr<nekomote::application::SessionInstance> instance;
};

struct nekomote_renderer
{
    std::unique_ptr<nekomote::render_gl::Renderer> renderer;
};

struct nekomote_image
{
    int width{0};
    int height{0};
    std::vector<uint8_t> pixels;
};

extern "C" {

const char *nekomote_version(void)
{
    return "0.1.0";
}

/* ---- Asset loading ---- */

nekomote_model *nekomote_model_load(const char *path)
{
    if (!path)
        return nullptr;
    try
    {
        auto result = nekomote::asset_io::load_asset(std::filesystem::path(path));
        if (!result.ok)
            return nullptr;
        auto *m = new nekomote_model;
        m->asset = std::move(result.asset);
        m->ok = true;
        return m;
    }
    catch (...)
    {
        return nullptr;
    }
}

void nekomote_model_destroy(nekomote_model *model)
{
    delete model;
}

nekomote_session_asset *nekomote_session_asset_load(const char *path)
{
    if (!path)
        return nullptr;
    try
    {
        auto result = nekomote::asset_io::load_session_asset(std::filesystem::path(path));
        if (!result.ok)
            return nullptr;
        auto *s = new nekomote_session_asset;
        s->asset = std::move(result.asset);
        s->ok = true;
        return s;
    }
    catch (...)
    {
        return nullptr;
    }
}

void nekomote_session_asset_destroy(nekomote_session_asset *asset)
{
    delete asset;
}

/* ---- Program ---- */

nekomote_program *nekomote_program_compile(
    const nekomote_model *model,
    const nekomote_session_asset *session)
{
    if (!model || !session || !model->ok || !session->ok)
        return nullptr;
    try
    {
        auto compiled = nekomote::application::compile_session(model->asset, session->asset);
        if (!compiled.program)
            return nullptr;
        auto *p = new nekomote_program;
        p->program = std::move(compiled.program);
        return p;
    }
    catch (...)
    {
        return nullptr;
    }
}

void nekomote_program_destroy(nekomote_program *program)
{
    delete program;
}

size_t nekomote_program_texture_count(const nekomote_program *program)
{
    if (!program || !program->program)
        return 0;
    return program->program->playback().scene().textures().size();
}

nekomote_texture_info nekomote_program_texture(
    const nekomote_program *program, size_t index)
{
    nekomote_texture_info info{};
    if (!program || !program->program)
        return info;
    const auto &textures = program->program->playback().scene().textures();
    if (index >= textures.size())
        return info;
    const auto &t = textures[index];
    info.id = t.id.value;
    info.name = t.name.c_str();
    info.path = t.path.c_str();
    info.width = t.width;
    info.height = t.height;
    return info;
}

size_t nekomote_program_audio_count(const nekomote_program *program)
{
    if (!program || !program->program)
        return 0;
    return program->program->audio_paths().size();
}

const char *nekomote_program_audio_path(
    const nekomote_program *program, size_t index)
{
    if (!program || !program->program)
        return nullptr;
    const auto &paths = program->program->audio_paths();
    if (index >= paths.size())
        return nullptr;
    return paths[index].c_str();
}

uint32_t nekomote_program_find_reaction(
    const nekomote_program *program, const char *label)
{
    if (!program || !program->program || !label)
        return 0;
    return program->program->find_reaction(label).value;
}

const char *nekomote_program_reaction_label(
    const nekomote_program *program, uint32_t reaction_id)
{
    if (!program || !program->program)
        return nullptr;
    auto label = program->program->reaction_label(nekomote::ReactionId{reaction_id});
    return label.data();
}

int nekomote_program_gaze_available(const nekomote_program *program)
{
    if (!program || !program->program)
        return 0;
    return program->program->gaze_available() ? 1 : 0;
}

int nekomote_program_touch_available(const nekomote_program *program)
{
    if (!program || !program->program)
        return 0;
    return program->program->touch_available() ? 1 : 0;
}

int nekomote_program_mirror(const nekomote_program *program)
{
    if (!program || !program->program)
        return 0;
    return program->program->mirror() ? 1 : 0;
}

/* ---- Session instance ---- */

nekomote_session *nekomote_session_create(
    const nekomote_program *program,
    nekomote_session_config config)
{
    if (!program || !program->program)
        return nullptr;
    try
    {
        auto *s = new nekomote_session;
        s->instance = std::make_unique<nekomote::application::SessionInstance>(
            *program->program, from_c_config(config));
        return s;
    }
    catch (...)
    {
        return nullptr;
    }
}

void nekomote_session_destroy(nekomote_session *session)
{
    delete session;
}

nekomote_error nekomote_session_start(nekomote_session *session)
{
    if (!session || !session->instance)
        return NEKOMOTE_ERROR_INVALID_INPUT;
    return to_c_error(session->instance->start().error);
}

nekomote_error nekomote_session_set_origin(
    nekomote_session *session, float x, float y)
{
    if (!session || !session->instance)
        return NEKOMOTE_ERROR_INVALID_INPUT;
    return to_c_error(session->instance->set_origin({x, y}).error);
}

nekomote_error nekomote_session_set_scale(
    nekomote_session *session, float scale)
{
    if (!session || !session->instance)
        return NEKOMOTE_ERROR_INVALID_INPUT;
    return to_c_error(session->instance->set_scale(scale).error);
}

nekomote_error nekomote_session_pointer_move(
    nekomote_session *session, float x, float y)
{
    if (!session || !session->instance)
        return NEKOMOTE_ERROR_INVALID_INPUT;
    return to_c_error(session->instance->pointer_move({x, y}).error);
}

nekomote_error nekomote_session_pointer_leave(nekomote_session *session)
{
    if (!session || !session->instance)
        return NEKOMOTE_ERROR_INVALID_INPUT;
    return to_c_error(session->instance->pointer_leave().error);
}

nekomote_error nekomote_session_pointer_enter(
    nekomote_session *session, float x, float y)
{
    if (!session || !session->instance)
        return NEKOMOTE_ERROR_INVALID_INPUT;
    return to_c_error(session->instance->pointer_enter({x, y}).error);
}

nekomote_error nekomote_session_touch(
    nekomote_session *session, float x, float y)
{
    if (!session || !session->instance)
        return NEKOMOTE_ERROR_INVALID_INPUT;
    return to_c_error(session->instance->touch({x, y}).error);
}

nekomote_error nekomote_session_react(
    nekomote_session *session, uint32_t reaction_id)
{
    if (!session || !session->instance)
        return NEKOMOTE_ERROR_INVALID_INPUT;
    return to_c_error(session->instance->react(nekomote::ReactionId{reaction_id}).error);
}

nekomote_error nekomote_session_set_audio_level(
    nekomote_session *session, float rms)
{
    if (!session || !session->instance)
        return NEKOMOTE_ERROR_INVALID_INPUT;
    return to_c_error(session->instance->set_audio_level(rms).error);
}

int nekomote_session_pop_audio_command(
    nekomote_session *session, nekomote_audio_command *out)
{
    if (!session || !session->instance || !out)
        return 0;
    try
    {
        const auto cmd = session->instance->pop_audio_command();
        if (!cmd)
            return 0;
        out->kind = static_cast<nekomote_audio_command_kind>(cmd->kind);
        out->clip_id = cmd->clip.value;
        return 1;
    }
    catch (...)
    {
        return 0;
    }
}

nekomote_error nekomote_session_set_playback_state(
    nekomote_session *session, nekomote_playback_state state)
{
    if (!session || !session->instance)
        return NEKOMOTE_ERROR_INVALID_INPUT;
    auto ps = state == NEKOMOTE_PLAYBACK_PAUSED
                  ? nekomote::runtime::PlaybackState::Paused
                  : nekomote::runtime::PlaybackState::Running;
    return to_c_error(session->instance->set_playback_state(ps).error);
}

nekomote_error nekomote_session_advance(
    nekomote_session *session, double elapsed_seconds)
{
    if (!session || !session->instance)
        return NEKOMOTE_ERROR_INVALID_INPUT;
    return to_c_error(session->instance->advance(elapsed_seconds).error);
}

nekomote_error nekomote_session_prepare_frame(nekomote_session *session)
{
    if (!session || !session->instance)
        return NEKOMOTE_ERROR_INVALID_INPUT;
    return to_c_error(session->instance->prepare_frame().error);
}

nekomote_error nekomote_session_reset(nekomote_session *session)
{
    if (!session || !session->instance)
        return NEKOMOTE_ERROR_INVALID_INPUT;
    return to_c_error(session->instance->reset().error);
}

nekomote_render_frame nekomote_session_frame(const nekomote_session *session)
{
    nekomote_render_frame empty{};
    if (!session || !session->instance)
        return empty;
    return to_c_frame(session->instance->frame());
}

nekomote_reaction_phase nekomote_session_reaction_phase(
    const nekomote_session *session)
{
    if (!session || !session->instance)
        return NEKOMOTE_REACTION_IDLE;
    auto phase = session->instance->reaction_phase();
    switch (phase)
    {
    case nekomote::application::ReactionPhase::Idle:
        return NEKOMOTE_REACTION_IDLE;
    case nekomote::application::ReactionPhase::Playing:
        return NEKOMOTE_REACTION_PLAYING;
    case nekomote::application::ReactionPhase::Recovering:
        return NEKOMOTE_REACTION_RECOVERING;
    }
    return NEKOMOTE_REACTION_IDLE;
}

uint32_t nekomote_session_active_reaction(const nekomote_session *session)
{
    if (!session || !session->instance)
        return 0;
    return session->instance->active_reaction().value;
}

/* ---- Background ---- */

nekomote_error nekomote_session_set_output_size(
    nekomote_session *session, int width, int height)
{
    if (!session || !session->instance)
        return NEKOMOTE_ERROR_INVALID_INPUT;
    return to_c_error(session->instance->set_output_size(width, height).error);
}

nekomote_error nekomote_session_set_background(
    nekomote_session *session, uint32_t texture_id, int width, int height)
{
    if (!session || !session->instance)
        return NEKOMOTE_ERROR_INVALID_INPUT;
    return to_c_error(
        session->instance->set_background(nekomote::TextureId{texture_id}, width, height).error);
}

nekomote_error nekomote_session_fade_background(
    nekomote_session *session, uint32_t texture_id,
    int width, int height, double duration_seconds)
{
    if (!session || !session->instance)
        return NEKOMOTE_ERROR_INVALID_INPUT;
    return to_c_error(
        session->instance->fade_background(
            nekomote::TextureId{texture_id}, width, height, duration_seconds).error);
}

void nekomote_session_clear_background(nekomote_session *session)
{
    if (session && session->instance)
        session->instance->clear_background();
}

nekomote_render_frame nekomote_session_background_frame(const nekomote_session *session)
{
    nekomote_render_frame empty{};
    if (!session || !session->instance)
        return empty;
    return to_c_frame(session->instance->background_frame());
}

/* ---- Renderer ---- */

nekomote_renderer *nekomote_renderer_create(nekomote_gl_loader loader)
{
    if (!loader)
        return nullptr;
    try
    {
        auto *r = new nekomote_renderer;
        r->renderer = std::make_unique<nekomote::render_gl::Renderer>(
            reinterpret_cast<nekomote::render_gl::GlLoader>(loader));
        return r;
    }
    catch (...)
    {
        return nullptr;
    }
}

void nekomote_renderer_destroy(nekomote_renderer *renderer)
{
    delete renderer;
}

void nekomote_renderer_reserve(
    nekomote_renderer *renderer, nekomote_renderer_capacity capacity)
{
    if (!renderer || !renderer->renderer)
        return;
    try
    {
        renderer->renderer->reserve({capacity.vertices, capacity.indices,
                                     capacity.commands, capacity.textures,
                                     capacity.mask_commands, capacity.masks});
    }
    catch (...)
    {
    }
}

void nekomote_renderer_upload_texture(
    nekomote_renderer *renderer, uint32_t id,
    int width, int height, const uint8_t *pixels)
{
    if (!renderer || !renderer->renderer || !pixels)
        return;
    try
    {
        nekomote::ImageRgba8 image;
        image.width = width;
        image.height = height;
        image.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
        renderer->renderer->upload_texture(nekomote::TextureId{id}, image);
    }
    catch (...)
    {
    }
}

void nekomote_renderer_prepare(
    nekomote_renderer *renderer, nekomote_render_frame frame)
{
    if (!renderer || !renderer->renderer)
        return;
    try
    {
        nekomote::RenderFrame f;
        f.vertices = {reinterpret_cast<const nekomote::RenderVertex *>(frame.vertices),
                      frame.vertex_count};
        f.indices = {frame.indices, frame.index_count};
        f.commands = {reinterpret_cast<const nekomote::DrawCommand *>(frame.commands),
                      frame.command_count};
        f.composition = frame.composition == NEKOMOTE_COMPOSITION_PREMULTIPLIED_OVER
                            ? nekomote::ColorComposition::PremultipliedOver
                            : nekomote::ColorComposition::ProtectedStraight;
        f.mask_commands = {
            reinterpret_cast<const nekomote::DrawCommand *>(frame.mask_commands),
            frame.mask_command_count};
        f.masks = {reinterpret_cast<const nekomote::MaskDefinition *>(frame.masks),
                   frame.mask_count};
        renderer->renderer->prepare(f);
    }
    catch (...)
    {
    }
}

void nekomote_renderer_resize_output(
    nekomote_renderer *renderer, int width, int height)
{
    if (!renderer || !renderer->renderer)
        return;
    try
    {
        renderer->renderer->resize_output(width, height);
    }
    catch (...)
    {
    }
}

void nekomote_renderer_render(
    nekomote_renderer *renderer,
    nekomote_render_target target,
    nekomote_render_view view,
    nekomote_target_contents contents)
{
    if (!renderer || !renderer->renderer)
        return;
    try
    {
        nekomote::render_gl::Target t;
        t.framebuffer = target.framebuffer;
        t.width = target.width;
        t.height = target.height;
        auto c = contents == NEKOMOTE_TARGET_PRESERVE
                     ? nekomote::render_gl::TargetContents::Preserve
                     : nekomote::render_gl::TargetContents::Clear;
        renderer->renderer->render(t, from_c_view(view), c);
    }
    catch (...)
    {
    }
}

/* ---- Image loading ---- */

nekomote_image *nekomote_image_load_png(const char *path)
{
    if (!path)
        return nullptr;
    try
    {
        auto result = nekomote::asset_io::load_png(std::filesystem::path(path));
        if (!result.ok)
            return nullptr;
        auto *img = new nekomote_image;
        img->width = result.image.width;
        img->height = result.image.height;
        img->pixels = std::move(result.image.pixels);
        return img;
    }
    catch (...)
    {
        return nullptr;
    }
}

void nekomote_image_destroy(nekomote_image *image)
{
    delete image;
}

int nekomote_image_width(const nekomote_image *image)
{
    return image ? image->width : 0;
}

int nekomote_image_height(const nekomote_image *image)
{
    return image ? image->height : 0;
}

const uint8_t *nekomote_image_pixels(const nekomote_image *image)
{
    return image ? image->pixels.data() : nullptr;
}

} // extern "C"
