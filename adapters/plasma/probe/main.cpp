#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QTimer>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "wallpaper_item.hpp"

namespace
{
struct Options
{
    std::filesystem::path model, session, capture, lifecycle;
    int width{800}, height{1080}, steps{0}, max_steps{0};
    float x{0}, y{400}, scale{1}, dpi{1};
    bool marker{false};
    std::string graphics_api{"opengl"};
    std::string backend{"opengl"};
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
            result.steps = static_cast<int>(integer(value(), 0, 100000));
        else if (key == "--max-steps")
            result.max_steps = static_cast<int>(integer(value(), 1, 1000000));
        else if (key == "--origin-x")
            result.x = number(value());
        else if (key == "--origin-y")
            result.y = number(value());
        else if (key == "--scale")
            result.scale = number(value());
        else if (key == "--dpi")
            result.dpi = number(value());
        else if (key == "--qt-marker")
            result.marker = true;
        else if (key == "--graphics-api")
        {
            const auto api = value();
            if (api != "opengl" && api != "software")
                throw std::runtime_error("graphics-api must be opengl or software");
            result.graphics_api = api;
        }
        else if (key == "--backend")
        {
            result.backend = value();
            if (result.backend != "opengl" && result.backend != "vulkan")
                throw std::runtime_error("backend must be opengl or vulkan");
        }
        else if (key == "--help")
        {
            std::puts("nekomote_plasma_probe --model MODEL --session SESSION\n"
                      "  [--width 800 --height 1080 --origin-x 0 --origin-y 400 --scale 1]\n"
                      "  [--dpi 1] [--capture NEW.png --steps N] [--qt-marker]\n"
                      "  [--check-lifecycle NEW-directory --steps N]\n"
                      "  [--max-steps N] [--graphics-api opengl|software]\n"
                      "  [--backend opengl|vulkan] (wallpaper engine, not Qt scene graph)\n"
                      "Loads the actual wallpaper main.qml. OpenGL default; software asserts\n"
                      "UnsupportedBackend. No desktop settings changed.");
            std::exit(0);
        }
        else
            throw std::runtime_error("unknown option: " + std::string(key));
    }
    if (result.model.empty() || result.session.empty() || result.scale <= 0)
        throw std::runtime_error("model/session and positive scale are required");
    if (!result.capture.empty() && !result.lifecycle.empty())
        throw std::runtime_error("choose capture or lifecycle diagnostics");
    if (!result.capture.empty() && std::filesystem::exists(result.capture))
        throw std::runtime_error("capture output already exists; choose a new path");
    if (!result.lifecycle.empty() && std::filesystem::exists(result.lifecycle))
        throw std::runtime_error("lifecycle output already exists; choose a new path");
    return result;
}
int run(int argc, char** argv)
{
    const auto config = options(argc, argv);
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setAlphaBufferSize(8);
    format.setSamples(0);
    QSurfaceFormat::setDefaultFormat(format);
    if (config.dpi != 1)
        qputenv("QT_SCALE_FACTOR", QByteArray::number(config.dpi));
    int qt_argc = 1;
    char* qt_argv[] = {argv[0], nullptr};
    QGuiApplication application(qt_argc, qt_argv);
    if (config.graphics_api == "opengl")
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    else
        QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
    QQmlEngine engine;
#ifdef NEKOMOTE_QML_IMPORT_PATH
    engine.addImportPath(QStringLiteral(NEKOMOTE_QML_IMPORT_PATH));
#endif
    const auto qml_path =
        QString::fromStdString(std::filesystem::path(NEKOMOTE_WALLPAPER_QML).string());
    QQmlComponent component(&engine, QUrl::fromLocalFile(qml_path));
    if (component.isError())
    {
        for (const auto& error : component.errors())
            std::fprintf(stderr, "QML error: %s\n", error.toString().toUtf8().constData());
        return 1;
    }
    std::unique_ptr<QObject> root_object(component.beginCreate(engine.rootContext()));
    auto* root = qobject_cast<QQuickItem*>(root_object.get());
    if (!root)
        throw std::runtime_error("QML root is not a QQuickItem");
    root->setProperty("assetsDir", QString::fromStdString(config.model.parent_path().string()));
    root->setProperty("renderBackend", QString::fromStdString(config.backend));
    component.completeCreate();
    auto* wallpaper =
        qobject_cast<nekomote::plasma::NekoWallpaperItem*>(root->findChild<QObject*>("wallpaper"));
    if (!wallpaper)
    {
        std::fprintf(stderr, "NekoWallpaperItem child not found\n");
        return 1;
    }
    {
        auto state = wallpaper->wallpaper_state();
        state->model_path = config.model;
        state->session_path = config.session;
        state->seed = 1;
        state->assets_mode = false;
        state->config_dirty = true;
    }
    const bool fixed = !config.capture.empty() || !config.lifecycle.empty();
    if (config.graphics_api != "software" &&
        !wallpaper->configure_diagnostics(config.steps, fixed, 1))
        throw std::runtime_error(wallpaper->errorString().toStdString());
    auto state = wallpaper->wallpaper_state();
    auto host_window = std::make_unique<QQuickWindow>();
    auto& window = *host_window;
    window.setTitle(QStringLiteral("NekoMote Plasma probe"));
    window.setFlags(Qt::Window | Qt::WindowDoesNotAcceptFocus);
    window.setColor(Qt::transparent);
    window.setPersistentSceneGraph(false);
    window.setPersistentGraphics(false);
    window.resize(config.width, config.height);
    root->setWidth(config.width);
    root->setHeight(config.height);
    root->setParentItem(window.contentItem());
    int exit_code = 0;
    bool complete = false;
    bool capture_pending = false;
    int lifecycle_stage = 0;
    unsigned original_generation = 0;
    unsigned resumed_generation = 0;
    unsigned rendered_frames = 0;
    double advanced_seconds = 0;
    QSize wanted_size;
    QImage initial_image;
    QSharedPointer<QQuickItemGrabResult> pending_grab;
    auto callbacks = std::make_unique<QObject>();
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(30000);
    const auto fail = [&](const QString& message)
    {
        if (complete)
            return;
        std::fprintf(stderr, "plasma probe: %s\n", message.toUtf8().constData());
        exit_code = 1;
        complete = true;
        application.quit();
    };
    QObject::connect(wallpaper, &nekomote::plasma::NekoWallpaperItem::failed, callbacks.get(),
                     fail);
    QObject::connect(&timeout, &QTimer::timeout, callbacks.get(),
                     [&] { fail(QStringLiteral("probe timed out")); });
    // The screenshot completion is asynchronous. No nested event loop can
    // re-enter a lifecycle stage before its initial image has completed.
    const auto capture = [&](const std::filesystem::path& path, auto finished)
    {
        capture_pending = true;
        pending_grab = window.contentItem()->grabToImage();
        if (!pending_grab)
        {
            capture_pending = false;
            fail(QStringLiteral("failed to begin diagnostic capture"));
            return;
        }
        QObject::connect(pending_grab.get(), &QQuickItemGrabResult::ready, callbacks.get(),
                         [&, path, finished]
                         {
                             if (complete)
                                 return;
                             const auto image = pending_grab->image();
                             if (image.isNull() ||
                                 !image.save(QString::fromStdString(path.string())))
                             {
                                 fail(QStringLiteral("failed to save diagnostic capture: %1")
                                          .arg(QString::fromStdString(path.string())));
                                 return;
                             }
                             capture_pending = false;
                             try
                             {
                                 finished(image);
                             }
                             catch (const std::exception& error)
                             {
                                 fail(QString::fromUtf8(error.what()));
                             }
                         });
    };
    if (!config.lifecycle.empty())
        std::filesystem::create_directories(config.lifecycle);
    if (config.graphics_api == "software")
    {
        QObject::connect(
            &window, &QQuickWindow::sceneGraphInitialized, callbacks.get(),
            [&]
            {
                // Item capability reporting is queued to the GUI
                // thread first; validate after actual initialization.
                QTimer::singleShot(
                    0, callbacks.get(),
                    [&]
                    {
                        if (!window.isVisible() || !window.isSceneGraphInitialized() ||
                            wallpaper->status() != 3 || wallpaper->errorString().isEmpty())
                        {
                            fail(QStringLiteral(
                                "initialized software window did not report UnsupportedBackend"));
                            return;
                        }
                        for (const auto* backend : {"vulkan", "opengl"})
                        {
                            wallpaper->setRenderBackend(QString::fromLatin1(backend));
                            if (wallpaper->status() != 3 || wallpaper->errorString().isEmpty() ||
                                !static_cast<nekomote::qt_quick::WallpaperItemBase*>(wallpaper)
                                     ->get_item_state()
                                     .paused)
                            {
                                fail(QStringLiteral(
                                    "engine selection concealed the unsupported Qt backend"));
                                return;
                            }
                        }
                        std::fprintf(stdout, "wallpaper status=3 errorString=%s\n",
                                     wallpaper->errorString().toUtf8().constData());
                        complete = true;
                        application.quit();
                    });
            },
            Qt::QueuedConnection);
    }
    else
        QObject::connect(
            wallpaper, &nekomote::plasma::NekoWallpaperItem::frame_ready, callbacks.get(),
            [&](QSize framebuffer_size, unsigned generation, double clock_delta, const QString&)
            {
                if (complete || capture_pending)
                    return;
                ++rendered_frames;
                if (!fixed)
                {
                    // Paused startup/hidden time is not counted as
                    // real advancement by the shared bridge.
                    advanced_seconds += clock_delta;
                    if (config.max_steps > 0 &&
                        rendered_frames >= static_cast<unsigned>(config.max_steps))
                    {
                        if (advanced_seconds <= 0)
                            fail(QStringLiteral("normal playback did not advance time"));
                        else
                        {
                            std::fprintf(stdout,
                                         "max-steps reached: rendered_frames=%u "
                                         "advanced_seconds=%.9f generation=%u\n",
                                         rendered_frames, advanced_seconds, generation);
                            complete = true;
                            application.quit();
                        }
                    }
                    return;
                }
                if (!config.capture.empty())
                {
                    capture(config.capture,
                            [&](const QImage& image)
                            {
                                std::fprintf(stdout, "captured %dx%d\n", image.width(),
                                             image.height());
                                complete = true;
                                application.quit();
                            });
                    return;
                }
                std::fprintf(stdout, "lifecycle stage=%d size=%dx%d generation=%u delta=%.9f\n",
                             lifecycle_stage, framebuffer_size.width(), framebuffer_size.height(),
                             generation, clock_delta);
                if (lifecycle_stage == 0)
                {
                    original_generation = generation;
                    capture(config.lifecycle / "initial.png",
                            [&](const QImage& image)
                            {
                                initial_image = image;
                                lifecycle_stage = 1;
                                wanted_size = QSize(640, 480) * window.devicePixelRatio();
                                window.resize(640, 480);
                                root->setWidth(640);
                                root->setHeight(480);
                                wallpaper->update();
                            });
                }
                else if (lifecycle_stage == 1 && window.size() == QSize(640, 480) &&
                         framebuffer_size == wanted_size)
                    capture(config.lifecycle / "resized.png",
                            [&](const QImage&)
                            {
                                lifecycle_stage = 2;
                                wallpaper->setPaused(true);
                                window.hide();
                                window.releaseResources();
                                QTimer::singleShot(150, callbacks.get(),
                                                   [&]
                                                   {
                                                       window.resize(config.width, config.height);
                                                       root->setWidth(config.width);
                                                       root->setHeight(config.height);
                                                       wallpaper->setPaused(false);
                                                       window.show();
                                                       wallpaper->update();
                                                   });
                            });
                else if (lifecycle_stage == 2 && generation > original_generation)
                {
                    if (resumed_generation != generation)
                    {
                        if (clock_delta != 0)
                        {
                            fail(QStringLiteral("scene-graph recreation advanced hidden time"));
                            return;
                        }
                        resumed_generation = generation;
                        wallpaper->update();
                        return;
                    }
                    if (window.size() == QSize(config.width, config.height) &&
                        framebuffer_size ==
                            QSize(config.width, config.height) * window.devicePixelRatio())
                        capture(config.lifecycle / "recreated.png",
                                [&](const QImage& image)
                                {
                                    if (image != initial_image)
                                        throw std::runtime_error(
                                            "recreated frame differs from initial");
                                    complete = true;
                                    application.quit();
                                });
                    else
                    {
                        window.resize(config.width, config.height);
                        root->setWidth(config.width);
                        root->setHeight(config.height);
                        wallpaper->update();
                    }
                }
            });
    timeout.start();
    window.show();
    application.exec();
    // Cancel queued diagnostics while their captured stack data is still alive.
    callbacks.reset();
    timeout.stop();
    pending_grab.reset();
    window.hide();
    window.releaseResources();
    root->setParentItem(nullptr);
    root_object.reset();
    host_window.reset(); // Joins render-thread GPU cleanup before final counts.
    std::fprintf(stdout, "plasma probe rendered_frames=%u\n", rendered_frames);
    if (state)
    {
        std::fprintf(stdout, "plasma probe renderer created=%u destroyed=%u\n",
                     state->renderer_created.load(), state->renderer_destroyed.load());
        if (state->renderer_created.load() != state->renderer_destroyed.load())
        {
            std::fprintf(stderr, "renderer cleanup did not complete\n");
            exit_code = 1;
        }
    }
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
        std::fprintf(stderr, "plasma probe: %s\n", error.what());
        return 1;
    }
}
