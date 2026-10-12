#include <KConfigGroup>
#include <KPackage/Package>
#include <KPackage/PackageLoader>
#include <Plasma/Applet>
#include <Plasma/Containment>
#include <Plasma/Corona>
#include <PlasmaQuick/AppletQuickItem>
#include <QApplication>
#include <QDir>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <exception>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "wallpaper_item.hpp"

namespace
{
class ProbeCorona final : public Plasma::Corona
{
  public:
    int numScreens() const override
    {
        return 1;
    }
    QRect screenGeometry(int) const override
    {
        return {0, 0, 800, 1080};
    }
    int screenForContainment(const Plasma::Containment*) const override
    {
        return 0;
    }
};

struct Options
{
    std::filesystem::path model, session, output;
    int width{800}, height{1080};
    int rounds{20};
    int config_rounds{20};
    float dpi{1};
    bool lifecycle{false};
    bool bad_asset{false};
    bool invalid_config{false};
    bool recovery{false};
    int steps{0};
    unsigned seed{1};
    std::string backend{"opengl"};
};

Options parse_options(int argc, char** argv)
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
        else if (key == "--output")
            result.output = value();
        else if (key == "--lifecycle")
            result.lifecycle = true;
        else if (key == "--bad-asset")
            result.bad_asset = true;
        else if (key == "--invalid-config")
            result.invalid_config = true;
        else if (key == "--recovery")
            result.recovery = true;
        else if (key == "--rounds")
            result.rounds = std::stoi(value());
        else if (key == "--config-rounds")
            result.config_rounds = std::stoi(value());
        else if (key == "--dpi")
        {
            result.dpi = std::stof(value());
            if (result.dpi <= 0 || !std::isfinite(result.dpi))
                throw std::runtime_error("--dpi must be positive and finite");
        }
        else if (key == "--width")
            result.width = std::stoi(value());
        else if (key == "--height")
            result.height = std::stoi(value());
        else if (key == "--steps")
            result.steps = std::stoi(value());
        else if (key == "--seed")
            result.seed = static_cast<unsigned>(std::stoul(value()));
        else if (key == "--backend")
        {
            result.backend = value();
            if (result.backend != "opengl" && result.backend != "vulkan")
                throw std::runtime_error("backend must be opengl or vulkan");
        }
        else if (key == "--help")
        {
            std::puts(
                "nekomote_plasma_package_probe --output DIR [--model M --session S]\n"
                "  [--lifecycle] [--bad-asset] [--invalid-config] [--recovery]\n"
                "  [--rounds 20] [--config-rounds 20] [--dpi 1] [--width 800 --height 1080]\n"
                "  [--steps 0] [--seed 1]\n"
                "  [--backend opengl|vulkan] (default opengl)\n"
                "Default: capture one screenshot. --lifecycle: full lifecycle test.\n"
                "--bad-asset/--invalid-config: error recovery test. --recovery: bad->good cycles.\n"
                "No desktop settings changed.");
            std::exit(0);
        }
        else
            throw std::runtime_error("unknown option: " + std::string(key));
    }
    if (result.output.empty())
        throw std::runtime_error("--output is required");
    if (std::filesystem::exists(result.output))
        throw std::runtime_error("output must be a new directory");
    if (!result.lifecycle && !result.bad_asset && !result.invalid_config && !result.recovery)
    {
        if (result.model.empty() || result.session.empty())
            throw std::runtime_error("--model and --session are required for capture mode");
    }
    if (result.rounds < 1 || result.rounds > 100)
        throw std::runtime_error("--rounds must be 1..100");
    if (result.config_rounds < 1 || result.config_rounds > 100)
        throw std::runtime_error("--config-rounds must be 1..100");
    if (result.width < 1 || result.width > 8192 || result.height < 1 || result.height > 8192)
        throw std::runtime_error("--width/--height must be 1..8192");
    return result;
}

QObject* find_native(Plasma::Containment* containment)
{
    auto* wallpaper = qobject_cast<QQuickItem*>(
        containment->property("wallpaperGraphicsObject").value<QObject*>());
    if (!wallpaper)
        return nullptr;
    return wallpaper->findChild<QObject*>(QStringLiteral("nativeSession"));
}

