#include <nekomote/application/controller.hpp>
#include <nekomote/application/session.hpp>
#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/image.hpp>
#include <nekomote/asset_io/session_asset.hpp>
#include <nekomote/render_gl/renderer.hpp>
#include <nekomote/render_gl/ui_renderer.hpp>
#include <nekomote/ui/menu.hpp>

#include <EGL/egl.h>
#include <glad/gl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "baseline_ui.hpp"

thread_local bool tracking = false;
thread_local std::size_t allocations = 0;
void* operator new(std::size_t size)
{
    if (tracking)
        ++allocations;
    if (auto p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n)
{
    return ::operator new(n);
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}
std::size_t draws = 0, uploads = 0, bytes = 0;
PFNGLDRAWARRAYSPROC original_draw;
PFNGLBUFFERDATAPROC original_upload;
void GLAD_API_PTR counted_draw(GLenum a, GLint b, GLsizei c)
{
    ++draws;
    original_draw(a, b, c);
}
void GLAD_API_PTR counted_upload(GLenum a, GLsizeiptr b, const void* c, GLenum d)
{
    ++uploads;
    bytes += static_cast<std::size_t>(b);
    original_upload(a, b, c, d);
}
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
int main(int argc, char** argv)
{
    require(argc >= 4, "assets output-directory desktop|gles [capture]");
    require(argc >= 5, "assets output-directory desktop|gles model-relative-path");

    const bool gles = std::string(argv[3]) == "gles";
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    require(eglInitialize(display, nullptr, nullptr), "eglInitialize");
    require(eglBindAPI(gles ? EGL_OPENGL_ES_API : EGL_OPENGL_API), "eglBindAPI");
    const EGLint attrs[] = {EGL_SURFACE_TYPE,
                            EGL_PBUFFER_BIT,
                            EGL_RENDERABLE_TYPE,
                            gles ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_BIT,
                            EGL_RED_SIZE,
                            8,
                            EGL_GREEN_SIZE,
                            8,
                            EGL_BLUE_SIZE,
                            8,
                            EGL_ALPHA_SIZE,
                            8,
                            EGL_NONE};
    EGLConfig config;
    EGLint count;
    require(eglChooseConfig(display, attrs, &config, 1, &count) && count, "eglChooseConfig");
    const EGLint context_attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION,
                                    gles ? 0 : 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
    require(context != EGL_NO_CONTEXT, "eglCreateContext");
    const EGLint surface_attrs[] = {EGL_WIDTH, 800, EGL_HEIGHT, 1080, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surface_attrs);
    require(eglMakeCurrent(display, surface, surface, context), "eglMakeCurrent");
    {
        using namespace nekomote;
        render_gl::Renderer reader(eglGetProcAddress);
        auto get_query = reinterpret_cast<PFNGLGETQUERYOBJECTUI64VPROC>(
            eglGetProcAddress(gles ? "glGetQueryObjectui64vEXT" : "glGetQueryObjectui64v"));
        require(get_query != nullptr, "GPU timer extension");
        std::printf("vendor=%s renderer=%s version=%s\n", glGetString(GL_VENDOR),
                    glGetString(GL_RENDERER), glGetString(GL_VERSION));
        original_draw = glad_glDrawArrays;
        original_upload = glad_glBufferData;
        glad_glDrawArrays = counted_draw;
        glad_glBufferData = counted_upload;
        application::WallpaperController controller;
        controller.set_no_save(true);
        require(controller.scan_assets(argv[1]), "scan assets");
        auto catalog = controller.catalog();
        require(!catalog.character_groups.empty(), "catalog");
        auto largest = std::max_element(
            catalog.character_groups.begin(), catalog.character_groups.end(),
            [](const auto& a, const auto& b) { return a.items.size() < b.items.size(); });
        const int largest_index = static_cast<int>(largest - catalog.character_groups.begin());
        std::printf("catalog_groups=%zu largest_group=%s items=%zu\n",
                    catalog.character_groups.size(), largest->id.c_str(), largest->items.size());

        const auto bundle = std::filesystem::path(argv[1]) / "characters" / argv[4];
        auto model = asset_io::load_asset(bundle / "model.nkm");
        require(model.ok, "model");
        auto session_asset = asset_io::load_session_asset(bundle / "session.nks.json");
        require(session_asset.ok, "session asset");
        auto compiled = application::compile_session(model.asset, session_asset.asset);
        require(bool(compiled.program), "compile session");
        const auto cap = compiled.program->playback().scene().frame_capacity();
        auto old_renderer = reader.create_peer();
        auto new_renderer = reader.create_peer();
        for (auto* renderer : {&old_renderer, &new_renderer})
        {
            renderer->reserve({cap.vertices, cap.indices, cap.commands,
                               compiled.program->playback().scene().textures().size(),
                               cap.mask_commands, cap.masks});
            for (const auto& texture : compiled.program->playback().scene().textures())
            {
                auto image = asset_io::load_cropped_png(bundle / texture.path);
                require(image.ok, "texture");
                renderer->upload_texture(texture.id, image.image, image.region);
            }
        }
        application::SessionConfig options;
        options.animation.random_seed = 1;
        options.reaction_seed = 1;
        std::printf("model=%s fixed_step=1/60 resolution=800x1080 warmup=100 sample_pairs=120\n",
                    argv[4]);

        baseline_render_gl::UiRenderer baseline;
        render_gl::UiRenderer optimized;
        require(baseline.init(800, 1080) && baseline.init_font("", 36), "baseline init");
        require(optimized.init(800, 1080) && optimized.init_font("", 36), "optimized init");
        require(ui::prepare_menu_text(*baseline.font(), catalog), "baseline font");
        require(ui::prepare_menu_text(*optimized.font(), catalog), "optimized font");
        const auto output = std::filesystem::path(argv[2]);
        std::filesystem::create_directories(output);
        for (const std::string label : {"icon", "collapsed", "characters-top", "characters-partial",
                                        "characters-bottom", "settings-top", "settings-bottom"})
        {
            struct State
            {
                ui::Context layout;
                ui::MenuState menu;
                ui::IconState icon;
                std::unique_ptr<application::SessionInstance> session;
                render_gl::Renderer* renderer;
            };
            State a, b;
            const auto init = [&](State& state, ui::Painter& painter)
            {
                state.layout.init(&painter);
                state.layout.mouse_move(340, 303);
                state.menu.sidebar.open = true;
                state.menu.sidebar.animation = 1;
                state.menu.sidebar.x = 300;
                state.menu.sidebar.y = 8;
                state.menu.sidebar.is_free = true;
                state.icon.initialized = true;
                state.icon.x = 732;
                state.icon.y = 8;
            };
            init(a, baseline);
            init(b, optimized);
            for (auto* state : {&a, &b})
            {
                state->session =
                    std::make_unique<application::SessionInstance>(*compiled.program, options);
                require(bool(state->session->start()), "session start");
            }
            a.renderer = &old_renderer;
            b.renderer = &new_renderer;
            for (auto* state : {&a, &b})
            {
                require(bool(state->session->prepare_frame()), "initial frame");
                state->renderer->prepare(state->session->frame());
                state->renderer->resize_output(800, 1080);
            }
            for (auto* state : {&a, &b})
            {
                if (label.starts_with("characters"))
                    state->menu.sidebar.expanded_drawer = largest_index;
                if (label == "characters-partial")
                    state->menu.sidebar.scroll_y = 17.5f;
                if (label.starts_with("settings"))
                    state->menu.sidebar.expanded_drawer =
                        static_cast<int>(catalog.character_groups.size()) + 1;
                if (label.ends_with("bottom"))
                    state->menu.sidebar.scroll_y = 1000000;
            }
            const auto frame = [&](State& state, ui::Painter& painter)
            {
                glDisable(GL_SCISSOR_TEST);
                glClearColor(.17f, .23f, .31f, .8f);
                glClear(GL_COLOR_BUFFER_BIT);
                require(bool(state.session->advance(1.0 / 60)), "advance");
                require(bool(state.session->prepare_frame()), "frame");
                state.renderer->prepare(state.session->frame());
                state.renderer->render({0, 800, 1080}, {400, 800, .7f, {.17f, .23f, .31f, .8f}});
                state.layout.begin_frame();
                if (label == "icon")
                    state.layout.toggle_icon(state.icon, 0);
                else
                {
                    state.layout.sidebar(state.menu.sidebar);
                    ui::build_sidebar(state.layout, painter, catalog, controller, state.menu, 1080);
                }
                state.layout.end_frame();
                painter.end_frame();
            };
            for (int i = 0; i < 100; ++i)
            {
                frame(a, baseline);
                frame(b, optimized);
            }
            glFinish();
            constexpr int count = 120;
            std::vector<double> old_times(count), new_times(count), differences(count);
            std::vector<GLuint> queries(count * 2);
            glGenQueries(count * 2, queries.data());
            auto next = std::chrono::steady_clock::now();
            const auto sample = [&](State& state, ui::Painter& painter)
            {
                auto t = std::chrono::steady_clock::now();
                frame(state, painter);
                return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() -
                                                                 t)
                    .count();
            };
            allocations = 0;
            tracking = true;
            for (int i = 0; i < count; ++i)
            {
                const auto timed = [&](State& state, ui::Painter& painter, GLuint query)
                {
                    glBeginQuery(GL_TIME_ELAPSED, query);
                    auto value = sample(state, painter);
                    glEndQuery(GL_TIME_ELAPSED);
                    return value;
                };
                if (i % 2 == 0)
                {
                    old_times[i] = timed(a, baseline, queries[i * 2]);
                    new_times[i] = timed(b, optimized, queries[i * 2 + 1]);
                }
                else
                {
                    new_times[i] = timed(b, optimized, queries[i * 2 + 1]);
                    old_times[i] = timed(a, baseline, queries[i * 2]);
                }
                glFlush();
                next += std::chrono::microseconds(16667);
                std::this_thread::sleep_until(next);
            }
            tracking = false;
            glFinish();
            std::vector<double> old_gpu(count), new_gpu(count);
            for (int i = 0; i < count; ++i)
            {
                GLuint64 old_ns, new_ns;
                get_query(queries[i * 2], GL_QUERY_RESULT, &old_ns);
                get_query(queries[i * 2 + 1], GL_QUERY_RESULT, &new_ns);
                old_gpu[i] = static_cast<double>(old_ns) / 1000;
                new_gpu[i] = static_cast<double>(new_ns) / 1000;
            }
            glDeleteQueries(count * 2, queries.data());
            std::ofstream csv(output / (label + ".csv"));
            csv << "baseline_us,optimized_us,baseline_gpu_us,optimized_gpu_us\n";
            for (int i = 0; i < count; ++i)
            {
                csv << old_times[i] << ',' << new_times[i] << ',' << old_gpu[i] << ',' << new_gpu[i]
                    << '\n';
                differences[i] = new_times[i] - old_times[i];
            }
            std::sort(old_times.begin(), old_times.end());
            std::sort(new_times.begin(), new_times.end());
            std::sort(differences.begin(), differences.end());
            std::printf("%s baseline_us=%.3f/%.3f/%.3f optimized_us=%.3f/%.3f/%.3f "
                        "paired_delta_us_p50=%.3f allocations=%zu\n",
                        label.c_str(), old_times.at(count / 2), old_times.at(count * 95 / 100),
                        old_times.at(count * 99 / 100), new_times.at(count / 2),
                        new_times.at(count * 95 / 100), new_times.at(count * 99 / 100),
                        differences.at(count / 2), allocations);
            require(glGetError() == GL_NO_ERROR, "GL error");
        }
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);
}
