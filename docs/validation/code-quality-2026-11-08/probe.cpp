#include <nekomote/application/controller.hpp>
#include <nekomote/application/session.hpp>
#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/image.hpp>
#include <nekomote/asset_io/session_asset.hpp>
#include <nekomote/render_gl/renderer.hpp>
#include <nekomote/runtime/geometry_scene.hpp>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <nekomote/abi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace nekomote;
using Clock = std::chrono::steady_clock;
void require(bool b, const char* s)
{
    if (!b)
        throw std::runtime_error(s);
}
void check(nekomote_error e)
{
    require(e == NEKOMOTE_OK, nekomote_error_message());
}
template <class F>
void bench(const char* name, int n, F f)
{
    std::vector<double> times;
    for (int i = 0; i < n + 5; ++i)
    {
        auto t = Clock::now();
        f();
        double us = std::chrono::duration<double, std::micro>(Clock::now() - t).count();
        if (i >= 5)
            times.push_back(us);
    }
    std::sort(times.begin(), times.end());
    std::printf("%s n=%d p50_us=%.3f p95_us=%.3f p99_us=%.3f\n", name, n, times[n / 2],
                times[int(n * .95)], times[int(n * .99)]);
}
int main(int argc, char** argv)
{
    try
    {
        auto getdisplay =
            (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
        auto display = getdisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
        require(eglInitialize(display, nullptr, nullptr), "eglInitialize");
        require(eglBindAPI(EGL_OPENGL_API), "eglBindAPI");
        EGLint attrs[] = {EGL_SURFACE_TYPE,
                          EGL_PBUFFER_BIT,
                          EGL_RENDERABLE_TYPE,
                          EGL_OPENGL_BIT,
                          EGL_RED_SIZE,
                          8,
                          EGL_GREEN_SIZE,
                          8,
                          EGL_BLUE_SIZE,
                          8,
                          EGL_ALPHA_SIZE,
                          8,
                          EGL_NONE};
        EGLConfig cfg;
        EGLint count;
        require(eglChooseConfig(display, attrs, &cfg, 1, &count) && count, "eglChooseConfig");
        EGLint ca[] = {EGL_CONTEXT_MAJOR_VERSION,
                       3,
                       EGL_CONTEXT_MINOR_VERSION,
                       3,
                       EGL_CONTEXT_OPENGL_PROFILE_MASK,
                       EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                       EGL_NONE};
        auto ctx = eglCreateContext(display, cfg, EGL_NO_CONTEXT, ca);
        require(ctx != EGL_NO_CONTEXT, "eglCreateContext");
        EGLint pa[] = {EGL_WIDTH, 128, EGL_HEIGHT, 128, EGL_NONE};
        auto surface = eglCreatePbufferSurface(display, cfg, pa);
        require(eglMakeCurrent(display, surface, surface, ctx), "eglMakeCurrent");
        auto getstr = (const unsigned char* (*)(unsigned))eglGetProcAddress("glGetString");
        std::printf("GL %s | %s\n", getstr(0x1F00), getstr(0x1F01));
        auto root = std::filesystem::path(argv[1]);
        auto bundle = root / (argc > 2 ? argv[2] : "characters/chocola/date-a");
        auto model = asset_io::load_asset(bundle / "model.nkm");
        require(model.ok, "load model");
        auto session = asset_io::load_session_asset(bundle / "session.nks.json");
        require(session.ok, "load session");
        auto compile = [&]
        {
            auto p = application::compile_session(model.asset, session.asset);
            require(bool(p.program), "compile session");
        };
        bench("session_new", 40, compile);
        bench("session_duplicate_geometry_simulated", 40,
              [&]
              {
                  {
                      auto g = runtime::compile_geometry_scene(model.asset);
                      require(bool(g.scene), "geometry");
                  }
                  compile();
              });
        auto scene = runtime::compile_geometry_scene(model.asset);
        require(bool(scene.scene), "compile geometry");
        runtime::GeometryInstance first(*scene.scene), second(*scene.scene);
        require(bool(first.evaluate()) && bool(first.build_frame()), "frame1");
        require(bool(second.set_origin({100, 0})) && bool(second.evaluate()) &&
                    bool(second.build_frame()),
                "frame2");
        auto frame = first.frame();
        auto other = second.frame();
        std::printf("frame vertices=%zu indices=%zu commands=%zu maskcmd=%zu masks=%zu "
                    "rev=%llu/%llu first_x=%f second_x=%f upload_bytes=%zu\n",
                    frame.vertices.size(), frame.indices.size(), frame.commands.size(),
                    frame.mask_commands.size(), frame.masks.size(),
                    (unsigned long long)frame.revision, (unsigned long long)other.revision,
                    frame.vertices[0].x, other.vertices[0].x,
                    frame.vertices.size() * sizeof(RenderVertex) + frame.indices.size() * 4);
        render_gl::Renderer renderer(eglGetProcAddress);
        renderer.reserve({frame.vertices.size(), frame.indices.size(), frame.commands.size(),
                          scene.scene->textures().size(), frame.mask_commands.size(),
                          frame.masks.size()});
        size_t raw = 0, padded = 0;
        for (auto& t : scene.scene->textures())
        {
            auto img = asset_io::load_cropped_png(bundle / t.path);
            require(img.ok, "image");
            renderer.upload_texture(t.id, img.image, img.region);
            auto pow2 = [](int x)
            {
                int n = 1;
                while (n < x)
                    n *= 2;
                return n;
            };
            raw += size_t(img.image.width) * img.image.height * 4;
            padded += size_t(pow2(img.image.width)) * pow2(img.image.height) * 4;
        }
        std::printf("atlas raw=%zu GLES_padded=%zu\n", raw, padded);
        renderer.prepare(frame);
        bench("prepare_cached", 1000, [&] { renderer.prepare(frame); });
        auto zero = frame;
        zero.revision = 0;
        bench("prepare_uncached_same_content", 1000, [&] { renderer.prepare(zero); });
        // A renderer is permitted to consume two independent GeometryInstances.
        renderer.prepare(frame);
        renderer.prepare(other);
        float gpu_x = 0;
        auto readbuffer =
            (void (*)(unsigned, long, long, void*))eglGetProcAddress("glGetBufferSubData");
        readbuffer(0x8892, 0, sizeof(float), &gpu_x);
        std::printf("cross_instance_gpu_x=%f expected=%f (first=%f)\n", gpu_x, other.vertices[0].x,
                    frame.vertices[0].x);
        auto invalid = other;
        invalid.composition = static_cast<ColorComposition>(99);
        bool rejected = false;
        try
        {
            renderer.prepare(invalid);
        }
        catch (...)
        {
            rejected = true;
        }
        std::printf(
            "cross_instance_revision_collision_invalid_composition_rejected=%d (expected 1)\n",
            rejected);
        // Direct Over is not equivalent even without protection or Multiply.
        {
            render_gl::Renderer r(eglGetProcAddress);
            r.reserve({4, 6, 1, 1});
            std::array<std::uint8_t, 4> white{255, 255, 255, 255};
            r.upload_texture(TextureId{1}, {1, 1, white});
            std::array<RenderVertex, 4> v{
                {{-32, -32, 0, 0}, {32, -32, 1, 0}, {32, 32, 1, 1}, {-32, 32, 0, 1}}};
            for (auto& vertex : v)
                vertex.color[3] = .5f;
            std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
            std::array<DrawCommand, 1> commands{{{TextureId{1}, 0, 6}}};
            RenderFrame direct{v, indices, commands};
            render_gl::View view{64, 64, 1, {0, 0, 0, 0}};
            r.prepare(direct);
            r.render({0, 128, 128}, view);
            auto a = r.capture({0, 128, 128});
            direct.composition = ColorComposition::ProtectedStraight;
            r.prepare(direct);
            r.resize_output(128, 128);
            r.render({0, 128, 128}, view);
            auto b = r.capture({0, 128, 128});
            const auto offset = (64 * 128 + 64) * 4;
            std::printf("half_alpha_white direct_RGBA=%u,%u,%u,%u protected_RGBA=%u,%u,%u,%u\n",
                        a.pixels[offset], a.pixels[offset + 1], a.pixels[offset + 2],
                        a.pixels[offset + 3], b.pixels[offset], b.pixels[offset + 1],
                        b.pixels[offset + 2], b.pixels[offset + 3]);
        }
        // Reproduce open sidebar keeping a paused instance permanently dirty.
        auto wp = nekomote_wallpaper_create(eglGetProcAddress);
        require(wp, "wp");
        check(nekomote_wallpaper_set_no_save(wp, 1));
        check(
            nekomote_wallpaper_set_config_path(wp, "/tmp/nekomote-review/nonexistent-panel.json"));
        check(nekomote_wallpaper_set_output_size(wp, 128, 128));
        check(nekomote_wallpaper_set_muted(wp, 1));
        check(nekomote_wallpaper_set_assets_dir(wp, root.c_str()));
        check(nekomote_wallpaper_load_initial(wp));
        check(nekomote_wallpaper_set_ui_open(wp, 1));
        for (int i = 0; i < 3; ++i)
        {
            check(nekomote_wallpaper_advance(wp, 0));
            check(nekomote_wallpaper_render_frame(wp));
            check(nekomote_wallpaper_render_ui(wp, .25f));
        }
        check(nekomote_wallpaper_set_paused(wp, 1));
        for (int i = 0; i < 3; ++i)
        {
            check(nekomote_wallpaper_advance(wp, 0));
            check(nekomote_wallpaper_render_frame(wp));
            check(nekomote_wallpaper_render_ui(wp, .016f));
            std::printf("paused_open_sidebar frame=%d needs_render=%d\n", i,
                        nekomote_wallpaper_needs_render(wp));
        }
        check(nekomote_wallpaper_set_ui_open(wp, 0));
        check(nekomote_wallpaper_advance(wp, 0));
        check(nekomote_wallpaper_render_frame(wp));
        check(nekomote_wallpaper_render_ui(wp, .016f));
        std::printf("paused_closed_sidebar needs_render=%d\n", nekomote_wallpaper_needs_render(wp));
        nekomote_wallpaper_destroy(wp);
        application::WallpaperController c;
        c.set_no_save(true);
        require(c.scan_assets(root), "scan");
        application::ControllerConfig external;
        external.audio = application::AudioMode::Device;
        auto initial = c.load_initial(0, external);
        require(initial.ok, "initial cpu");
        c.install_initial(std::move(initial));
        external.audio = application::AudioMode::Disabled;
        c.request_switch_character(1);
        std::optional<application::CandidateCpu> candidate;
        auto deadline = Clock::now() + std::chrono::seconds(10);
        while (!candidate && Clock::now() < deadline)
        {
            candidate = c.poll_candidate();
            std::this_thread::yield();
        }
        require(candidate && candidate->ok, "async cpu");
        size_t pcm = 0;
        for (auto& clip : candidate->audio_clips)
            pcm += clip.samples.size() * sizeof(float);
        std::printf("external_audio_disabled_but_async_bank_clips=%zu pcm_bytes=%zu\n",
                    candidate->audio_clips.size(), pcm);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "ERROR %s\n", e.what());
        return 1;
    }
}
