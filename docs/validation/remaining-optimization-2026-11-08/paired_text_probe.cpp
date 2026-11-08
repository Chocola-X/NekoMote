#include <nekomote/application/controller.hpp>
#include <nekomote/asset_io/image.hpp>
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
    const bool capture = argc > 4;
    const bool paced = std::getenv("NEKOMOTE_BENCH_PACED") != nullptr;
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
                if (i % 2 == 0)
                {
                    old_times[i] = sample(a, baseline);
                    new_times[i] = sample(b, optimized);
                }
                else
                {
                    new_times[i] = sample(b, optimized);
                    old_times[i] = sample(a, baseline);
                }
                glFlush();
                next += std::chrono::microseconds(16667);
                std::this_thread::sleep_until(next);
            }
            tracking = false;
            glFinish();
            std::ofstream csv(output / (label + ".csv"));
            csv << "baseline_us,optimized_us\n";
            for (int i = 0; i < count; ++i)
            {
                csv << old_times[i] << ',' << new_times[i] << '\n';
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
        }
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);
}
