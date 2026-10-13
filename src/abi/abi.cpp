#include <nekomote/application/session.hpp>
#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/image.hpp>
#include <nekomote/asset_io/session_asset.hpp>
#include <nekomote/render_frame.hpp>
#include <nekomote/render_gl/renderer.hpp>

#include <nekomote/abi.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "abi_internal.hpp"
#include "wallpaper_instance.hpp"

thread_local nekomote::abi::ErrorState nekomote::abi::g_error;
using nekomote::abi::g_error;
using nekomote::abi::set_error;

namespace
{
using PointerClock = nekomote::application::WallpaperController;
constexpr auto kMaximumPointerTimeMs =
    static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   PointerClock::PointerTime::duration::max())
                                   .count());
PointerClock::PointerTime pointer_time(std::uint64_t timestamp_ms)
{
    return PointerClock::PointerTime(
        std::chrono::duration_cast<PointerClock::PointerTime::duration>(
            std::chrono::milliseconds(static_cast<std::int64_t>(timestamp_ms))));
}

/* ---- Per-thread error diagnostic is in abi_internal.hpp ---- */

nekomote_error session_error_code(nekomote::application::SessionError e)
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

nekomote_error to_c_error(nekomote::application::SessionError e) noexcept
{
    const auto code = session_error_code(e);
    if (code != NEKOMOTE_OK)
        set_error(code, "session operation failed");
    return code;
}

// Wallpaper setup, callbacks and drawing may allocate or invoke a GPU backend.
// Convert failures at the C boundary; diagnostics use the fixed TLS buffer.
template <typename Operation>
nekomote_error wallpaper_call(Operation&& operation) noexcept
{
    try
    {
        return operation();
    }
    catch (const std::bad_alloc&)
    {
        set_error(NEKOMOTE_ERROR_OUT_OF_MEMORY, "wallpaper allocation failed");
        return NEKOMOTE_ERROR_OUT_OF_MEMORY;
    }
    catch (const std::exception& error)
    {
        set_error(NEKOMOTE_ERROR_RENDERER, error.what());
        return NEKOMOTE_ERROR_RENDERER;
    }
    catch (...)
    {
        set_error(NEKOMOTE_ERROR_FAULTED, "unknown wallpaper exception");
        return NEKOMOTE_ERROR_FAULTED;
    }
}

/* ---- Enum conversions with validation ---- */

bool to_cpp_pointer_scope(nekomote_pointer_scope c, nekomote::application::PointerScope& out)
{
    switch (c)
    {
        case NEKOMOTE_POINTER_UNAVAILABLE:
            out = nekomote::application::PointerScope::Unavailable;
            return true;
        case NEKOMOTE_POINTER_LOCAL_SURFACE:
            out = nekomote::application::PointerScope::LocalSurface;
            return true;
    }
    return false;
}

bool to_cpp_audio_mode(nekomote_audio_mode c, nekomote::application::AudioMode& out)
{
    switch (c)
    {
        case NEKOMOTE_AUDIO_DISABLED:
            out = nekomote::application::AudioMode::Disabled;
            return true;
        case NEKOMOTE_AUDIO_DEVICE:
            out = nekomote::application::AudioMode::Device;
            return true;
    }
    return false;
}

bool to_cpp_playback_state(nekomote_playback_state c, nekomote::runtime::PlaybackState& out)
{
    switch (c)
    {
        case NEKOMOTE_PLAYBACK_RUNNING:
            out = nekomote::runtime::PlaybackState::Running;
            return true;
        case NEKOMOTE_PLAYBACK_PAUSED:
            out = nekomote::runtime::PlaybackState::Paused;
            return true;
    }
    return false;
}

bool to_cpp_blend(nekomote_blend_mode c, nekomote::BlendMode& out)
{
    switch (c)
    {
        case NEKOMOTE_BLEND_OVER:
            out = nekomote::BlendMode::Over;
            return true;
        case NEKOMOTE_BLEND_ADD:
            out = nekomote::BlendMode::Add;
            return true;
        case NEKOMOTE_BLEND_MULTIPLY:
            out = nekomote::BlendMode::Multiply;
            return true;
        case NEKOMOTE_BLEND_SCREEN:
            out = nekomote::BlendMode::Screen;
            return true;
    }
    return false;
}

nekomote_blend_mode to_c_blend(nekomote::BlendMode b)
{
    switch (b)
    {
        case nekomote::BlendMode::Over:
            return NEKOMOTE_BLEND_OVER;
        case nekomote::BlendMode::Add:
            return NEKOMOTE_BLEND_ADD;
        case nekomote::BlendMode::Multiply:
            return NEKOMOTE_BLEND_MULTIPLY;
        case nekomote::BlendMode::Screen:
            return NEKOMOTE_BLEND_SCREEN;
    }
    return NEKOMOTE_BLEND_OVER;
}

bool to_cpp_protection(nekomote_color_protection c, nekomote::ColorProtection& out)
{
    switch (c)
    {
        case NEKOMOTE_PROTECTION_NONE:
            out = nekomote::ColorProtection::None;
            return true;
        case NEKOMOTE_PROTECTION_PRESERVE_TRANSLUCENT:
            out = nekomote::ColorProtection::PreserveTranslucent;
            return true;
    }
    return false;
}

nekomote_color_protection to_c_protection(nekomote::ColorProtection p)
{
    switch (p)
    {
        case nekomote::ColorProtection::None:
            return NEKOMOTE_PROTECTION_NONE;
        case nekomote::ColorProtection::PreserveTranslucent:
            return NEKOMOTE_PROTECTION_PRESERVE_TRANSLUCENT;
    }
    return NEKOMOTE_PROTECTION_NONE;
}

bool to_cpp_composition(nekomote_color_composition c, nekomote::ColorComposition& out)
{
    switch (c)
    {
        case NEKOMOTE_COMPOSITION_PREMULTIPLIED_OVER:
            out = nekomote::ColorComposition::PremultipliedOver;
            return true;
        case NEKOMOTE_COMPOSITION_PROTECTED_STRAIGHT:
            out = nekomote::ColorComposition::ProtectedStraight;
            return true;
    }
    return false;
}

