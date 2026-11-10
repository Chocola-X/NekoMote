#include <sys/resource.h>

#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QOpenGLContext>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

#include "wallpaper_item.hpp"

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void wait_for(const std::function<bool()>& ready)
{
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(5);
    QObject::connect(&poll, &QTimer::timeout, &loop,
                     [&]
                     {
                         if (ready())
                             loop.quit();
                     });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    poll.start();
    loop.exec();
    require(ready(), "timed out waiting for frame scheduling");
}

void run_for(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

double process_cpu_seconds()
{
    timespec time{};
    require(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time) == 0, "process CPU clock failed");
    return static_cast<double>(time.tv_sec) + static_cast<double>(time.tv_nsec) * 1e-9;
}

QImage capture(QQuickItem& item)
{
    auto grab = item.grabToImage();
    require(bool(grab), "grabToImage failed");
    bool ready = false;
    QObject callbacks;
    QObject::connect(grab.get(), &QQuickItemGrabResult::ready, &callbacks, [&] { ready = true; });
    wait_for([&] { return ready; });
    auto image = grab->image().convertToFormat(QImage::Format_RGBA8888);
    require(!image.isNull(), "empty captured image");
    return image;
}

class ProbeItem final : public nekomote::plasma::NekoWallpaperItem
{
  public:
    using NekoWallpaperItem::componentComplete;
};
} // namespace

