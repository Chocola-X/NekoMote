#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSGSimpleRectNode>
#include <QScopedValueRollback>
#include <QScreen>
#include <QSurfaceFormat>
#include <QTimer>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "session_item.hpp"

namespace
{
struct Options
{
    std::filesystem::path model, session, capture, lifecycle;
    int width{800}, height{1080}, steps{0}, run_seconds{0}, trace_frames{0};
    std::uint32_t seed{1};
    float x{0}, y{400}, scale{1};
    bool fixed{false}, marker{false};
    std::optional<QPointF> touch;
};
long integer(const std::string& text, long minimum, long maximum)
{
    std::size_t consumed = 0;
    const auto value = std::stol(text, &consumed);
    if (consumed != text.size() || value < minimum || value > maximum)
        throw std::runtime_error("integer option outside its accepted range: " + text);
    return value;
}
float number(const std::string& text)
{
    std::size_t consumed = 0;
    const auto value = std::stof(text, &consumed);
    if (consumed != text.size() || !std::isfinite(value))
        throw std::runtime_error("non-finite/invalid number: " + text);
    return value;
}
Options options(int argc, char** argv)
{
    Options result;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view key(argv[i]);
        const auto value = [&]() -> std::string
        {
            if (++i >= argc)
                throw std::runtime_error("missing option value");
            return argv[i];
        };
        if (key == "--model")
            result.model = value();
        else if (key == "--session")
            result.session = value();
        else if (key == "--capture")
            result.capture = value();
        else if (key == "--check-lifecycle")
            result.lifecycle = value();
        else if (key == "--width")
            result.width = static_cast<int>(integer(value(), 1, 8192));
        else if (key == "--height")
            result.height = static_cast<int>(integer(value(), 1, 8192));
        else if (key == "--steps")
        {
            result.steps = static_cast<int>(integer(value(), 0, 100000));
            result.fixed = true;
        }
        else if (key == "--run-seconds")
            result.run_seconds = static_cast<int>(integer(value(), 1, 3600));
        else if (key == "--trace-frames")
            result.trace_frames = static_cast<int>(integer(value(), 1, 64));
        else if (key == "--seed")
            result.seed = static_cast<std::uint32_t>(
                integer(value(), 0, std::numeric_limits<std::uint32_t>::max()));
        else if (key == "--origin-x")
            result.x = number(value());
        else if (key == "--origin-y")
            result.y = number(value());
        else if (key == "--scale")
            result.scale = number(value());
        else if (key == "--qt-marker")
            result.marker = true;
        else if (key == "--touch")
            result.touch = QPointF{number(value()), number(value())};
        else if (key == "--graphics-api")
        {
            if (value() != "opengl")
                throw std::runtime_error("only the OpenGL Qt Quick backend is supported");
        }
        else if (key == "--help")
        {
            std::puts(
                "nekomote_qt_quick_host --model MODEL --session SESSION\n"
                "  [--width 800 --height 1080 --origin-x 0 --origin-y 400 --scale 1 --seed 1]\n"
                "  [--capture NEW.png --steps N] [--qt-marker]\n"
                "  [--trace-frames N] (bounded CPU submission diagnostics)\n"
                "  [--touch LOGICAL_X LOGICAL_Y] (diagnostic callback, no OS injection)\n"
                "  [--check-lifecycle NEW-directory --steps N] [--run-seconds N]\n"
                "Explicitly silent diagnostic host. OpenGL only; no desktop settings changed.");
            std::exit(0);
        }
        else
            throw std::runtime_error("unknown option: " + std::string(key));
    }
    if (result.model.empty() || result.session.empty() || result.scale <= 0)
        throw std::runtime_error("model/session and positive scale are required");
    if ((!result.capture.empty() || !result.lifecycle.empty()) != result.fixed)
        throw std::runtime_error("capture/lifecycle require --steps; --steps requires diagnostics");
    if (!result.capture.empty() && !result.lifecycle.empty())
        throw std::runtime_error("choose capture or lifecycle diagnostics");
    if (result.run_seconds && result.fixed)
        throw std::runtime_error("--run-seconds is for normal playback only");
    if (result.trace_frames && !result.run_seconds && !result.fixed)
        throw std::runtime_error("--trace-frames requires bounded playback or diagnostics");
    if (result.touch && !result.fixed)
        throw std::runtime_error("--touch requires capture/lifecycle diagnostics");
    if ((!result.capture.empty() && std::filesystem::exists(result.capture)) ||
        (!result.lifecycle.empty() && std::filesystem::exists(result.lifecycle)))
        throw std::runtime_error("diagnostic output already exists; choose a new path");
    return result;
}