nekomote_color_composition to_c_composition(nekomote::ColorComposition c)
{
    switch (c)
    {
        case nekomote::ColorComposition::PremultipliedOver:
            return NEKOMOTE_COMPOSITION_PREMULTIPLIED_OVER;
        case nekomote::ColorComposition::ProtectedStraight:
            return NEKOMOTE_COMPOSITION_PROTECTED_STRAIGHT;
    }
    return NEKOMOTE_COMPOSITION_PREMULTIPLIED_OVER;
}

bool to_cpp_target_contents(nekomote_target_contents c, nekomote::render_gl::TargetContents& out)
{
    switch (c)
    {
        case NEKOMOTE_TARGET_CLEAR:
            out = nekomote::render_gl::TargetContents::Clear;
            return true;
        case NEKOMOTE_TARGET_PRESERVE:
            out = nekomote::render_gl::TargetContents::Preserve;
            return true;
    }
    return false;
}

nekomote::application::SessionConfig from_c_config(nekomote_session_config c, bool& ok)
{
    ok = true;
    nekomote::application::SessionConfig cfg;
    cfg.animation.random_seed = c.random_seed;
    cfg.reaction_seed = c.random_seed;
    if (!to_cpp_pointer_scope(c.pointer, cfg.pointer))
    {
        ok = false;
        return cfg;
    }
    if (!to_cpp_audio_mode(c.audio, cfg.audio))
    {
        ok = false;
        return cfg;
    }
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

/* ---- Field-by-field frame copy (C++ → C storage) ---- */

void copy_commands_to_c(std::vector<nekomote_draw_command>& dst,
                        std::span<const nekomote::DrawCommand> src)
{
    dst.resize(src.size());
    for (size_t i = 0; i < src.size(); ++i)
    {
        dst[i].texture = src[i].texture.value;
        dst[i].first_index = src[i].first_index;
        dst[i].index_count = src[i].index_count;
        dst[i].blend = to_c_blend(src[i].blend);
        dst[i].protection = to_c_protection(src[i].protection);
        dst[i].mask = src[i].mask.value;
    }
}

void copy_frame_to_c(const nekomote::RenderFrame& f, std::vector<nekomote_vertex>& c_verts,
                     std::vector<uint32_t>& c_indices, std::vector<nekomote_draw_command>& c_cmds,
                     std::vector<nekomote_draw_command>& c_mask_cmds,
                     std::vector<nekomote_mask_definition>& c_masks, nekomote_render_frame& out)
{
    c_verts.resize(f.vertices.size());
    for (size_t i = 0; i < f.vertices.size(); ++i)
    {
        c_verts[i].x = f.vertices[i].x;
        c_verts[i].y = f.vertices[i].y;
        c_verts[i].u = f.vertices[i].u;
        c_verts[i].v = f.vertices[i].v;
        c_verts[i].color[0] = f.vertices[i].color[0];
        c_verts[i].color[1] = f.vertices[i].color[1];
        c_verts[i].color[2] = f.vertices[i].color[2];
        c_verts[i].color[3] = f.vertices[i].color[3];
    }

    c_indices.assign(f.indices.begin(), f.indices.end());

    copy_commands_to_c(c_cmds, f.commands);
    copy_commands_to_c(c_mask_cmds, f.mask_commands);

    c_masks.resize(f.masks.size());
    for (size_t i = 0; i < f.masks.size(); ++i)
    {
        c_masks[i].first_command = f.masks[i].first_command;
        c_masks[i].command_count = f.masks[i].command_count;
    }

    out.vertices = c_verts.data();
    out.vertex_count = c_verts.size();
    out.indices = c_indices.data();
    out.index_count = c_indices.size();
    out.commands = c_cmds.data();
    out.command_count = c_cmds.size();
    out.composition = to_c_composition(f.composition);
    out.mask_commands = c_mask_cmds.data();
    out.mask_command_count = c_mask_cmds.size();
    out.masks = c_masks.data();
    out.mask_count = c_masks.size();
}

/* ---- Field-by-field frame copy (C → C++ storage) for renderer ---- */

bool copy_frame_from_c(const nekomote_render_frame& c,
                       std::vector<nekomote::RenderVertex>& cpp_verts,
                       std::vector<uint32_t>& cpp_indices,
                       std::vector<nekomote::DrawCommand>& cpp_cmds,
                       std::vector<nekomote::DrawCommand>& cpp_mask_cmds,
                       std::vector<nekomote::MaskDefinition>& cpp_masks,
                       nekomote::ColorComposition& cpp_composition)
{
    if (c.vertex_count > 0 && !c.vertices)
        return false;
    if (c.index_count > 0 && !c.indices)
        return false;
    if (c.command_count > 0 && !c.commands)
        return false;
    if (c.mask_command_count > 0 && !c.mask_commands)
        return false;
    if (c.mask_count > 0 && !c.masks)
        return false;

    cpp_verts.resize(c.vertex_count);
    for (size_t i = 0; i < c.vertex_count; ++i)
    {
        cpp_verts[i].x = c.vertices[i].x;
        cpp_verts[i].y = c.vertices[i].y;
        cpp_verts[i].u = c.vertices[i].u;
        cpp_verts[i].v = c.vertices[i].v;
        cpp_verts[i].color[0] = c.vertices[i].color[0];
        cpp_verts[i].color[1] = c.vertices[i].color[1];
        cpp_verts[i].color[2] = c.vertices[i].color[2];
        cpp_verts[i].color[3] = c.vertices[i].color[3];
    }

    if (c.index_count)
        cpp_indices.assign(c.indices, c.indices + c.index_count);
    else
        cpp_indices.clear();

    auto copy_cmds = [](std::vector<nekomote::DrawCommand>& dst, const nekomote_draw_command* src,
                        size_t n) -> bool
    {
        dst.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            dst[i].texture = nekomote::TextureId{src[i].texture};
            dst[i].first_index = src[i].first_index;
            dst[i].index_count = src[i].index_count;
            if (!to_cpp_blend(src[i].blend, dst[i].blend))
                return false;
            if (!to_cpp_protection(src[i].protection, dst[i].protection))
                return false;
            dst[i].mask = nekomote::MaskId{src[i].mask};
        }
        return true;
    };

    if (!copy_cmds(cpp_cmds, c.commands, c.command_count))
        return false;
    if (!copy_cmds(cpp_mask_cmds, c.mask_commands, c.mask_command_count))
        return false;

    cpp_masks.resize(c.mask_count);
    for (size_t i = 0; i < c.mask_count; ++i)
    {
        cpp_masks[i].first_command = c.masks[i].first_command;
        cpp_masks[i].command_count = c.masks[i].command_count;
    }

    if (!to_cpp_composition(c.composition, cpp_composition))
        return false;

    return true;
}

} // namespace