int main(int argc, char** argv)
{
    // Standalone audit host only: never change plasmashell's global format in
    // the plugin. This allows equal-context XWayland/Wayland comparisons.
    const auto gl_api = qEnvironmentVariable("NEKOMOTE_TEST_GL_API");
    if (!gl_api.isEmpty())
    {
        require(gl_api == "desktop" || gl_api == "gles", "invalid test GL API");
        QSurfaceFormat format;
        const bool gles = gl_api == "gles";
        format.setRenderableType(gles ? QSurfaceFormat::OpenGLES : QSurfaceFormat::OpenGL);
        format.setVersion(3, gles ? 0 : 3);
        format.setProfile(gles ? QSurfaceFormat::NoProfile : QSurfaceFormat::CoreProfile);
        QSurfaceFormat::setDefaultFormat(format);
    }
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication application(argc, argv);
    const auto source = qEnvironmentVariable("NEKOMOTE_TEST_ASSETS_DIR");
    namespace fs = std::filesystem;
    if (!fs::exists(source.toStdString() + "/characters/chocola/date-a/model.nkm"))
    {
        std::puts("SKIPPED: set NEKOMOTE_TEST_ASSETS_DIR with chocola/date-a");
        return 77;
    }
    // Measurement mode also runs against the frozen, unfixed library. Never
    // capture/read back pixels during CPU measurements.
    const bool measure = qEnvironmentVariableIsSet("NEKOMOTE_PACING_MEASURE");
    QTemporaryDir directory;
    require(directory.isValid(), "temporary directory failed");
    try
    {
        const fs::path root(directory.path().toStdString());
        fs::create_directories(root / "characters/chocola");
        fs::create_directory_symlink(fs::absolute(source.toStdString()) /
                                         "characters/chocola/date-a",
                                     root / "characters/chocola/date-a");
        auto item = std::make_unique<ProbeItem>();
        auto state = item->wallpaper_state();
        state->no_save = true;
        if (!measure)
            require(item->configure_diagnostics(0, true, 1), "deterministic setup failed");
        item->setRenderBackend(qEnvironmentVariable("NEKOMOTE_TEST_BACKEND", "opengl"));
        item->setAcceptHoverEvents(false);
        item->setAcceptedMouseButtons(Qt::NoButton);
        item->setSize({640, 480});
        item->componentComplete();
        item->setAssetsDir(directory.path());
        unsigned config_revision = 0;
        const auto configure_fps = [&](int fps)
        {
            // Use the same GUI-buffered config handoff as the item setters;
            // never call the render-thread ABI handle from this test thread.
            const auto path = root / ("panel-" + std::to_string(++config_revision) + ".json");
            std::ofstream config(path);
            config << R"({"version":1,"character":"chocola/date-a","mute":true,"fps":)" << fps
                   << '}';
            config.close();
            state->config_path = path;
            state->config_dirty = true;
            item->update();
        };
        configure_fps(60);
        QQuickWindow window;
        QObject::connect(
            &window, &QQuickWindow::sceneGraphInitialized, &window,
            []
            {
                auto* context = QOpenGLContext::currentContext();
                require(context != nullptr, "no scene-graph GL context");
                const auto format = context->format();
                std::printf("context=%s version=%d.%d screen_platform=%s\n",
                            context->isOpenGLES() ? "gles" : "desktop", format.majorVersion(),
                            format.minorVersion(), qPrintable(QGuiApplication::platformName()));
            },
            Qt::DirectConnection);
        window.setFlags(Qt::Window | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
        window.setPersistentSceneGraph(false);
        window.resize(640, 480);
        item->setParentItem(window.contentItem());
        QString error;
        QObject::connect(item.get(), &nekomote::qt_quick::WallpaperItemBase::failed, &window,
                         [&](const QString& message) { error = message; });
        window.show();
        wait_for([&] { return item->queryRenderedFrames() >= 4 || !error.isEmpty(); });
        require(error.isEmpty(), qPrintable(error));
        auto* timer = item->findChild<QTimer*>();
        require(timer && timer->isActive() && timer->interval() == 17, "60 FPS timer missing");
        unsigned timer_ticks = 0;
        QObject::connect(timer, &QTimer::timeout, &window, [&] { ++timer_ticks; });
        const auto sample = [&](const char* phase, bool repeat_updates)
        {
            std::vector<double> intervals;
            intervals.reserve(4096);
            auto previous = std::chrono::steady_clock::now();
            QObject callbacks;
            QObject::connect(
                &window, &QQuickWindow::frameSwapped, &callbacks,
                [&]
                {
                    const auto now = std::chrono::steady_clock::now();
                    intervals.push_back(
                        std::chrono::duration<double, std::milli>(now - previous).count());
                    previous = now;
                });
            QTimer redundant;
            unsigned requests = 0;
            redundant.setInterval(5);
            QObject::connect(&redundant, &QTimer::timeout, &window,
                             [&]
                             {
                                 ++requests;
                                 item->update();
                             });
            if (repeat_updates)
                redundant.start();
            const auto frames = item->queryRenderedFrames();
            const auto ticks = timer_ticks;
            const auto cpu = process_cpu_seconds();
            const auto start = std::chrono::steady_clock::now();
            run_for(measure ? 2000 : 250);
            const auto wall =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            const auto drawn = item->queryRenderedFrames() - frames;
            const auto cpu_seconds = process_cpu_seconds() - cpu;
            rusage usage{};
            require(getrusage(RUSAGE_SELF, &usage) == 0, "getrusage failed");
            // First interval is a partial frame at the sampling boundary.
            if (!intervals.empty())
                intervals.erase(intervals.begin());
            std::sort(intervals.begin(), intervals.end());
            const auto percentile = [&](double fraction)
            {
                return intervals.empty()
                           ? 0.0
                           : intervals[static_cast<std::size_t>(
                                 fraction * static_cast<double>(intervals.size() - 1))];
            };
            std::printf(
                "phase=%s wall_s=%.6f cpu_s=%.6f frames=%u timer_ticks=%u requests=%u "
                "present_p50_ms=%.3f present_p95_ms=%.3f present_p99_ms=%.3f peak_rss_kib=%ld\n",
                phase, wall, cpu_seconds, drawn, timer_ticks - ticks, requests, percentile(.50),
                percentile(.95), percentile(.99), usage.ru_maxrss);
            return drawn;
        };
        if (measure)
            run_for(2000);
        sample("limited60", false);
        item->setPaused(true);
        run_for(250);
        const auto before = measure ? QImage{} : capture(*item);
        const auto idle_draws = sample("paused_redundant", true);
        if (!measure)
        {
            require(idle_draws == 0, "paused unchanged FBO was redrawn");
            require(before == capture(*item), "paused cached content changed");
            item->setSize({512, 384});
            window.resize(512, 384);
            require(capture(*item).size() == QSize(512, 384), "paused resize did not recreate FBO");
            item->setSize({640, 480});
            window.resize(640, 480);
            const auto restored = capture(*item);
            if (qEnvironmentVariableIsSet("NEKOMOTE_PACING_EVIDENCE"))
            {
                const auto output = qEnvironmentVariable("NEKOMOTE_PACING_EVIDENCE");
                fs::create_directories(output.toStdString());
                before.save(output + "/before.png");
                restored.save(output + "/restored.png");
            }
            // Shrinking clamps the movable control icon to the smaller output;
            // enlarging preserves that position. Compare the model below it.
            require(before.copy(0, 64, 640, 416) == restored.copy(0, 64, 640, 416),
                    "paused resize lost model content");
            const auto generation = state->renderer_created.load();
            window.hide();
            window.releaseResources();
            run_for(100);
            window.show();
            wait_for([&] { return state->renderer_created.load() > generation; });
            const auto recreated = capture(*item);
            if (qEnvironmentVariableIsSet("NEKOMOTE_PACING_EVIDENCE"))
                recreated.save(qEnvironmentVariable("NEKOMOTE_PACING_EVIDENCE") + "/recreated.png");
            require(before == recreated, "paused scene-graph recreation lost initial content");
            require(sample("paused_recreated", true) == 0,
                    "paused renderer dirtied every callback before its first animation tick");
            // A seed change reloads the model without replacing the renderer.
            const auto reloaded_frames = item->queryRenderedFrames();
            state->seed = 2;
            state->config_dirty = true;
            item->update();
            wait_for([&] { return item->queryRenderedFrames() > reloaded_frames; });
            run_for(150);
            require(sample("paused_reloaded", true) == 0,
                    "paused model reload did not settle after synchronizing playback");
        }
        item->setPaused(false);
        auto frames = item->queryRenderedFrames();
        wait_for([&] { return item->queryRenderedFrames() > frames + 2; });
        for (int fps : {180, 240, 0, 60, 0})
        {
            configure_fps(fps);
            wait_for([&] { return state->fps_limit.load() == fps || !error.isEmpty(); });
            require(error.isEmpty(), qPrintable(error));
            run_for(measure ? 500 : 150);
            if (!measure)
                require(fps == 0 ? !timer->isActive() : timer->isActive(),
                        "frame limit change left wrong timer state");
            if (fps == 0)
                require(sample("unlimited", false) > 2, "unlimited playback froze");
        }
        item->setPaused(true);
        run_for(150);
        require(!timer->isActive(), "unlimited pause did not stop timer");
        frames = item->queryRenderedFrames();
        run_for(150);
        require(item->queryRenderedFrames() == frames, "unlimited pause kept requesting frames");
        item->setPaused(false);
        wait_for([&] { return item->queryRenderedFrames() > frames + 2; });
        window.hide();
        run_for(100);
        frames = item->queryRenderedFrames();
        run_for(100);
        require(!timer->isActive() && item->queryRenderedFrames() == frames,
                "hidden unlimited playback continued");
        window.show();
        wait_for([&] { return item->queryRenderedFrames() > frames + 2; });
        if (!measure)
        {
            state->deterministic = false;
            for (int fps : {0, 60, 0})
            {
                frames = item->queryRenderedFrames();
                configure_fps(fps);
                wait_for([&] { return item->queryRenderedFrames() > frames + 3; });
                const auto moving = capture(*item).copy(0, 64, 640, 416);
                run_for(250);
                require(moving != capture(*item).copy(0, 64, 640, 416),
                        "normal animation froze after a frame limit change");
            }
            window.hide();
            run_for(100);
            window.show();
            frames = item->queryRenderedFrames();
            wait_for([&] { return item->queryRenderedFrames() > frames + 3; });
            const auto moving = capture(*item).copy(0, 64, 640, 416);
            run_for(250);
            require(moving != capture(*item).copy(0, 64, 640, 416),
                    "normal unlimited animation froze after hide/show");
        }
        require(error.isEmpty(), qPrintable(error));
        window.hide();
        item->setParentItem(nullptr);
        window.releaseResources();
        item.reset();
        std::puts("frame pacing, pause, resume, hide, resize and recreation PASS");
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
