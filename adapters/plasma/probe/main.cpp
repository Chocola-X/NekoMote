#include <QColor>
#include <QCoreApplication>
#include <QEventLoop>
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
#include <stdexcept>
#include <string>
#include <string_view>

#include "wallpaper_item.hpp"

namespace
{
struct Options
{
    std::filesystem::path model, session, capture, lifecycle;
    int width{800}, height{1080}, steps{0};
    float x{0}, y{400}, scale{1}, dpi{1};
    bool marker{false};
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
            if (value() != "opengl")
                throw std::runtime_error("only the OpenGL Qt Quick backend is supported");
        }
        else if (key == "--help")
        {
            std::puts(
                "nekomote_plasma_probe --model MODEL --session SESSION\n"
                "  [--width 800 --height 1080 --origin-x 0 --origin-y 400 --scale 1]\n"
                "  [--dpi 1] [--capture NEW.png --steps N] [--qt-marker]\n"
                "  [--check-lifecycle NEW-directory --steps N]\n"
                "Isolated QML wallpaper probe. OpenGL only; no desktop settings changed.");
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
QString qml_source(const Options& config)
{
    const auto model_url = QUrl::fromLocalFile(QString::fromStdString(config.model.string()));
    const auto session_url = QUrl::fromLocalFile(QString::fromStdString(config.session.string()));
    return QStringLiteral(
        "import QtQuick\n"
        "import NekoMote.Wallpaper\n"
        "NekoWallpaperItem {\n"
        "    width: %1; height: %2\n"
        "    mirrorVertically: true\n"
        "    originX: %5; originY: %6; scale: %7\n"
        "    modelFile: \"%3\"\n"
        "    sessionFile: \"%4\"\n"
        "}")
        .arg(config.width)
        .arg(config.height)
        .arg(model_url.toString())
        .arg(session_url.toString())
        .arg(static_cast<double>(config.x))
        .arg(static_cast<double>(config.y))
        .arg(static_cast<double>(config.scale));
}
QImage grab(QQuickWindow& window)
{
    const auto result = window.contentItem()->grabToImage();
    if (!result)
        return {};
    QEventLoop wait;
    QTimer deadline;
    deadline.setSingleShot(true);
    deadline.setInterval(5000);
    QObject::connect(result.get(), &QQuickItemGrabResult::ready, &wait, &QEventLoop::quit);
    QObject::connect(&deadline, &QTimer::timeout, &wait, &QEventLoop::quit);
    deadline.start();
    wait.exec();
    return result->image();
}
int run(int argc, char** argv)
{
    const auto config = options(argc, argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
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
    QQmlEngine engine;
#ifdef NEKOMOTE_QML_IMPORT_PATH
    engine.addImportPath(QStringLiteral(NEKOMOTE_QML_IMPORT_PATH));
#endif
    QQmlComponent component(&engine);
    component.setData(qml_source(config).toUtf8(), QUrl{});
    if (component.isError())
    {
        for (const auto& error : component.errors())
            std::fprintf(stderr, "QML error: %s\n", error.toString().toUtf8().constData());
        return 1;
    }
    auto* wallpaper = qobject_cast<nekomote::plasma::NekoWallpaperItem*>(component.create());
    if (!wallpaper)
    {
        std::fprintf(stderr, "QML component creation failed\n");
        return 1;
    }
    QQuickWindow window;
    window.setTitle(QStringLiteral("NekoMote Plasma probe"));
    window.setFlags(Qt::Window | Qt::WindowDoesNotAcceptFocus);
    window.setColor(Qt::transparent);
    window.setPersistentSceneGraph(false);
    window.setPersistentGraphics(false);
    window.resize(config.width, config.height);
    wallpaper->setParentItem(window.contentItem());
    const auto status = wallpaper->status();
    const auto error_string = wallpaper->errorString();
    std::fprintf(stdout, "wallpaper status=%d errorString=%s\n", status,
                 error_string.toUtf8().constData());
    if (status != 1)
    {
        std::fprintf(stderr, "wallpaper did not load successfully\n");
        return 1;
    }
    auto state = wallpaper->session_state();
    state->fixed_steps = config.steps;
    state->deterministic = true;
    int exit_code = 0;
    bool complete = false;
    int lifecycle_stage = 0;
    unsigned original_generation = 0;
    unsigned resumed_generation = 0;
    QSize wanted_size;
    QImage initial_image;
    QObject::connect(wallpaper, &nekomote::plasma::NekoWallpaperItem::failed, &application,
                     [&exit_code, &application](const QString& message)
                     {
                         std::fprintf(stderr, "wallpaper failed: %s\n", message.toUtf8().constData());
                         exit_code = 1;
                         application.quit();
                     });
    QTimer timer;
    timer.setInterval(33);
    QObject::connect(&timer, &QTimer::timeout, &application, [&] { wallpaper->update(); });
    timer.start();
    if (!config.capture.empty())
    {
        QTimer::singleShot(100, &application,
                           [&]
                           {
                               const auto image = grab(window);
                               if (image.isNull() ||
                                   !image.save(QString::fromStdString(config.capture.string())))
                               {
                                   std::fprintf(stderr, "failed to save capture\n");
                                   exit_code = 1;
                               }
                               else
                                   std::fprintf(stdout, "captured %dx%d\n", image.width(),
                                                image.height());
                               complete = true;
                               application.quit();
                           });
    }
    else if (!config.lifecycle.empty())
    {
        std::filesystem::create_directories(config.lifecycle);
        QObject::connect(wallpaper, &nekomote::plasma::NekoWallpaperItem::frame_ready,
                         &application,
                         [&](QSize framebuffer_size, unsigned generation, double clock_delta,
                             const QString&)
                         {
                             if (complete)
                                 return;
                             std::fprintf(stdout,
                                          "lifecycle stage=%d size=%dx%d generation=%u delta=%.9f\n",
                                          lifecycle_stage, framebuffer_size.width(),
                                          framebuffer_size.height(), generation, clock_delta);
                             std::fflush(stdout);
                             try
                             {
                                 if (lifecycle_stage == 0)
                                 {
                                     const auto image = grab(window);
                                     if (image.isNull())
                                         throw std::runtime_error("initial capture failed");
                                     initial_image = image;
                                     image.save(QString::fromStdString(
                                         (config.lifecycle / "initial.png").string()));
                                     original_generation = generation;
                                     lifecycle_stage = 1;
                                     wanted_size = QSize(640, 480) * window.devicePixelRatio();
                                     window.resize(640, 480);
                                     wallpaper->setWidth(640);
                                     wallpaper->setHeight(480);
                                     wallpaper->update();
                                 }
                                 else if (lifecycle_stage == 1 && window.size() == QSize(640, 480) &&
                                          framebuffer_size == wanted_size)
                                 {
                                     const auto image = grab(window);
                                     if (image.isNull())
                                         throw std::runtime_error("resized capture failed");
                                     image.save(QString::fromStdString(
                                         (config.lifecycle / "resized.png").string()));
                                     lifecycle_stage = 2;
                                     wallpaper->setPaused(true);
                                     window.hide();
                                     window.releaseResources();
                                     QTimer::singleShot(150, &application,
                                                        [&]
                                                        {
                                                            window.resize(config.width,
                                                                          config.height);
                                                            wallpaper->setWidth(config.width);
                                                            wallpaper->setHeight(config.height);
                                                            wallpaper->setPaused(false);
                                                            window.show();
                                                            wallpaper->update();
                                                        });
                                 }
                                 else if (lifecycle_stage == 2 && generation > original_generation)
                                 {
                                     if (resumed_generation != generation)
                                     {
                                         if (clock_delta != 0)
                                             throw std::runtime_error(
                                                 "scene-graph recreation advanced hidden time");
                                         resumed_generation = generation;
                                         wallpaper->update();
                                         return;
                                     }
                                     if (window.size() == QSize(config.width, config.height) &&
                                         framebuffer_size ==
                                             QSize(config.width, config.height) *
                                                 window.devicePixelRatio())
                                     {
                                         const auto image = grab(window);
                                         if (image.isNull())
                                             throw std::runtime_error("recreated capture failed");
                                         image.save(QString::fromStdString(
                                             (config.lifecycle / "recreated.png").string()));
                                         if (image.size() != initial_image.size())
                                             throw std::runtime_error(
                                                 "recreated size differs from initial");
                                         if (image != initial_image)
                                             throw std::runtime_error(
                                                 "recreated frame differs from initial");
                                         complete = true;
                                         application.quit();
                                     }
                                     else
                                     {
                                         window.resize(config.width, config.height);
                                         wallpaper->setWidth(config.width);
                                         wallpaper->setHeight(config.height);
                                         wallpaper->update();
                                     }
                                 }
                             }
                             catch (const std::exception& error)
                             {
                                 std::fprintf(stderr, "lifecycle: %s\n", error.what());
                                 exit_code = 1;
                                 complete = true;
                                 application.quit();
                             }
                         });
    }
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(30000);
    QObject::connect(&timeout, &QTimer::timeout, &application,
                     [&]
                     {
                         if (!complete)
                         {
                             std::fprintf(stderr, "probe timed out\n");
                             exit_code = 1;
                             application.quit();
                         }
                     });
    timeout.start();
    window.show();
    application.exec();
    timer.stop();
    timeout.stop();
    window.hide();
    window.releaseResources();
    std::fprintf(stdout, "plasma probe renderer created=%u destroyed=%u\n",
                 state->renderer_created.load(), state->renderer_destroyed.load());
    if (state->renderer_created.load() != state->renderer_destroyed.load())
    {
        std::fprintf(stderr, "renderer cleanup did not complete\n");
        exit_code = 1;
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