/* ---- Opaque handle definitions ---- */

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
    std::vector<nekomote_vertex> frame_vertices;
    std::vector<uint32_t> frame_indices;
    std::vector<nekomote_draw_command> frame_commands;
    std::vector<nekomote_draw_command> frame_mask_commands;
    std::vector<nekomote_mask_definition> frame_masks;
    nekomote_render_frame frame{};
    std::vector<nekomote_vertex> bg_vertices;
    std::vector<uint32_t> bg_indices;
    std::vector<nekomote_draw_command> bg_commands;
    std::vector<nekomote_draw_command> bg_mask_commands;
    std::vector<nekomote_mask_definition> bg_masks;
    nekomote_render_frame bg_frame{};
};

struct nekomote_renderer
{
    std::unique_ptr<nekomote::render_gl::Renderer> renderer;
    std::vector<nekomote::RenderVertex> prep_vertices;
    std::vector<uint32_t> prep_indices;
    std::vector<nekomote::DrawCommand> prep_commands;
    std::vector<nekomote::DrawCommand> prep_mask_commands;
    std::vector<nekomote::MaskDefinition> prep_masks;
    bool frame_ready{false};
};

struct nekomote_image
{
    nekomote::asset_io::DecodedImageRgba8 image;
};

extern "C"
{

    /* ---- Error diagnostic ---- */

    nekomote_error nekomote_last_error(void)
    {
        return g_error.code;
    }

    const char* nekomote_error_message(void)
    {
        return g_error.message[0] == 0 ? nullptr : g_error.message.data();
    }

    void nekomote_clear_error(void)
    {
        g_error.code = NEKOMOTE_OK;
        g_error.message[0] = 0;
    }

    const char* nekomote_version(void)
    {
        return "0.5.0";
    }

    /* ---- Asset loading ---- */

    nekomote_model* nekomote_model_load(const char* path)
    {
        if (!path)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null path");
            return nullptr;
        }
        try
        {
            auto result = nekomote::asset_io::load_asset(std::filesystem::path(path));
            if (!result.ok)
            {
                set_error(NEKOMOTE_ERROR_IO, "asset load failed");
                return nullptr;
            }
            auto m = std::make_unique<nekomote_model>();
            m->asset = std::move(result.asset);
            m->ok = true;
            return m.release();
        }
        catch (const std::exception& e)
        {
            set_error(NEKOMOTE_ERROR_FAULTED, e.what());
            return nullptr;
        }
        catch (...)
        {
            set_error(NEKOMOTE_ERROR_FAULTED, "unknown exception");
            return nullptr;
        }
    }

    void nekomote_model_destroy(nekomote_model* model)
    {
        delete model;
    }

    nekomote_session_asset* nekomote_session_asset_load(const char* path)
    {
        if (!path)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null path");
            return nullptr;
        }
        try
        {
            auto result = nekomote::asset_io::load_session_asset(std::filesystem::path(path));
            if (!result.ok)
            {
                set_error(NEKOMOTE_ERROR_IO, "session asset load failed");
                return nullptr;
            }
            auto s = std::make_unique<nekomote_session_asset>();
            s->asset = std::move(result.asset);
            s->ok = true;
            return s.release();
        }
        catch (const std::exception& e)
        {
            set_error(NEKOMOTE_ERROR_FAULTED, e.what());
            return nullptr;
        }
        catch (...)
        {
            set_error(NEKOMOTE_ERROR_FAULTED, "unknown exception");
            return nullptr;
        }
    }

    void nekomote_session_asset_destroy(nekomote_session_asset* asset)
    {
        delete asset;
    }

    /* ---- Program ---- */

    nekomote_program* nekomote_program_compile(const nekomote_model* model,
                                               const nekomote_session_asset* session)
    {
        if (!model || !session || !model->ok || !session->ok)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null or invalid model/session");
            return nullptr;
        }
        try
        {
            auto compiled = nekomote::application::compile_session(model->asset, session->asset);
            if (!compiled.program)
            {
                std::string msg = "compile failed";
                if (!compiled.errors.empty())
                {
                    msg += ": ";
                    msg += compiled.errors[0];
                }
                set_error(NEKOMOTE_ERROR_UNSUPPORTED, msg.c_str());
                return nullptr;
            }
            auto p = std::make_unique<nekomote_program>();
            p->program = std::move(compiled.program);
            return p.release();
        }
        catch (const std::exception& e)
        {
            set_error(NEKOMOTE_ERROR_FAULTED, e.what());
            return nullptr;
        }
        catch (...)
        {
            set_error(NEKOMOTE_ERROR_FAULTED, "unknown exception");
            return nullptr;
        }
    }

    void nekomote_program_destroy(nekomote_program* program)
    {
        delete program;
    }

    size_t nekomote_program_texture_count(const nekomote_program* program)
    {
        if (!program || !program->program)
            return 0;
        return program->program->playback().scene().textures().size();
    }

    nekomote_texture_info nekomote_program_texture(const nekomote_program* program, size_t index)
    {
        nekomote_texture_info info{};
        if (!program || !program->program)
            return info;
        const auto& textures = program->program->playback().scene().textures();
        if (index >= textures.size())
            return info;
        const auto& t = textures[index];
        info.id = t.id.value;
        info.name = t.name.c_str();
        info.path = t.path.c_str();
        info.width = t.width;
        info.height = t.height;
        return info;
    }

    size_t nekomote_program_audio_count(const nekomote_program* program)
    {
        if (!program || !program->program)
            return 0;
        return program->program->audio_paths().size();
    }

    const char* nekomote_program_audio_path(const nekomote_program* program, size_t index)
    {
        if (!program || !program->program)
            return nullptr;
        const auto& paths = program->program->audio_paths();
        if (index >= paths.size())
            return nullptr;
        return paths[index].c_str();
    }

    uint32_t nekomote_program_find_reaction(const nekomote_program* program, const char* label)
    {
        if (!program || !program->program || !label)
            return 0;
        return program->program->find_reaction(label).value;
    }

    const char* nekomote_program_reaction_label(const nekomote_program* program,
                                                uint32_t reaction_id)
    {
        if (!program || !program->program)
            return nullptr;
        auto label = program->program->reaction_label(nekomote::ReactionId{reaction_id});
        return label.data();
    }

    int nekomote_program_gaze_available(const nekomote_program* program)
    {
        if (!program || !program->program)
            return 0;
        return program->program->gaze_available() ? 1 : 0;
    }

    int nekomote_program_touch_available(const nekomote_program* program)
    {
        if (!program || !program->program)
            return 0;
        return program->program->touch_available() ? 1 : 0;
    }

    int nekomote_program_mirror(const nekomote_program* program)
    {
        if (!program || !program->program)
            return 0;
        return program->program->mirror() ? 1 : 0;
    }

    nekomote_renderer_capacity nekomote_program_renderer_capacity(const nekomote_program* program)
    {
        nekomote_renderer_capacity cap{};
        if (!program || !program->program)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null or invalid program");
            return cap;
        }
        const auto fc = program->program->playback().scene().frame_capacity();
        cap.vertices = fc.vertices;
        cap.indices = fc.indices;
        cap.commands = fc.commands;
        cap.textures = program->program->playback().scene().textures().size();
        cap.mask_commands = fc.mask_commands;
        cap.masks = fc.masks;
        return cap;
    }

    /* ---- Session instance ---- */

    nekomote_session* nekomote_session_create(const nekomote_program* program,
                                              nekomote_session_config config)
    {
        if (!program || !program->program)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null or invalid program");
            return nullptr;
        }
        bool config_ok = true;
        auto cfg = from_c_config(config, config_ok);
        if (!config_ok)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "invalid session config enum");
            return nullptr;
        }
        try
        {
            auto s = std::make_unique<nekomote_session>();
            s->instance =
                std::make_unique<nekomote::application::SessionInstance>(*program->program, cfg);
            const auto capacity = program->program->playback().scene().frame_capacity();
            s->frame_vertices.reserve(capacity.vertices);
            s->frame_indices.reserve(capacity.indices);
            s->frame_commands.reserve(capacity.commands);
            s->frame_mask_commands.reserve(capacity.mask_commands);
            s->frame_masks.reserve(capacity.masks);
            s->bg_vertices.reserve(8);
            s->bg_indices.reserve(12);
            s->bg_commands.reserve(2);
            return s.release();
        }
        catch (const std::bad_alloc&)
        {
            set_error(NEKOMOTE_ERROR_OUT_OF_MEMORY, "session allocation failed");
            return nullptr;
        }
        catch (const std::exception& e)
        {
            set_error(NEKOMOTE_ERROR_FAULTED, e.what());
            return nullptr;
        }
        catch (...)
        {
            set_error(NEKOMOTE_ERROR_FAULTED, "unknown exception");
            return nullptr;
        }
    }

    void nekomote_session_destroy(nekomote_session* session)
    {
        delete session;
    }

    nekomote_error nekomote_session_start(nekomote_session* session)
    {
        if (!session || !session->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null session");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        auto st = session->instance->start();
        if (st.error != nekomote::application::SessionError::None)
            set_error(NEKOMOTE_ERROR_PLAYBACK, "start failed");
        return to_c_error(st.error);
    }

    nekomote_error nekomote_session_set_origin(nekomote_session* session, float x, float y)
    {
        if (!session || !session->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null session");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return to_c_error(session->instance->set_origin({x, y}).error);
    }

    nekomote_error nekomote_session_set_scale(nekomote_session* session, float scale)
    {
        if (!session || !session->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null session");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return to_c_error(session->instance->set_scale(scale).error);
    }

    nekomote_error nekomote_session_pointer_move(nekomote_session* session, float x, float y)
    {
        if (!session || !session->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null session");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return to_c_error(session->instance->pointer_move({x, y}).error);
    }

    nekomote_error nekomote_session_pointer_leave(nekomote_session* session)
    {
        if (!session || !session->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null session");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return to_c_error(session->instance->pointer_leave().error);
    }

    nekomote_error nekomote_session_pointer_enter(nekomote_session* session, float x, float y)
    {
        if (!session || !session->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null session");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return to_c_error(session->instance->pointer_enter({x, y}).error);
    }

    nekomote_error nekomote_session_touch(nekomote_session* session, float x, float y)
    {
        if (!session || !session->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null session");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return to_c_error(session->instance->touch({x, y}).error);
    }

    nekomote_error nekomote_session_react(nekomote_session* session, uint32_t reaction_id)
    {
        if (!session || !session->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null session");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return to_c_error(session->instance->react(nekomote::ReactionId{reaction_id}).error);
    }

    nekomote_error nekomote_session_set_audio_level(nekomote_session* session, float rms)
    {
        if (!session || !session->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null session");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return to_c_error(session->instance->set_audio_level(rms).error);
    }

    int nekomote_session_pop_audio_command(nekomote_session* session, nekomote_audio_command* out)
    {
        if (!session || !session->instance || !out)
            return 0;
        try
        {
            const auto cmd = session->instance->pop_audio_command();
            if (!cmd)
                return 0;
            switch (cmd->kind)
            {
                case nekomote::application::AudioCommandKind::Play:
                    out->kind = NEKOMOTE_AUDIO_PLAY;
                    break;
                case nekomote::application::AudioCommandKind::Stop:
                    out->kind = NEKOMOTE_AUDIO_STOP;
                    break;
                case nekomote::application::AudioCommandKind::Pause:
                    out->kind = NEKOMOTE_AUDIO_PAUSE;
                    break;
                case nekomote::application::AudioCommandKind::Resume:
                    out->kind = NEKOMOTE_AUDIO_RESUME;
                    break;
            }
            out->clip_id = cmd->clip.value;
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    nekomote_error nekomote_session_set_playback_state(nekomote_session* session,
                                                       nekomote_playback_state state)
    {
        if (!session || !session->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null session");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        nekomote::runtime::PlaybackState ps;
        if (!to_cpp_playback_state(state, ps))
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "invalid playback state enum");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return to_c_error(session->instance->set_playback_state(ps).error);
    }

    nekomote_error nekomote_session_advance(nekomote_session* session, double elapsed_seconds)
    {
        if (!session || !session->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null session");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        if (elapsed_seconds < 0.0)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "negative elapsed_seconds");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return to_c_error(session->instance->advance(elapsed_seconds).error);
    }

    nekomote_error nekomote_session_prepare_frame(nekomote_session* session)
    {
        if (!session || !session->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null session");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        auto st = session->instance->prepare_frame();
        if (st.error != nekomote::application::SessionError::None)
        {
            set_error(NEKOMOTE_ERROR_PLAYBACK, "prepare_frame failed");
            return to_c_error(st.error);
        }
        session->frame = {};
        try
        {
            auto frame = session->instance->frame();
            copy_frame_to_c(frame, session->frame_vertices, session->frame_indices,
                            session->frame_commands, session->frame_mask_commands,
                            session->frame_masks, session->frame);
            return NEKOMOTE_OK;
        }
        catch (const std::bad_alloc&)
        {
            set_error(NEKOMOTE_ERROR_OUT_OF_MEMORY, "frame copy allocation failed");
            return NEKOMOTE_ERROR_OUT_OF_MEMORY;
        }
        catch (...)
        {
            set_error(NEKOMOTE_ERROR_FAULTED, "frame copy failed");
            return NEKOMOTE_ERROR_FAULTED;
        }
    }

    nekomote_error nekomote_session_reset(nekomote_session* session)
    {
        if (!session || !session->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null session");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return to_c_error(session->instance->reset().error);
    }

    nekomote_render_frame nekomote_session_frame(const nekomote_session* session)
    {
        if (!session || !session->instance)
            return nekomote_render_frame{};
        return session->instance->frame().commands.empty() ? nekomote_render_frame{}
                                                           : session->frame;
    }

    nekomote_reaction_phase nekomote_session_reaction_phase(const nekomote_session* session)
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

    uint32_t nekomote_session_active_reaction(const nekomote_session* session)
    {
        if (!session || !session->instance)
            return 0;
        return session->instance->active_reaction().value;
    }

    /* ---- Background ---- */

    nekomote_error nekomote_session_set_output_size(nekomote_session* session, int width,
                                                    int height)
    {
        if (!session || !session->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null session");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        if (width <= 0 || height <= 0)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "non-positive output size");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return to_c_error(session->instance->set_output_size(width, height).error);
    }

    nekomote_error nekomote_session_set_background(nekomote_session* session, uint32_t texture_id,
                                                   int width, int height)
    {
        if (!session || !session->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null session");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        if (width <= 0 || height <= 0)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "non-positive background size");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return to_c_error(
            session->instance->set_background(nekomote::TextureId{texture_id}, width, height)
                .error);
    }

    nekomote_error nekomote_session_fade_background(nekomote_session* session, uint32_t texture_id,
                                                    int width, int height, double duration_seconds)
    {
        if (!session || !session->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null session");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        if (width <= 0 || height <= 0)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "non-positive background size");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        if (duration_seconds < 0.0)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "negative fade duration");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return to_c_error(
            session->instance
                ->fade_background(nekomote::TextureId{texture_id}, width, height, duration_seconds)
                .error);
    }

    void nekomote_session_clear_background(nekomote_session* session)
    {
        if (session && session->instance)
            session->instance->clear_background();
    }

    nekomote_render_frame nekomote_session_background_frame(const nekomote_session* session)
    {
        if (!session || !session->instance)
            return nekomote_render_frame{};
        auto* s = const_cast<nekomote_session*>(session);
        s->bg_frame = {};
        try
        {
            auto frame = s->instance->background_frame();
            copy_frame_to_c(frame, s->bg_vertices, s->bg_indices, s->bg_commands,
                            s->bg_mask_commands, s->bg_masks, s->bg_frame);
            return s->bg_frame;
        }
        catch (const std::bad_alloc&)
        {
            set_error(NEKOMOTE_ERROR_OUT_OF_MEMORY, "background frame allocation failed");
        }
        catch (...)
        {
            set_error(NEKOMOTE_ERROR_FAULTED, "background frame copy failed");
        }
        return {};
    }

    /* ---- Renderer ---- */

    nekomote_renderer* nekomote_renderer_create(nekomote_gl_loader loader)
    {
        if (!loader)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null GL loader");
            return nullptr;
        }
        try
        {
            auto r = std::make_unique<nekomote_renderer>();
            r->renderer = std::make_unique<nekomote::render_gl::Renderer>(loader);
            return r.release();
        }
        catch (const std::bad_alloc&)
        {
            set_error(NEKOMOTE_ERROR_OUT_OF_MEMORY, "renderer allocation failed");
            return nullptr;
        }
        catch (const std::exception& e)
        {
            set_error(NEKOMOTE_ERROR_RENDERER, e.what());
            return nullptr;
        }
        catch (...)
        {
            set_error(NEKOMOTE_ERROR_RENDERER, "unknown exception");
            return nullptr;
        }
    }

    void nekomote_renderer_destroy(nekomote_renderer* renderer)
    {
        delete renderer;
    }

    nekomote_error nekomote_renderer_reserve(nekomote_renderer* renderer,
                                             nekomote_renderer_capacity capacity)
    {
        if (!renderer || !renderer->renderer)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null renderer");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        try
        {
            renderer->prep_vertices.reserve(capacity.vertices);
            renderer->prep_indices.reserve(capacity.indices);
            renderer->prep_commands.reserve(capacity.commands);
            renderer->prep_mask_commands.reserve(capacity.mask_commands);
            renderer->prep_masks.reserve(capacity.masks);
            renderer->renderer->reserve({capacity.vertices, capacity.indices, capacity.commands,
                                         capacity.textures, capacity.mask_commands,
                                         capacity.masks});
            return NEKOMOTE_OK;
        }
        catch (const std::bad_alloc&)
        {
            set_error(NEKOMOTE_ERROR_OUT_OF_MEMORY, "renderer reserve allocation failed");
            return NEKOMOTE_ERROR_OUT_OF_MEMORY;
        }
        catch (const std::exception& e)
        {
            set_error(NEKOMOTE_ERROR_RENDERER, e.what());
            return NEKOMOTE_ERROR_RENDERER;
        }
        catch (...)
        {
            set_error(NEKOMOTE_ERROR_RENDERER, "unknown exception");
            return NEKOMOTE_ERROR_RENDERER;
        }
    }

    nekomote_error nekomote_renderer_upload_texture(nekomote_renderer* renderer, uint32_t id,
                                                    int width, int height, const uint8_t* pixels,
                                                    size_t pixel_bytes)
    {
        if (!renderer || !renderer->renderer)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null renderer");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        if (!pixels)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null pixels");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        if (width <= 0 || height <= 0)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "non-positive texture dimensions");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        constexpr size_t kBytesPerPixel = 4;
        const size_t w = static_cast<size_t>(width);
        const size_t h = static_cast<size_t>(height);
        if (w > (std::numeric_limits<size_t>::max() / kBytesPerPixel) / h)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "texture dimensions overflow");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        const size_t byte_count = w * h * kBytesPerPixel;
        if (pixel_bytes != byte_count)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "RGBA8 byte length does not match dimensions");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        try
        {
            nekomote::ImageViewRgba8 image{width, height, {pixels, byte_count}};
            renderer->renderer->upload_texture(nekomote::TextureId{id}, image);
            return NEKOMOTE_OK;
        }
        catch (const std::bad_alloc&)
        {
            set_error(NEKOMOTE_ERROR_OUT_OF_MEMORY, "texture upload allocation failed");
            return NEKOMOTE_ERROR_OUT_OF_MEMORY;
        }
        catch (const std::exception& e)
        {
            set_error(NEKOMOTE_ERROR_RENDERER, e.what());
            return NEKOMOTE_ERROR_RENDERER;
        }
        catch (...)
        {
            set_error(NEKOMOTE_ERROR_RENDERER, "unknown exception");
            return NEKOMOTE_ERROR_RENDERER;
        }
    }

    nekomote_error nekomote_renderer_prepare(nekomote_renderer* renderer,
                                             nekomote_render_frame frame)
    {
        if (!renderer || !renderer->renderer)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null renderer");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        renderer->frame_ready = false;
        try
        {
            nekomote::ColorComposition composition;
            if (!copy_frame_from_c(frame, renderer->prep_vertices, renderer->prep_indices,
                                   renderer->prep_commands, renderer->prep_mask_commands,
                                   renderer->prep_masks, composition))
            {
                set_error(NEKOMOTE_ERROR_INVALID_INPUT, "invalid frame data or enum");
                return NEKOMOTE_ERROR_INVALID_INPUT;
            }
            nekomote::RenderFrame f;
            f.vertices = renderer->prep_vertices;
            f.indices = renderer->prep_indices;
            f.commands = renderer->prep_commands;
            f.composition = composition;
            f.mask_commands = renderer->prep_mask_commands;
            f.masks = renderer->prep_masks;
            renderer->renderer->prepare(f);
            renderer->frame_ready = true;
            return NEKOMOTE_OK;
        }
        catch (const std::bad_alloc&)
        {
            set_error(NEKOMOTE_ERROR_OUT_OF_MEMORY, "frame preparation allocation failed");
            return NEKOMOTE_ERROR_OUT_OF_MEMORY;
        }
        catch (const std::exception& e)
        {
            set_error(NEKOMOTE_ERROR_RENDERER, e.what());
            return NEKOMOTE_ERROR_RENDERER;
        }
        catch (...)
        {
            set_error(NEKOMOTE_ERROR_RENDERER, "unknown exception");
            return NEKOMOTE_ERROR_RENDERER;
        }
    }

    nekomote_error nekomote_renderer_resize_output(nekomote_renderer* renderer, int width,
                                                   int height)
    {
        if (!renderer || !renderer->renderer)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null renderer");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        if (width <= 0 || height <= 0)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "non-positive output size");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        try
        {
            renderer->renderer->resize_output(width, height);
            return NEKOMOTE_OK;
        }
        catch (const std::exception& e)
        {
            set_error(NEKOMOTE_ERROR_RENDERER, e.what());
            return NEKOMOTE_ERROR_RENDERER;
        }
        catch (...)
        {
            set_error(NEKOMOTE_ERROR_RENDERER, "unknown exception");
            return NEKOMOTE_ERROR_RENDERER;
        }
    }

    nekomote_error nekomote_renderer_render(nekomote_renderer* renderer,
                                            nekomote_render_target target,
                                            nekomote_render_view view,
                                            nekomote_target_contents contents)
    {
        if (!renderer || !renderer->renderer)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null renderer");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        if (!renderer->frame_ready)
        {
            set_error(NEKOMOTE_ERROR_RENDERER,
                      "no valid prepared frame; prepare again after failure");
            return NEKOMOTE_ERROR_RENDERER;
        }
        if (target.width <= 0 || target.height <= 0)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "non-positive target size");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        nekomote::render_gl::TargetContents tc;
        if (!to_cpp_target_contents(contents, tc))
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "invalid target contents enum");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        try
        {
            nekomote::render_gl::Target t;
            t.framebuffer = target.framebuffer;
            t.width = target.width;
            t.height = target.height;
            renderer->renderer->render(t, from_c_view(view), tc);
            return NEKOMOTE_OK;
        }
        catch (const std::exception& e)
        {
            set_error(NEKOMOTE_ERROR_RENDERER, e.what());
            return NEKOMOTE_ERROR_RENDERER;
        }
        catch (...)
        {
            set_error(NEKOMOTE_ERROR_RENDERER, "unknown exception");
            return NEKOMOTE_ERROR_RENDERER;
        }
    }

    /* ---- Image loading ---- */

    nekomote_image* nekomote_image_load_png(const char* path)
    {
        if (!path)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null path");
            return nullptr;
        }
        try
        {
            auto result = nekomote::asset_io::load_png(std::filesystem::path(path));
            if (!result.ok)
            {
                set_error(NEKOMOTE_ERROR_IO, "PNG load failed");
                return nullptr;
            }
            auto img = std::make_unique<nekomote_image>();
            img->image = std::move(result.image);
            return img.release();
        }
        catch (const std::exception& e)
        {
            set_error(NEKOMOTE_ERROR_FAULTED, e.what());
            return nullptr;
        }
        catch (...)
        {
            set_error(NEKOMOTE_ERROR_FAULTED, "unknown exception");
            return nullptr;
        }
    }

    void nekomote_image_destroy(nekomote_image* image)
    {
        delete image;
    }

    int nekomote_image_width(const nekomote_image* image)
    {
        return image ? image->image.width : 0;
    }

    int nekomote_image_height(const nekomote_image* image)
    {
        return image ? image->image.height : 0;
    }

    size_t nekomote_image_byte_count(const nekomote_image* image)
    {
        return image ? image->image.pixels.size() : 0;
    }

    const uint8_t* nekomote_image_pixels(const nekomote_image* image)
    {
        return image ? image->image.pixels.data() : nullptr;
    }

    /* ---- Wallpaper instance ---- */

    struct nekomote_wallpaper
    {
        std::unique_ptr<nekomote::abi::WallpaperInstance> instance;
    };

    nekomote_wallpaper* nekomote_wallpaper_create(nekomote_gl_loader loader)
    {
        if (!loader)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null GL loader");
            return nullptr;
        }
        try
        {
            auto w = std::make_unique<nekomote_wallpaper>();
            w->instance = std::make_unique<nekomote::abi::WallpaperInstance>(
                reinterpret_cast<nekomote::render_gl::GlProc (*)(const char*)>(loader));
            return w.release();
        }
        catch (const std::bad_alloc&)
        {
            set_error(NEKOMOTE_ERROR_OUT_OF_MEMORY, "wallpaper allocation failed");
            return nullptr;
        }
        catch (const std::exception& e)
        {
            set_error(NEKOMOTE_ERROR_FAULTED, e.what());
            return nullptr;
        }
        catch (...)
        {
            set_error(NEKOMOTE_ERROR_FAULTED, "unknown exception");
            return nullptr;
        }
    }

    void nekomote_wallpaper_destroy(nekomote_wallpaper* wp)
    {
        delete wp;
    }

    nekomote_wallpaper* nekomote_wallpaper_create_vk(nekomote_vk_device_context ctx)
    {
        if (!ctx.instance || !ctx.physical_device || !ctx.device || !ctx.graphics_queue)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null Vulkan device context member");
            return nullptr;
        }
        if (ctx.color_format != 37)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "Vulkan target requires RGBA8 UNORM (37)");
            return nullptr;
        }
        try
        {
#if defined(NEKOMOTE_HAS_VULKAN)
            auto w = std::make_unique<nekomote_wallpaper>();
            nekomote::render_vk::DeviceContext vk_ctx{};
            vk_ctx.instance = static_cast<VkInstance>(ctx.instance);
            vk_ctx.physical_device = static_cast<VkPhysicalDevice>(ctx.physical_device);
            vk_ctx.device = static_cast<VkDevice>(ctx.device);
            vk_ctx.graphics_queue = static_cast<VkQueue>(ctx.graphics_queue);
            vk_ctx.graphics_queue_family = ctx.graphics_queue_family;
            vk_ctx.color_format = static_cast<VkFormat>(ctx.color_format);
            w->instance = std::make_unique<nekomote::abi::WallpaperInstance>(vk_ctx);
            return w.release();
#else
            (void)ctx;
            set_error(NEKOMOTE_ERROR_UNSUPPORTED, "Vulkan backend not built");
            return nullptr;
#endif
        }
        catch (const std::bad_alloc&)
        {
            set_error(NEKOMOTE_ERROR_OUT_OF_MEMORY, "wallpaper allocation failed");
            return nullptr;
        }
        catch (const std::exception& e)
        {
            set_error(NEKOMOTE_ERROR_FAULTED, e.what());
            return nullptr;
        }
        catch (...)
        {
            set_error(NEKOMOTE_ERROR_FAULTED, "unknown exception");
            return nullptr;
        }
    }

    nekomote_error nekomote_wallpaper_set_vk_target(nekomote_wallpaper* wp,
                                                    nekomote_vk_target target)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
#if defined(NEKOMOTE_HAS_VULKAN)
        nekomote::render_vk::Target vk_target{};
        vk_target.commands = static_cast<VkCommandBuffer>(target.commands);
        vk_target.framebuffer = static_cast<VkFramebuffer>(target.framebuffer);
        vk_target.width = target.width;
        vk_target.height = target.height;
        return wallpaper_call([&] { return wp->instance->set_vk_target(vk_target); });
#else
        (void)target;
        set_error(NEKOMOTE_ERROR_UNSUPPORTED, "Vulkan backend not built");
        return NEKOMOTE_ERROR_UNSUPPORTED;
#endif
    }

    nekomote_error nekomote_wallpaper_set_assets_dir(nekomote_wallpaper* wp, const char* dir)
    {
        if (!wp || !wp->instance || !dir)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper or dir");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&]
                              { return wp->instance->set_assets_dir(std::filesystem::path(dir)); });
    }

    nekomote_error nekomote_wallpaper_set_no_save(nekomote_wallpaper* wp, int no_save)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->set_no_save(no_save != 0); });
    }

    nekomote_error nekomote_wallpaper_set_muted(nekomote_wallpaper* wp, int muted)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->set_muted(muted != 0); });
    }

    nekomote_error nekomote_wallpaper_set_seed(nekomote_wallpaper* wp, uint32_t seed)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->set_seed(seed); });
    }

    nekomote_error nekomote_wallpaper_set_output_size(nekomote_wallpaper* wp, int width, int height)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->set_output_size(width, height); });
    }

    nekomote_error nekomote_wallpaper_set_paused(nekomote_wallpaper* wp, int paused)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->set_paused(paused != 0); });
    }

    nekomote_error nekomote_wallpaper_set_ui_open(nekomote_wallpaper* wp, int open)
    {
        if (!wp || !wp->instance || (open != 0 && open != 1))
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "invalid wallpaper or panel state");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->set_ui_open(open != 0); });
    }

    void nekomote_wallpaper_set_render_target(nekomote_wallpaper* wp, uint32_t fbo)
    {
        if (wp && wp->instance)
            wp->instance->set_render_target_fbo(fbo);
    }

    void nekomote_wallpaper_reset_gl(nekomote_wallpaper* wp)
    {
        if (wp && wp->instance)
            wp->instance->reset_gpu_resources();
    }

    nekomote_error nekomote_wallpaper_load_initial(nekomote_wallpaper* wp)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->load_initial(); });
    }

    nekomote_error nekomote_wallpaper_load_model(nekomote_wallpaper* wp, const char* model_path,
                                                 const char* session_path)
    {
        if (!wp || !wp->instance || !model_path || !session_path)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper or paths");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call(
            [&]
            {
                return wp->instance->load_model(std::filesystem::path(model_path),
                                                std::filesystem::path(session_path));
            });
    }

    nekomote_error nekomote_wallpaper_advance(nekomote_wallpaper* wp, double dt)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->advance(dt); });
    }

    nekomote_error nekomote_wallpaper_render_frame(nekomote_wallpaper* wp)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->render_frame(); });
    }

    nekomote_error nekomote_wallpaper_render_ui(nekomote_wallpaper* wp, float dt)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->render_ui(dt); });
    }

    nekomote_error nekomote_wallpaper_pointer_move(nekomote_wallpaper* wp, double x, double y,
                                                   uint64_t timestamp_ms)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        if (timestamp_ms > kMaximumPointerTimeMs || !std::isfinite(x) || !std::isfinite(y) ||
            std::abs(x) > static_cast<double>(std::numeric_limits<float>::max()) ||
            std::abs(y) > static_cast<double>(std::numeric_limits<float>::max()))
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "invalid pointer position or timestamp");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call(
            [&] { return wp->instance->pointer_move(x, y, pointer_time(timestamp_ms)); });
    }

    nekomote_error nekomote_wallpaper_pointer_enter(nekomote_wallpaper* wp, double x, double y)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->pointer_enter(x, y); });
    }

    nekomote_error nekomote_wallpaper_pointer_leave(nekomote_wallpaper* wp)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->pointer_leave(); });
    }

    nekomote_error nekomote_wallpaper_touch(nekomote_wallpaper* wp, float x, float y)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->touch(x, y); });
    }

    nekomote_error nekomote_wallpaper_mouse_button(nekomote_wallpaper* wp, int button,
                                                   nekomote_mouse_action action,
                                                   uint64_t timestamp_ms)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        if (timestamp_ms > kMaximumPointerTimeMs || button < 0 || button > 1 ||
            (action != NEKOMOTE_MOUSE_PRESS && action != NEKOMOTE_MOUSE_RELEASE))
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "invalid mouse button, action or timestamp");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call(
            [&]
            {
                return wp->instance->mouse_button(button, static_cast<int>(action),
                                                  pointer_time(timestamp_ms));
            });
    }

    nekomote_error nekomote_wallpaper_mouse_scroll(nekomote_wallpaper* wp, double amount)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->mouse_scroll(amount); });
    }

    nekomote_error nekomote_wallpaper_key(nekomote_wallpaper* wp, nekomote_key key,
                                          nekomote_mouse_action action)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call(
            [&] { return wp->instance->key(static_cast<int>(key), static_cast<int>(action)); });
    }

    nekomote_error nekomote_wallpaper_switch_character(nekomote_wallpaper* wp, int index)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->switch_character(index); });
    }

    nekomote_error nekomote_wallpaper_switch_background(nekomote_wallpaper* wp, int index)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->switch_background(index); });
    }

    nekomote_error nekomote_wallpaper_clear_background(nekomote_wallpaper* wp)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->clear_background(); });
    }

    nekomote_error nekomote_wallpaper_fit(nekomote_wallpaper* wp)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->fit(); });
    }

    nekomote_error nekomote_wallpaper_reset(nekomote_wallpaper* wp)
    {
        if (!wp || !wp->instance)
        {
            set_error(NEKOMOTE_ERROR_INVALID_INPUT, "null wallpaper");
            return NEKOMOTE_ERROR_INVALID_INPUT;
        }
        return wallpaper_call([&] { return wp->instance->reset(); });
    }

    int nekomote_wallpaper_character_count(const nekomote_wallpaper* wp)
    {
        return (wp && wp->instance) ? wp->instance->character_count() : 0;
    }

    const char* nekomote_wallpaper_character_name(const nekomote_wallpaper* wp, int index)
    {
        return (wp && wp->instance) ? wp->instance->character_name(index) : nullptr;
    }

    int nekomote_wallpaper_background_count(const nekomote_wallpaper* wp)
    {
        return (wp && wp->instance) ? wp->instance->background_count() : 0;
    }

    const char* nekomote_wallpaper_background_name(const nekomote_wallpaper* wp, int index)
    {
        return (wp && wp->instance) ? wp->instance->background_name(index) : nullptr;
    }

    int nekomote_wallpaper_current_character(const nekomote_wallpaper* wp)
    {
        return (wp && wp->instance) ? wp->instance->current_character() : -1;
    }

    int nekomote_wallpaper_current_background(const nekomote_wallpaper* wp)
    {
        return (wp && wp->instance) ? wp->instance->current_background() : -1;
    }

    const char* nekomote_wallpaper_current_character_name(const nekomote_wallpaper* wp)
    {
        return (wp && wp->instance) ? wp->instance->current_character_name() : nullptr;
    }

    int nekomote_wallpaper_has_background(const nekomote_wallpaper* wp)
    {
        return (wp && wp->instance) ? (wp->instance->has_background() ? 1 : 0) : 0;
    }

    int nekomote_wallpaper_paused(const nekomote_wallpaper* wp)
    {
        return (wp && wp->instance) ? (wp->instance->paused() ? 1 : 0) : 0;
    }

    nekomote_audio_state nekomote_wallpaper_audio_state(const nekomote_wallpaper* wp)
    {
        return (wp && wp->instance) ? static_cast<nekomote_audio_state>(wp->instance->audio_state())
                                    : NEKOMOTE_AUDIO_STATE_DISABLED;
    }

    float nekomote_wallpaper_audio_rms(const nekomote_wallpaper* wp)
    {
        return (wp && wp->instance) ? wp->instance->audio_rms() : 0.0f;
    }

    int nekomote_wallpaper_audio_error(const nekomote_wallpaper* wp)
    {
        return (wp && wp->instance) ? wp->instance->audio_error() : 0;
    }

} // extern "C"
