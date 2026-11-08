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
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
int main(int argc, char** argv)
{
    require(argc >= 4, "assets output-directory desktop|gles [capture]");

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
        render_gl::Renderer loader(eglGetProcAddress);
        std::printf("vendor=%s renderer=%s version=%s\n", glGetString(GL_VENDOR),
                    glGetString(GL_RENDERER), glGetString(GL_VERSION));
        const auto output = std::filesystem::path(argv[2]);
        std::filesystem::create_directories(output);
        for (const auto name : {"chocola/date-a", "chocola/wintermaid", "maple/yukata"})
        {
            const auto bundle = std::filesystem::path(argv[1]) / "characters" / name;
            auto model = asset_io::load_asset(bundle / "model.nkm");
            require(model.ok, "model");
            auto asset = asset_io::load_session_asset(bundle / "session.nks.json");
            require(asset.ok, "session");
            auto compiled = application::compile_session(model.asset, asset.asset);
            require(bool(compiled.program), "compile");
            application::SessionConfig options;
            options.animation.random_seed = 1;
            options.reaction_seed = 1;
            application::SessionInstance instance(*compiled.program, options);
            require(bool(instance.start()), "start");
            require(bool(instance.prepare_frame()), "prepare");
            auto frame = instance.frame();
            auto renderer = loader.create_peer();
            const auto& scene = compiled.program->playback().scene();
            const auto cap = scene.frame_capacity();
            renderer.reserve({cap.vertices, cap.indices, cap.commands, scene.textures().size(),
                              cap.mask_commands, cap.masks});
            std::size_t raw = 0, padded = 0;
            for (const auto& t : scene.textures())
            {
                auto image = asset_io::load_cropped_png(bundle / t.path);
                require(image.ok, "image");
                auto power = [](int n)
                {
                    int p = 1;
                    while (p < n)
                        p *= 2;
                    return p;
                };
                raw += std::size_t(image.image.width) * image.image.height * 4;
                padded += std::size_t(power(image.image.width)) * power(image.image.height) * 4;
                renderer.upload_texture(t.id, image.image, image.region);
            }
            std::printf("%s atlas_raw=%zu atlas_padded=%zu\n", name, raw, padded);
            renderer.prepare(frame);
            renderer.resize_output(800, 1080);
            int previous = 0;
            for (int step : {0, 60, 180})
            {
                for (; previous < step; ++previous)
                    require(bool(instance.advance(1.0 / 60)), "advance");
                require(bool(instance.prepare_frame()), "frame");
                renderer.prepare(instance.frame());
                for (int variant = 0; variant < 3; ++variant)
                {
                    render_gl::View view{400, 540, variant == 0 ? .7f : .713f, {0, 0, 0, 0}};
                    view.flip_x = variant == 1;
                    view.alpha = variant == 2 ? .6f : 1;
                    renderer.render({0, 800, 1080}, view);
                    auto image = renderer.capture({0, 800, 1080});
                    std::string label = name;
                    std::replace(label.begin(), label.end(), '/', '-');
                    require(asset_io::write_png(output / (label + "-" + std::to_string(step) + "-" +
                                                          std::to_string(variant) + ".png"),
                                                image)
                                .empty(),
                            "png");
                }
            }
        }
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);
}
