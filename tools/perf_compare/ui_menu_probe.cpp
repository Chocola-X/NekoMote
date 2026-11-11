#include <nekomote/render_gl/ui_renderer.hpp>
#include <nekomote/ui/menu.hpp>

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

// UI submission only: no model evaluation, swaps or GPU completion timing.
// Run every candidate serially; pixel readback and draw counting are untimed.
namespace
{
PFNGLDRAWARRAYSPROC original_draw;
unsigned draw_calls = 0;
void APIENTRY count_draw(GLenum mode, GLint first, GLsizei count)
{
    ++draw_calls;
    original_draw(mode, first, count);
}
} // namespace
int main(int argc, char** argv)
{
    if (argc < 3 || !glfwInit())
        return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* w = glfwCreateWindow(800, 1080, "UI audit", nullptr, nullptr);
    if (!w)
        return 2;
    glfwMakeContextCurrent(w);
    if (!gladLoadGL(glfwGetProcAddress))
        return 3;
    original_draw = glad_glDrawArrays;
    std::printf("renderer=%s version=%s glfw_platform=%d\n", glGetString(GL_RENDERER),
                glGetString(GL_VERSION), glfwGetPlatform());
    {
        nekomote::render_gl::UiRenderer painter;
        if (!painter.init(800, 1080) || !painter.init_font("", 36))
            return 4;
        auto catalog = nekomote::application::AssetCatalog::scan(argv[1]);
        if (catalog.character_groups.empty())
            return 6;
        std::printf("character_groups=%zu warmup=300 samples=1200 viewport=800x1080\n",
                    catalog.character_groups.size());
        nekomote::application::WallpaperController controller;
        controller.set_no_save(true);
        if (!nekomote::ui::prepare_menu_text(*painter.font(), catalog))
            return 5;
        nekomote::ui::Context context;
        context.init(&painter, {});
        for (int mode = 0; mode < 5; ++mode)
        {
            nekomote::ui::MenuState menu;
            menu.sidebar.open = true;
            menu.sidebar.animation = mode == 0 ? 0 : 1;
            menu.sidebar.expanded_drawer = mode == 2   ? 0
                                           : mode == 3 ? int(catalog.character_groups.size())
                                           : mode == 4 ? int(catalog.character_groups.size() + 1)
                                                       : -1;
            auto frame = [&]
            {
                draw_calls = 0;
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);
                context.begin_frame();
                nekomote::ui::build_sidebar(context, painter, catalog, controller, menu, 1080);
                context.end_frame();
                painter.end_frame();
                glFlush();
            };
            for (int i = 0; i < 300; ++i)
                frame();
            std::vector<double> times;
            times.reserve(1200);
            const auto cpu_start = std::clock();
            for (int i = 0; i < 1200; ++i)
            {
                auto a = std::chrono::steady_clock::now();
                frame();
                times.push_back(
                    std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - a)
                        .count());
            }
            const double cpu_us = static_cast<double>(std::clock() - cpu_start) * 1e6 /
                                  static_cast<double>(CLOCKS_PER_SEC) / 1200.0;
            std::sort(times.begin(), times.end());
            double mean = 0;
            for (auto t : times)
                mean += t;
            std::printf("scene=%d mean_us=%.3f p50_us=%.3f p95_us=%.3f p99_us=%.3f cpu_us=%.3f\n",
                        mode, mean / static_cast<double>(times.size()), times[600], times[1140],
                        times[1188], cpu_us);
            glad_glDrawArrays = count_draw;
            frame();
            std::printf("draw_calls=%u\n", draw_calls);
            glad_glDrawArrays = original_draw;
            std::vector<unsigned char> pixels(800 * 1080 * 4);
            glReadPixels(0, 0, 800, 1080, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            std::ofstream out(std::string(argv[2]) + "-" + std::to_string(mode) + ".rgba",
                              std::ios::binary);
            out.write(reinterpret_cast<char*>(pixels.data()),
                      static_cast<std::streamsize>(pixels.size()));
            if (!out || glGetError() != GL_NO_ERROR)
                return 7;
        }
    }
    glfwDestroyWindow(w);
    glfwTerminate();
    return 0;
}
