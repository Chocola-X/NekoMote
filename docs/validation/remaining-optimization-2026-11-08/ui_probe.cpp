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
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

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
        render_gl::UiRenderer ui;
        require(ui.init(800, 1080) && ui.init_font("", 36), "UI init");
        require(ui::prepare_menu_text(*ui.font(), catalog), "font prepare");
        ui::Context layout;
        layout.init(&ui);
        layout.mouse_move(340, 303);
        std::filesystem::create_directories(argv[2]);
        const auto output = std::filesystem::path(argv[2]);
        struct Case
        {
            const char* name;
            int drawer;
            float scroll;
            bool icon;
        };
        const std::vector<Case> cases = {
            {"icon", -1, 0, true},
            {"collapsed", -1, 0, false},
            {"characters-top", largest_index, 0, false},
            {"characters-partial", largest_index, 17.5f, false},
            {"characters-bottom", largest_index, 1000000, false},
            {"settings-top", static_cast<int>(catalog.character_groups.size()) + 1, 0, false},
            {"settings-bottom", static_cast<int>(catalog.character_groups.size()) + 1, 1000000,
             false}};
        for (const auto& c : cases)
        {
            ui::MenuState menu;
            menu.sidebar.open = true;
            menu.sidebar.animation = 1;
            menu.sidebar.x = 300;
            menu.sidebar.y = 8;
            menu.sidebar.is_free = true;
            menu.sidebar.expanded_drawer = c.drawer;
            menu.sidebar.scroll_y = c.scroll;
            ui::IconState icon;
            icon.initialized = true;
            icon.x = 732;
            icon.y = 8;
            const auto frame = [&]
            {
                glDisable(GL_SCISSOR_TEST);
                glClearColor(0.17f, 0.23f, 0.31f, 0.8f);
                glClear(GL_COLOR_BUFFER_BIT);
                layout.begin_frame();
                if (c.icon)
                    layout.toggle_icon(icon, 0);
                else
                {
                    layout.sidebar(menu.sidebar);
                    ui::build_sidebar(layout, ui, catalog, controller, menu, 1080);
                }
                layout.end_frame();
                ui.end_frame();
            };
            for (int i = 0; i < 200; ++i)
                frame();
            glFinish();
            if (capture)
            {
                const auto image = reader.capture({0, 800, 1080});
                require(asset_io::write_png(output / (std::string(c.name) + ".png"), image).empty(),
                        "write png");
            }
            const int samples = paced ? 120 : 1000;
            std::vector<GLuint> queries(samples);
            glGenQueries(samples, queries.data());
            std::vector<double> cpu;
            cpu.reserve(samples);
            draws = uploads = bytes = allocations = 0;
            auto next = std::chrono::steady_clock::now();
            tracking = true;
            for (int i = 0; i < samples; ++i)
            {
                glBeginQuery(GL_TIME_ELAPSED, queries[static_cast<std::size_t>(i)]);
                auto start = std::chrono::steady_clock::now();
                frame();
                cpu.push_back(std::chrono::duration<double, std::micro>(
                                  std::chrono::steady_clock::now() - start)
                                  .count());
                glEndQuery(GL_TIME_ELAPSED);
                if (paced)
                {
                    glFlush();
                    next += std::chrono::microseconds(16667);
                    std::this_thread::sleep_until(next);
                }
            }
            tracking = false;
            glFinish();
            std::vector<double> gpu;
            gpu.reserve(samples);
            for (auto q : queries)
            {
                GLuint64 ns;
                get_query(q, GL_QUERY_RESULT, &ns);
                gpu.push_back(static_cast<double>(ns) / 1000);
            }
            glDeleteQueries(samples, queries.data());
            std::sort(cpu.begin(), cpu.end());
            std::sort(gpu.begin(), gpu.end());
            std::printf("%s cpu_us=%.3f/%.3f/%.3f gpu_us=%.3f/%.3f/%.3f draw=%zu uploads=%zu "
                        "bytes=%zu allocations=%zu\n",
                        c.name, cpu.at(samples / 2), cpu.at(samples * 95 / 100),
                        cpu.at(samples * 99 / 100), gpu.at(samples / 2), gpu.at(samples * 95 / 100),
                        gpu.at(samples * 99 / 100), draws / samples, uploads / samples,
                        bytes / samples, allocations);
            require(glGetError() == GL_NO_ERROR, "GL error");
        }
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);
}