// Qt scene-graph geometry drawn after the native FBO. The optional marker verifies
// native GL state reset and composition without embedding any wallpaper policy.
class MarkerItem final : public QQuickItem
{
  public:
    explicit MarkerItem(QQuickItem* parent) : QQuickItem(parent)
    {
        setFlag(ItemHasContents, true);
        setPosition({4, 4});
        setSize({16, 16});
        setZ(1);
    }

  protected:
    QSGNode* updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) override
    {
        auto* node = previous ? static_cast<QSGSimpleRectNode*>(previous) : new QSGSimpleRectNode;
        node->setRect(boundingRect());
        node->setColor(Qt::yellow);
        return node;
    }
};

int run(int argc, char** argv)
{
    const auto config = options(argc, argv);
    auto state =
        nekomote::qt_quick::load_session(config.model, config.session, config.seed, config.x,
                                         config.y, config.scale, config.steps, config.fixed);
    // This choice is confined to our own diagnostic process. A future Plasma
    // item must query the host backend and reject unsupported RHI implementations.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setAlphaBufferSize(8);
    format.setSamples(0);
    QSurfaceFormat::setDefaultFormat(format);
    state->trace_frames = config.trace_frames;
    // Our --session is a model asset, not Qt's session-manager command option.
    int qt_argc = 1;
    char* qt_argv[] = {argv[0], nullptr};
    QGuiApplication application(qt_argc, qt_argv);
    int exit_code = 0;
    {
        QQuickWindow window;
        auto callbacks = std::make_unique<QObject>();
        window.setTitle(QStringLiteral("NekoMote Qt Quick diagnostic host"));
        window.setFlags(Qt::Window | Qt::WindowDoesNotAcceptFocus);
        window.setColor(Qt::transparent);
        window.setPersistentSceneGraph(false);
        window.setPersistentGraphics(false);
        window.resize(config.width, config.height);
        auto* item = new nekomote::qt_quick::SessionItem(state, window.contentItem());
        item->setSize({static_cast<qreal>(config.width), static_cast<qreal>(config.height)});
        if (config.touch)
            item->replay_touch(*config.touch);
        if (config.marker)
            new MarkerItem(window.contentItem());
        QObject::connect(&window, &QQuickWindow::widthChanged, item,
                         [item](int width) { item->setWidth(width); });
        QObject::connect(&window, &QQuickWindow::heightChanged, item,
                         [item](int height) { item->setHeight(height); });
        QObject::connect(&window, &QQuickWindow::visibleChanged, item,
                         [item](bool visible) { item->set_paused(!visible); });
        const auto fail = [&](const QString& message)
        {
            std::fprintf(stderr, "Qt Quick host: %s\n", message.toUtf8().constData());
            exit_code = 1;
            application.quit();
        };
        QObject::connect(item, &nekomote::qt_quick::SessionItem::failed, callbacks.get(), fail);
        int traced_gui_ticks = 0;
        int traced_presentations = 0;
        if (config.trace_frames)
        {
            std::fprintf(stderr, "Qt diagnostic screen_hz=%.3f swap_interval=%d\n",
                         window.screen()->refreshRate(), window.format().swapInterval());
            QObject::connect(&window, &QQuickWindow::afterFrameEnd, callbacks.get(),
                             [&]
                             {
                                 if (traced_presentations++ >= config.trace_frames * 2)
                                     return;
                                 std::fprintf(stderr, "Qt frame end exposed=%d size=%dx%d\n",
                                              window.isExposed(), window.width(), window.height());
                             });
        }
        QTimer timer;
        timer.setInterval(33);
        QObject::connect(&timer, &QTimer::timeout, item,
                         [item, &config, &traced_gui_ticks]
                         {
                             if (config.trace_frames &&
                                 traced_gui_ticks++ < config.trace_frames * 2)
                                 std::fprintf(stderr, "GUI timer update\n");
                             item->update();
                         });
        if (!config.fixed)
            timer.start();
        int stage = 0;
        unsigned original_generation = 0;
        unsigned resumed_generation = 0;
        QSize wanted_size;
        bool complete = false;
        if (!config.lifecycle.empty())
            std::filesystem::create_directories(config.lifecycle);
        bool capture_in_progress = false;
        const auto capture = [&](const std::filesystem::path& path, QSize framebuffer_size)
        {
            QScopedValueRollback<bool> capture_guard(capture_in_progress, true);
            // Capture Qt's complete content into its own offscreen target. An
            // X11 drawable may still have the old size after hide/show/resize.
            // This is diagnostic only; normal playback never requests readback.
            const auto result = window.contentItem()->grabToImage();
            if (!result)
                throw std::runtime_error("Qt content capture could not be scheduled");
            QEventLoop wait;
            QTimer deadline;
            deadline.setSingleShot(true);
            deadline.setInterval(5000);
            QObject::connect(result.get(), &QQuickItemGrabResult::ready, &wait, &QEventLoop::quit);
            QObject::connect(&deadline, &QTimer::timeout, &wait, &QEventLoop::quit);
            deadline.start();
            wait.exec();
            const auto image = result->image();
            if (image.isNull() || image.size() != framebuffer_size ||
                !image.save(QString::fromStdString(path.string())))
                throw std::runtime_error("failed to capture Qt composition at framebuffer size: " +
                                         std::to_string(image.width()) + "x" +
                                         std::to_string(image.height()));
        };
        const auto consume_frame = [&](QSize framebuffer_size, unsigned generation,
                                       double clock_delta, const QString& reaction)
        {
            if (complete || capture_in_progress)
                return;
            std::fprintf(
                stdout, "Qt frame stage=%d size=%dx%d generation=%u clock_delta=%.9f reaction=%s\n",
                stage, framebuffer_size.width(), framebuffer_size.height(), generation, clock_delta,
                reaction.toUtf8().constData());
            std::fflush(stdout);
            try
            {
                if (!config.capture.empty())
                {
                    capture(config.capture, framebuffer_size);
                    complete = true;
                    application.quit();
                }
                else if (stage == 0)
                {
                    capture(config.lifecycle / "initial.png", framebuffer_size);
                    original_generation = generation;
                    stage = 1;
                    wanted_size = QSize(640, 480) * window.devicePixelRatio();
                    window.resize(640, 480);
                    item->update();
                }
                else if (stage == 1 && window.size() == QSize(640, 480) &&
                         framebuffer_size == wanted_size)
                {
                    capture(config.lifecycle / "resized.png", framebuffer_size);
                    stage = 2;
                    item->set_paused(true);
                    window.hide();
                    window.releaseResources();
                    QTimer::singleShot(150, callbacks.get(),
                                       [&]
                                       {
                                           window.resize(config.width, config.height);
                                           item->set_paused(false);
                                           window.show();
                                           window.resize(config.width, config.height);
                                           item->setSize({static_cast<qreal>(config.width),
                                                          static_cast<qreal>(config.height)});
                                           item->update();
                                       });
                }
                else if (stage == 2 && generation > original_generation)
                {
                    if (resumed_generation != generation)
                    {
                        if (clock_delta != 0)
                            throw std::runtime_error("scene-graph recreation advanced hidden time");
                        resumed_generation = generation;
                        // Showing/resizing an X11 surface is asynchronous. Let Qt
                        // present its rebuilt swapchain before grabbing that surface.
                        item->update();
                        return;
                    }
                    if (window.size() == QSize(config.width, config.height) &&
                        framebuffer_size ==
                            QSize(config.width, config.height) * window.devicePixelRatio())
                    {
                        capture(config.lifecycle / "recreated.png", framebuffer_size);
                        complete = true;
                        application.quit();
                    }
                    else
                    {
                        window.resize(config.width, config.height);
                        item->setSize(
                            {static_cast<qreal>(config.width), static_cast<qreal>(config.height)});
                        item->update();
                    }
                }
            }
            catch (const std::exception& error)
            {
                complete = true;
                fail(QString::fromUtf8(error.what()));
            }
        };
        if (config.fixed)
            QObject::connect(item, &nekomote::qt_quick::SessionItem::frame_ready, callbacks.get(),
                             consume_frame);
        QTimer timeout;
        if (config.fixed)
        {
            timeout.setSingleShot(true);
            timeout.setInterval(30000);
            QObject::connect(&timeout, &QTimer::timeout, callbacks.get(),
                             [&] { fail(QStringLiteral("diagnostic capture timed out")); });
            timeout.start();
        }
        if (config.run_seconds)
            QTimer::singleShot(config.run_seconds * 1000, callbacks.get(),
                               [&] { application.quit(); });
        window.show();
        if (config.trace_frames)
            window.raise();
        application.exec();
        // Cancel queued GUI diagnostics before captured stack data can disappear.
        callbacks.reset();
        timer.stop();
        timeout.stop();
        window.hide();
        window.releaseResources();
        // Window destruction joins scene-graph cleanup before state counters below.
    }
    std::fprintf(stdout, "Qt Quick renderer created=%u destroyed=%u frames=%u\n",
                 state->renderer_created.load(), state->renderer_destroyed.load(),
                 state->rendered_frames.load());
    if (state->renderer_created.load() != state->renderer_destroyed.load())
        throw std::runtime_error("Qt Quick renderer cleanup did not complete");
    if (config.run_seconds && state->rendered_frames.load() == 0 && exit_code == 0)
        throw std::runtime_error("normal playback produced no frames");
    return exit_code;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        return run(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Qt Quick host: %s\n", error.what());
        return 1;
    }
}
