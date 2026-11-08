#include <nekomote/application/session.hpp>
#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/image.hpp>
#include <nekomote/asset_io/session_asset.hpp>
#include <nekomote/render_gl/renderer.hpp>

#include <EGL/egl.h>
#include <glad/gl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
using namespace nekomote;
using Clock = std::chrono::steady_clock;
void require(bool v, const char* m)
{
    if (!v)
        throw std::runtime_error(m);
}
PFNGLBUFFERSUBDATAPROC original_upload;
std::size_t uploaded_indices = 0, uploaded_vertices = 0, index_calls = 0;
void counted_upload(GLenum target, GLintptr offset, GLsizeiptr size, const void* data)
{
    if (target == GL_ELEMENT_ARRAY_BUFFER)
    {
        uploaded_indices += size;
        ++index_calls;
    }
    else if (target == GL_ARRAY_BUFFER)
        uploaded_vertices += size;
    original_upload(target, offset, size, data);
}
render_gl::GlProc loader(const char* name)
{
    auto result = eglGetProcAddress(name);
    if (std::strcmp(name, "glBufferSubData") == 0)
    {
        original_upload = reinterpret_cast<PFNGLBUFFERSUBDATAPROC>(result);
        return reinterpret_cast<render_gl::GlProc>(counted_upload);
    }
    return result;
}
void stats(const char* label, std::vector<double> data)
{
    double sum = 0;
    for (auto x : data)
        sum += x;
    std::sort(data.begin(), data.end());
    auto n = data.size();
    std::printf("\"%s\":{\"mean\":%.3f,\"p50\":%.3f,\"p95\":%.3f,\"p99\":%.3f}", label,
                sum / static_cast<double>(n), data[n / 2], data[n * 95 / 100], data[n * 99 / 100]);
}
int main(int argc, char** argv)
{
    require(argc >= 6, "assets desktop|gles samples image-directory reuse|upload");
    bool reuse = std::string(argv[5]) == "reuse";
    bool gles = std::string(argv[2]) == "gles";
    int samples = std::stoi(argv[3]);
    auto display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    require(eglInitialize(display, nullptr, nullptr), "display");
    require(eglBindAPI(gles ? EGL_OPENGL_ES_API : EGL_OPENGL_API), "api");
    EGLint attrs[] = {EGL_SURFACE_TYPE,
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
    require(eglChooseConfig(display, attrs, &config, 1, &count) && count, "config");
    EGLint ca[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, gles ? 0 : 3, EGL_NONE};
    auto context = eglCreateContext(display, config, EGL_NO_CONTEXT, ca);
    require(context != EGL_NO_CONTEXT, "context");
    EGLint sa[] = {EGL_WIDTH, 1280, EGL_HEIGHT, 720, EGL_NONE};
    auto surface = eglCreatePbufferSurface(display, config, sa);
    require(eglMakeCurrent(display, surface, surface, context), "current");
    {
        render_gl::Renderer shared(loader);
        std::fprintf(stderr, "%s %s %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER),
                     glGetString(GL_VERSION));
        std::filesystem::create_directories(argv[4]);
        for (const char* name : {"chocola/date-a", "chocola/wintermaid", "maple/yukata"})
        {
            auto bundle = std::filesystem::path(argv[1]) / "characters" / name;
            auto model = asset_io::load_asset(bundle / "model.nkm");
            require(model.ok, "model");
            auto asset = asset_io::load_session_asset(bundle / "session.nks.json");
            require(asset.ok, "session");
            auto compiled = application::compile_session(model.asset, asset.asset);
            require(bool(compiled.program), "compile");
            for (bool moving : {false, true})
            {
                application::SessionConfig options;
                options.animation.random_seed = 12345;
                options.reaction_seed = 12345;
                application::SessionInstance session(*compiled.program, options);
                require(bool(session.start()), "start");
                auto renderer = shared.create_peer();
                const auto& scene = compiled.program->playback().scene();
                auto cap = scene.frame_capacity();
                renderer.reserve({cap.vertices, cap.indices, cap.commands, scene.textures().size(),
                                  cap.mask_commands, cap.masks});
                for (auto t : scene.textures())
                {
                    auto img = asset_io::load_cropped_png(bundle / t.path);
                    require(img.ok, "image");
                    renderer.upload_texture(t.id, img.image, img.region);
                }
                renderer.resize_output(1280, 720);
                std::vector<double> prepares(samples), cpu(samples), complete(samples);
                uploaded_indices = uploaded_vertices = index_calls = 0;
                for (int i = -120; i < samples; ++i)
                {
                    if (moving)
                        require(bool(session.drag_origin(
                                    {float((i + 120) % 80 - 40), float((i + 120) % 40 - 20)})),
                                "drag");
                    auto a = Clock::now();
                    require(bool(session.advance(1.0 / 60)), "advance");
                    require(bool(session.prepare_frame()), "frame");
                    auto b = Clock::now();
                    auto frame = session.frame();
                    if (!reuse)
                        frame.index_revision = 0;
                    renderer.prepare(frame);
                    renderer.resize_output(1280, 720);
                    auto c = Clock::now();
                    renderer.render({0, 1280, 720}, {640, 400, .65f, {0, 0, 0, 0}});
                    auto fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                    require(fence != nullptr, "fence");
                    glFlush();
                    auto d = Clock::now();
                    auto ready = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ULL);
                    require(ready == GL_ALREADY_SIGNALED || ready == GL_CONDITION_SATISFIED,
                            "wait");
                    auto e = Clock::now();
                    glDeleteSync(fence);
                    if (i == -1)
                        uploaded_indices = uploaded_vertices = index_calls = 0;
                    if (i >= 0)
                    {
                        prepares[i] = std::chrono::duration<double, std::micro>(c - b).count();
                        cpu[i] = std::chrono::duration<double, std::micro>(d - a).count();
                        complete[i] = std::chrono::duration<double, std::micro>(e - a).count();
                    }
                }
                std::string label = name;
                std::replace(label.begin(), label.end(), '/', '-');
                auto image = renderer.capture({0, 1280, 720});
                require(asset_io::write_png(std::filesystem::path(argv[4]) /
                                                (label + (moving ? "-moving.png" : "-idle.png")),
                                            image)
                            .empty(),
                        "png");
                std::printf("{\"model\":\"%s\",\"moving\":%s,\"api\":\"%s\",\"samples\":%d,\"index_"
                            "calls\":%zu,\"index_bytes\":%zu,\"vertex_bytes\":%zu,",
                            name, moving ? "true" : "false", argv[2], samples, index_calls,
                            uploaded_indices, uploaded_vertices);
                stats("prepare_us", prepares);
                std::printf(",");
                stats("cpu_us", cpu);
                std::printf(",");
                stats("complete_us", complete);
                std::puts("}");
                std::fflush(stdout);
            }
        }
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);
}