nekomote::plasma::NekoWallpaperItem* native_item(Plasma::Containment* containment)
{
    return qobject_cast<nekomote::plasma::NekoWallpaperItem*>(find_native(containment));
}

QObject* configuration_of(Plasma::Containment* containment)
{
    auto* wallpaper = qobject_cast<QQuickItem*>(
        containment->property("wallpaperGraphicsObject").value<QObject*>());
    if (!wallpaper)
        return nullptr;
    return wallpaper->property("configuration").value<QObject*>();
}

void wait_for(const std::function<bool()>& ready, int timeout_ms = 15000)
{
    if (ready())
        return;
    QEventLoop loop;
    std::exception_ptr failure;
    QTimer poll;
    poll.setInterval(50);
    QObject::connect(&poll, &QTimer::timeout, &loop,
                     [&]
                     {
                         try
                         {
                             if (ready())
                                 loop.quit();
                         }
                         catch (...)
                         {
                             failure = std::current_exception();
                             loop.quit();
                         }
                     });
    QTimer::singleShot(timeout_ms, &loop, &QEventLoop::quit);
    poll.start();
    loop.exec();
    poll.stop();
    if (failure)
        std::rethrow_exception(failure);
    if (!ready())
        throw std::runtime_error("timed out waiting for condition");
}

bool save_grab(QQuickWindow& window, const std::filesystem::path& path)
{
    QEventLoop loop;
    auto grab = window.contentItem()->grabToImage();
    if (!grab)
        return false;
    bool ok = false;
    QObject::connect(grab.get(), &QQuickItemGrabResult::ready, &loop,
                     [&]
                     {
                         ok = !grab->image().isNull() &&
                              grab->image().save(QString::fromStdString(path.string()));
                         loop.quit();
                     });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    return ok;
}

std::filesystem::path setup_workspace(const std::filesystem::path& output)
{
    std::filesystem::create_directories(output / "data/plasma/wallpapers");
    std::filesystem::create_directories(output / "data/plasma/plasmoids");
    std::filesystem::create_directories(output / "config");
    std::filesystem::create_directories(output / "cache");
    std::filesystem::copy(NEKOMOTE_PACKAGE_PATH,
                          output / "data/plasma/wallpapers/org.nekomote.wallpaper",
                          std::filesystem::copy_options::recursive);
    std::filesystem::copy(NEKOMOTE_CONTAINMENT_PATH,
                          output / "data/plasma/plasmoids/org.nekomote.validation",
                          std::filesystem::copy_options::recursive);
    return output / "data/plasma/wallpapers/org.nekomote.wallpaper";
}

int run(int argc, char** argv)
{
    const auto opts = parse_options(argc, argv);
    const auto abs_model =
        opts.model.empty() ? std::filesystem::path{} : std::filesystem::absolute(opts.model);
    const auto abs_session =
        opts.session.empty() ? std::filesystem::path{} : std::filesystem::absolute(opts.session);
    const auto abs_output = std::filesystem::absolute(opts.output);
    if (!opts.bad_asset && !opts.invalid_config && !opts.recovery)
    {
        if (!std::filesystem::is_regular_file(abs_model))
            throw std::runtime_error("model file does not exist");
        if (!std::filesystem::is_regular_file(abs_session))
            throw std::runtime_error("session file does not exist");
    }
    if (opts.recovery)
    {
        if (!std::filesystem::is_regular_file(abs_model))
            throw std::runtime_error("model file does not exist");
        if (!std::filesystem::is_regular_file(abs_session))
            throw std::runtime_error("session file does not exist");
    }
    const auto package_path = setup_workspace(abs_output);
    qputenv("XDG_DATA_HOME", QByteArray::fromStdString((abs_output / "data").string()));
    qputenv("XDG_CONFIG_HOME", QByteArray::fromStdString((abs_output / "config").string()));
    qputenv("XDG_CACHE_HOME", QByteArray::fromStdString((abs_output / "cache").string()));
    if (qgetenv("QML_IMPORT_PATH").isEmpty())
        qputenv("QML_IMPORT_PATH", QByteArray(NEKOMOTE_QML_IMPORT_PATH));
    if (opts.dpi != 1)
        qputenv("QT_SCALE_FACTOR", QByteArray::number(opts.dpi));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    int qt_argc = 1;
    char* qt_argv[] = {argv[0], nullptr};
    QApplication app(qt_argc, qt_argv);
    QCoreApplication::setApplicationName(QStringLiteral("nekomote_package_probe"));
    auto package = KPackage::PackageLoader::self()->loadPackage(QStringLiteral("Plasma/Wallpaper"));
    package.setPath(QString::fromStdString(package_path.string()));
    if (!package.isValid())
        throw std::runtime_error("staged KPackage is invalid");
    const auto config_path = package.filePath("ui", "config.qml");
    const auto schema_path = package.filePath("config", "main.xml");
    if (config_path.isEmpty() || schema_path.isEmpty())
        throw std::runtime_error("wallpaper configuration page/schema missing");
    QQmlEngine config_engine;
    QQmlComponent config_component(&config_engine, QUrl::fromLocalFile(config_path));
    auto assets_root = abs_model.parent_path();
    while (!assets_root.empty() && assets_root != assets_root.parent_path() &&
           !std::filesystem::exists(assets_root / "characters"))
        assets_root = assets_root.parent_path();
    if (assets_root.empty() || assets_root == assets_root.parent_path())
        assets_root = abs_model.parent_path();
    std::unique_ptr<QObject> config_object(config_component.createWithInitialProperties(
        {{"cfg_assetsDir",
          QUrl::fromLocalFile(QString::fromStdString(assets_root.string())).toString()}}));
    if (!config_object)
        throw std::runtime_error("configuration QML failed: " +
                                 config_component.errorString().toStdString());
    std::printf("KPackage config page=%s schema=%s: loaded\n", config_path.toUtf8().constData(),
                schema_path.toUtf8().constData());
    ProbeCorona corona;
    auto* containment = corona.createContainment(QStringLiteral("org.nekomote.validation"));
    if (!containment)
        throw std::runtime_error("validation containment could not be created");
    auto config =
        containment->config().group("Wallpaper").group("org.nekomote.wallpaper").group("General");
    config.writeEntry("renderBackend", QString::fromStdString(opts.backend));
    if (opts.bad_asset)
    {
        config.writeEntry("assetsDir", QStringLiteral("/nonexistent/assets"));
    }
    else if (opts.invalid_config)
    {
        const auto bad_path = abs_output / "bad_assets";
        std::filesystem::create_directories(bad_path);
        config.writeEntry("assetsDir", QString::fromStdString(bad_path.string()));
    }
    else
    {
        config.writeEntry("assetsDir", QString::fromStdString(assets_root.string()));
    }
    containment->setWallpaperPlugin(QStringLiteral("org.nekomote.wallpaper"));
    auto* item = PlasmaQuick::AppletQuickItem::itemForApplet(containment);
    if (!item)
        throw std::runtime_error("containment QML item could not be created");
    QQmlEngine::contextForObject(item)->engine()->addImportPath(
        QStringLiteral(NEKOMOTE_QML_IMPORT_PATH));
    auto window_owner = std::make_unique<QQuickWindow>();
    auto& window = *window_owner;
    window.setFlags(Qt::Window | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
    window.setColor(Qt::transparent);
    window.resize(opts.width, opts.height);
    item->setParentItem(window.contentItem());
    item->setSize({static_cast<qreal>(opts.width), static_cast<qreal>(opts.height)});
    // QWindow's native resize notification can precede its final geometry.
    // Bind the containment to the actual scene root, as a shell does.
    const auto resize_item = [&] { item->setSize(window.contentItem()->size()); };
    QObject::connect(window.contentItem(), &QQuickItem::widthChanged, item, resize_item);
    QObject::connect(window.contentItem(), &QQuickItem::heightChanged, item, resize_item);
    int exit_code = 0;
    auto callbacks = std::make_unique<QObject>();

    if (opts.bad_asset || opts.invalid_config)
    {
        window.show();
        wait_for(
            [&]
            {
                auto* native = find_native(containment);
                return native && native->property("status").toInt() == 2;
            });
        wait_for([&] { return window.isSceneGraphInitialized(); });
        auto* native = find_native(containment);
        const auto error = native->property("errorString").toString();
        if (error.isEmpty())
        {
            std::fprintf(stderr, "error test: status=2 but errorString empty\n");
            exit_code = 1;
        }
        else
        {
            std::printf("error test: status=2 errorString=%s\n", error.toUtf8().constData());
            if (!save_grab(window, abs_output / "error_recovery.png"))
            {
                std::fprintf(stderr, "error test: GUI grab failed\n");
                exit_code = 1;
            }
            else
                std::printf("error test: GUI survived, screenshot saved\n");
        }
    }
    else if (opts.lifecycle)
    {
        wait_for(
            [&]
            {
                auto* native = native_item(containment);
                return native && native->property("status").toInt() == 1;
            });
        auto* native = native_item(containment);
        if (!native)
            throw std::runtime_error("native item not found after load");
        if (!native->configure_diagnostics(0, false, 1))
            throw std::runtime_error("cannot configure diagnostics for lifecycle test");
        const QPointer diagnostic_native(native);
        window.show();
        wait_for(
            [&]
            {
                auto* n = native_item(containment);
                return n && n->queryRendererCreated() > 0;
            },
            10000);
        native = native_item(containment);
        if (diagnostic_native.isNull() || diagnostic_native != native)
            throw std::runtime_error(
                "initial configuration rebuilt the diagnostic item before rendering");
        std::printf("lifecycle: renderer created=%u frames=%u\n", native->queryRendererCreated(),
                    native->queryRenderedFrames());

        // Helper to re-fetch native safely
        auto get_native = [&]() -> nekomote::plasma::NekoWallpaperItem*
        { return native_item(containment); };

        // 1. Pause/resume
        std::printf("lifecycle: pause/resume\n");
        native = get_native();
        native->setProperty("paused", true);
        // Let any in-flight frame complete before measuring
        {
            QEventLoop settle;
            QTimer::singleShot(100, &settle, &QEventLoop::quit);
            settle.exec();
        }
        native = get_native();
        const auto pause_frames = native->queryRenderedFrames();
        {
            QEventLoop pause_loop;
            QTimer::singleShot(500, &pause_loop, &QEventLoop::quit);
            pause_loop.exec();
        }
        native = get_native();
        if (!native || native->queryRenderedFrames() != pause_frames)
            throw std::runtime_error("pause did not suspend advancement");
        std::printf("lifecycle: pause confirmed (frames=%u)\n", native->queryRenderedFrames());
        native->setProperty("paused", false);
        // Consume the actual first diagnostic notification; polling the latest
        // delta can observe a later frame and conceal a resume clock error.
        bool resumed = false;
        double resume_delta = -1;
        const auto resume_connection = QObject::connect(
            native, &nekomote::qt_quick::WallpaperItemBase::frame_ready, callbacks.get(),
            [&](QSize, unsigned, double delta, const QString&)
            {
                if (!resumed)
                {
                    resumed = true;
                    resume_delta = delta;
                }
            });
        wait_for([&] { return resumed; });
        QObject::disconnect(resume_connection);
        native = get_native();
        if (resume_delta != 0)
            throw std::runtime_error("resume replayed suspended wall-clock time");
        std::printf("lifecycle: resume confirmed (first delta=%.6f)\n", resume_delta);

        // 2. Hide/show
        std::printf("lifecycle: hide/show\n");
        native = get_native();
        window.hide();
        const auto* timer = native->findChild<QTimer*>();
        if (!static_cast<nekomote::qt_quick::WallpaperItemBase*>(native)->get_item_state().paused ||
            !timer || timer->isActive())
            throw std::runtime_error("hidden window did not suspend scheduling/runtime");
        // A frame submitted before hide may still complete. Drain it before
        // testing that suspended scheduling produces no further frames.
        {
            QEventLoop settle;
            QTimer::singleShot(100, &settle, &QEventLoop::quit);
            settle.exec();
        }
        const auto pre_hide_frames = get_native()->queryRenderedFrames();
        {
            QEventLoop loop;
            QTimer::singleShot(300, &loop, &QEventLoop::quit);
            loop.exec();
        }
        if (window.isVisible())
            throw std::runtime_error("window did not hide");
        native = get_native();
        if (native && native->queryRenderedFrames() != pre_hide_frames)
            throw std::runtime_error("hidden window produced frames");
        std::printf("lifecycle: hidden confirmed (no frames)\n");
        window.show();
        wait_for(
            [&]
            {
                auto* n = get_native();
                return n && n->queryRenderedFrames() > pre_hide_frames;
            });
        std::printf("lifecycle: show confirmed (frames resumed)\n");

        // 3. Resize with containment and FBO size assertion
        std::printf("lifecycle: resize\n");
        native = get_native();
        const auto pre_resize_frames = native->queryRenderedFrames();
        window.resize(640, 480);
        wait_for(
            [&]
            {
                auto* n = get_native();
                return item->size() == QSizeF(640, 480) && n &&
                       n->queryRenderedFrames() > pre_resize_frames;
            });
        QSize resize_fbo;
        bool resize_fbo_captured = false;
        const auto resize_fbo_connection = QObject::connect(
            native, &nekomote::qt_quick::WallpaperItemBase::frame_ready, callbacks.get(),
            [&](QSize fbo_size, unsigned, double, const QString&)
            {
                if (fbo_size == QSize(640, 480) * window.devicePixelRatio())
                {
                    resize_fbo = fbo_size;
                    resize_fbo_captured = true;
                }
            });
        wait_for([&] { return resize_fbo_captured; });
        QObject::disconnect(resize_fbo_connection);
        if (item->size() != QSizeF(640, 480))
            throw std::runtime_error(
                "containment did not resize with window: item=" + std::to_string(item->width()) +
                "x" + std::to_string(item->height()) + "; window=" +
                std::to_string(window.width()) + "x" + std::to_string(window.height()) +
                "; scene=" + std::to_string(window.contentItem()->width()) + "x" +
                std::to_string(window.contentItem()->height()));
        {
            const auto expected_fbo = QSize(640, 480) * window.devicePixelRatio();
            if (resize_fbo != expected_fbo)
                throw std::runtime_error("resize FBO size " + std::to_string(resize_fbo.width()) +
                                         "x" + std::to_string(resize_fbo.height()) +
                                         " != expected " + std::to_string(expected_fbo.width()) +
                                         "x" + std::to_string(expected_fbo.height()));
        }
        std::printf("lifecycle: resize 640x480 confirmed (FBO=%dx%d)\n", resize_fbo.width(),
                    resize_fbo.height());
        native = get_native();
        const auto pre_resize2_frames = native->queryRenderedFrames();
        window.resize(opts.width, opts.height);
        wait_for(
            [&]
            {
                auto* n = get_native();
                return n && n->queryRenderedFrames() > pre_resize2_frames;
            });
        std::printf("lifecycle: resize back confirmed\n");

        // 4. Config reload rounds (assetsDir toggle: bad→good triggers fault recovery)
        std::printf("lifecycle: create/destroy %d rounds\n", opts.rounds);
        unsigned total_created = 0;
        const auto good_assets = QString::fromStdString(assets_root.string());
        const auto bad_assets = QStringLiteral("/nonexistent/assets");
        for (int i = 0; i < opts.rounds; ++i)
        {
            auto* cfg = configuration_of(containment);
            if (!cfg)
                throw std::runtime_error("cannot access configuration for round " +
                                         std::to_string(i));
            cfg->setProperty("assetsDir", bad_assets);
            wait_for(
                [&]
                {
                    auto* n = find_native(containment);
                    return n && n->property("status").toInt() == 2;
                });
            cfg->setProperty("assetsDir", good_assets);
            wait_for(
                [&]
                {
                    auto* n = get_native();
                    return n && n->property("status").toInt() == 1;
                });
            auto* n = get_native();
            if (!n->configure_diagnostics(0, false, 1))
                throw std::runtime_error("diagnostic isolation failed for round " +
                                         std::to_string(i));
            if (n->acceptHoverEvents() || n->acceptedMouseButtons() != Qt::NoButton)
                throw std::runtime_error("round " + std::to_string(i) +
                                         " did not isolate real pointer input");
            total_created += n->queryRendererCreated();
            std::printf("lifecycle: round %d renderer_created=%u\n", i, n->queryRendererCreated());
        }
        std::printf("lifecycle: create/destroy %d rounds complete, total_created=%u\n", opts.rounds,
                    total_created);

        // 5. Config switch rounds (assetsDir toggle)
        std::printf("lifecycle: config switch %d rounds\n", opts.config_rounds);
        unsigned total_config_created = 0;
        for (int i = 0; i < opts.config_rounds; ++i)
        {
            auto* cfg = configuration_of(containment);
            if (!cfg)
                throw std::runtime_error("cannot access configuration for config round " +
                                         std::to_string(i));
            cfg->setProperty("assetsDir", bad_assets);
            wait_for(
                [&]
                {
                    auto* n = find_native(containment);
                    return n && n->property("status").toInt() == 2;
                });
            cfg->setProperty("assetsDir", good_assets);
            wait_for(
                [&]
                {
                    auto* n = get_native();
                    return n && n->property("status").toInt() == 1;
                });
            auto* n = get_native();
            if (!n->configure_diagnostics(0, false, 1))
                throw std::runtime_error("diagnostic isolation failed for config round " +
                                         std::to_string(i));
            if (n->acceptHoverEvents() || n->acceptedMouseButtons() != Qt::NoButton)
                throw std::runtime_error("config round " + std::to_string(i) +
                                         " did not isolate real pointer input");
            total_config_created += n->queryRendererCreated();
            std::printf("lifecycle: config round %d renderer_created=%u\n", i,
                        n->queryRendererCreated());
        }
        std::printf("lifecycle: config switch %d rounds complete, total_created=%u\n",
                    opts.config_rounds, total_config_created);

        // Final: verify renderer balance on the last native item
        auto* final_native = get_native();
        if (final_native)
        {
            const auto created = final_native->queryRendererCreated();
            const auto destroyed = final_native->queryRendererDestroyed();
            std::printf("lifecycle: final renderer_created=%u destroyed=%u\n", created, destroyed);
        }
        if (!save_grab(window, abs_output / "lifecycle_final.png"))
            throw std::runtime_error("lifecycle: final screenshot failed");
        else
            std::printf("lifecycle: final screenshot saved\n");
        std::printf("lifecycle: configured checks passed; teardown follows\n");
    }
    else if (opts.recovery)
    {
        auto set_config = [&](const char* key, const QVariant& value)
        {
            auto* cfg = configuration_of(containment);
            if (!cfg)
                throw std::runtime_error(std::string("cannot access configuration for ") + key);
            cfg->setProperty(key, value);
        };
        auto get_native = [&]() -> nekomote::plasma::NekoWallpaperItem*
        { return native_item(containment); };
        auto wait_loaded = [&]()
        {
            wait_for(
                [&]
                {
                    auto* n = get_native();
                    return n && n->property("status").toInt() == 1 && n->queryRendererCreated() > 0;
                });
        };
        auto isolate_inputs = [&](int step)
        {
            auto* n = get_native();
            if (!n || !n->configure_diagnostics(0, false, 1))
                throw std::runtime_error("diagnostic isolation failed at step " +
                                         std::to_string(step));
            if (n->acceptHoverEvents() || n->acceptedMouseButtons() != Qt::NoButton)
                throw std::runtime_error("step " + std::to_string(step) +
                                         " did not isolate real pointer input");
        };
        auto wait_frame_advance = [&]()
        {
            const auto baseline = get_native()->queryRenderedFrames();
            wait_for(
                [&]
                {
                    auto* n = get_native();
                    return n && n->queryRenderedFrames() > baseline + 1;
                });
        };
        const auto good_assets_dir = QString::fromStdString(assets_root.string());
        const auto bad_assets_dir = QStringLiteral("/nonexistent/assets");

        set_config("assetsDir", good_assets_dir);
        window.show();
        wait_loaded();
        isolate_inputs(1);
        wait_frame_advance();
        std::printf("recovery: initial good load status=1 frames=%u\n",
                    get_native()->queryRenderedFrames());

        for (int cycle = 0; cycle < 2; ++cycle)
        {
            set_config("assetsDir", bad_assets_dir);
            wait_for(
                [&]
                {
                    auto* n = find_native(containment);
                    return n && n->property("status").toInt() == 2;
                });
            {
                auto* n = find_native(containment);
                if (n->property("errorString").toString().isEmpty())
                    throw std::runtime_error("bad assets: errorString empty");
                std::printf("recovery: cycle %d bad assets status=2 error=%s\n", cycle,
                            n->property("errorString").toString().toUtf8().constData());
            }
            set_config("assetsDir", good_assets_dir);
            wait_for(
                [&]
                {
                    auto* n = get_native();
                    return n && n->property("status").toInt() == 1;
                });
            isolate_inputs(2 + cycle);
            wait_frame_advance();
            std::printf("recovery: cycle %d good assets restored status=1 frames=%u\n", cycle,
                        get_native()->queryRenderedFrames());
        }

        if (!save_grab(window, abs_output / "recovery_final.png"))
            throw std::runtime_error("recovery: final screenshot failed");
        std::printf("recovery: all bad->good->bad->good cycles passed\n");
    }
    else
    {
        wait_for(
            [&]
            {
                auto* n = native_item(containment);
                return n && n->status() != 0;
            });
        auto* native = native_item(containment);
        if (native->status() != 1)
            throw std::runtime_error(native->errorString().toStdString());
        if (!native->configure_diagnostics(opts.steps, true, opts.seed))
            throw std::runtime_error("fixed-step diagnostics must precede rendering");
        bool frame_complete = false;
        QSize frame_size;
        double frame_delta = -1;
        QObject::connect(native, &nekomote::qt_quick::WallpaperItemBase::frame_ready,
                         callbacks.get(),
                         [&](QSize size, unsigned, double delta, const QString&)
                         {
                             if (frame_complete)
                                 return;
                             frame_size = size;
                             frame_delta = delta;
                             frame_complete = true;
                         });
        window.show();
        wait_for([&] { return frame_complete; });
        const QSize expected_size(
            qRound(static_cast<qreal>(opts.width) * static_cast<qreal>(opts.dpi)),
            qRound(static_cast<qreal>(opts.height) * static_cast<qreal>(opts.dpi)));
        const auto executed = native->wallpaper_state()->fixed_steps_executed.load();
        if (frame_size != expected_size || frame_delta != 0 ||
            executed != static_cast<unsigned>(opts.steps))
            throw std::runtime_error("fixed frame has unexpected size, clock delta or step count");
        std::printf("capture: fixed steps=%d executed=%u seed=%u rendered_frames=%u\n", opts.steps,
                    executed, opts.seed, native->queryRenderedFrames());
        if (!save_grab(window, abs_output / "package.png"))
        {
            std::fprintf(stderr, "capture: screenshot failed\n");
            exit_code = 1;
        }
        else
            std::printf("capture: screenshot saved\n");
    }

    const auto* final_native = native_item(containment);
    const auto final_state = final_native ? final_native->wallpaper_state() : nullptr;
    callbacks.reset();
    window.hide();
    window.releaseResources();
    item->setParentItem(nullptr);
    window_owner.reset();
    if (final_state)
    {
        std::printf("teardown: renderer created=%u destroyed=%u\n",
                    final_state->renderer_created.load(), final_state->renderer_destroyed.load());
        if (final_state->renderer_created.load() != final_state->renderer_destroyed.load())
            throw std::runtime_error("renderer remained alive after window teardown");
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
        std::fprintf(stderr, "package probe: %s\n", error.what());
        return 1;
    }
}
