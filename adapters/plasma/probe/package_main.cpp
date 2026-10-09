#include <KConfigGroup>
#include <KPackage/Package>
#include <KPackage/PackageLoader>
#include <Plasma/Applet>
#include <Plasma/Containment>
#include <Plasma/Corona>
#include <PlasmaQuick/AppletQuickItem>
#include <QApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>

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

int run(int argc, char** argv)
{
    if (argc != 4)
        throw std::runtime_error("package probe: MODEL SESSION NEW_OUTPUT_DIRECTORY required");
    const auto model = std::filesystem::absolute(argv[1]);
    const auto session = std::filesystem::absolute(argv[2]);
    const auto output = std::filesystem::absolute(argv[3]);
    if (!std::filesystem::is_regular_file(model) || !std::filesystem::is_regular_file(session) ||
        std::filesystem::exists(output))
        throw std::runtime_error("inputs must exist and output must be a new directory");
    std::filesystem::create_directories(output / "data/plasma/wallpapers");
    std::filesystem::create_directories(output / "data/plasma/plasmoids");
    std::filesystem::copy(NEKOMOTE_PACKAGE_PATH,
                          output / "data/plasma/wallpapers/org.nekomote.wallpaper",
                          std::filesystem::copy_options::recursive);
    std::filesystem::copy(NEKOMOTE_CONTAINMENT_PATH,
                          output / "data/plasma/plasmoids/org.nekomote.validation",
                          std::filesystem::copy_options::recursive);
    // Only this diagnostic process uses workspace-local KDE state. The user's
    // installed packages, shell and desktop configuration are untouched.
    qputenv("XDG_DATA_HOME", QByteArray::fromStdString((output / "data").string()));
    qputenv("XDG_CONFIG_HOME", QByteArray::fromStdString((output / "config").string()));
    qputenv("XDG_CACHE_HOME", QByteArray::fromStdString((output / "cache").string()));
    qputenv("QML_IMPORT_PATH", QByteArray(NEKOMOTE_QML_IMPORT_PATH));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    int qt_argc = 1;
    char* qt_argv[] = {argv[0], nullptr};
    QApplication app(qt_argc, qt_argv);
    QCoreApplication::setApplicationName(QStringLiteral("nekomote_package_probe"));
    auto package = KPackage::PackageLoader::self()->loadPackage(QStringLiteral("Plasma/Wallpaper"));
    package.setPath(QString::fromStdString(
        (output / "data/plasma/wallpapers/org.nekomote.wallpaper").string()));
    if (!package.isValid())
        throw std::runtime_error("staged Plasma/Wallpaper KPackage is invalid");
    const auto config_path = package.filePath("ui", "config.qml");
    const auto schema_path = package.filePath("config", "main.xml");
    if (config_path.isEmpty() || schema_path.isEmpty())
        throw std::runtime_error("wallpaper configuration page/schema missing");
    QQmlEngine config_engine;
    QQmlComponent config_component(&config_engine, QUrl::fromLocalFile(config_path));
    std::unique_ptr<QObject> config_object(config_component.createWithInitialProperties(
        {{"cfg_modelFile", QUrl::fromLocalFile(QString::fromStdString(model.string())).toString()},
         {"cfg_sessionFile",
          QUrl::fromLocalFile(QString::fromStdString(session.string())).toString()}}));
    if (!config_object)
        throw std::runtime_error("wallpaper configuration QML failed: " +
                                 config_component.errorString().toStdString());
    std::printf("KPackage config page=%s schema=%s: loaded\n", config_path.toUtf8().constData(),
                schema_path.toUtf8().constData());
    ProbeCorona corona;
    auto* containment = corona.createContainment(QStringLiteral("org.nekomote.validation"));
    if (!containment)
        throw std::runtime_error("validation containment could not be created");
    auto config =
        containment->config().group("Wallpaper").group("org.nekomote.wallpaper").group("General");
    config.writeEntry("modelFile",
                      QUrl::fromLocalFile(QString::fromStdString(model.string())).toString());
    config.writeEntry("sessionFile",
                      QUrl::fromLocalFile(QString::fromStdString(session.string())).toString());
    containment->setWallpaperPlugin(QStringLiteral("org.nekomote.wallpaper"));
    auto* item = PlasmaQuick::AppletQuickItem::itemForApplet(containment);
    if (!item)
        throw std::runtime_error("containment QML item could not be created");
    QQmlEngine::contextForObject(item)->engine()->addImportPath(
        QStringLiteral(NEKOMOTE_QML_IMPORT_PATH));
    QQuickWindow window;
    window.setFlags(Qt::Window | Qt::WindowDoesNotAcceptFocus);
    window.setColor(Qt::transparent);
    window.resize(800, 1080);
    item->setParentItem(window.contentItem());
    item->setSize({800, 1080});
    int result = 1;
    QObject callbacks;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &callbacks,
                     [&]
                     {
                         std::fprintf(stderr, "package initialization/capture timed out\n");
                         app.quit();
                     });
    QSharedPointer<QQuickItemGrabResult> grab;
    QTimer poll;
    poll.setInterval(100);
    QObject::connect(
        &poll, &QTimer::timeout, &callbacks,
        [&]
        {
            auto* wallpaper = qobject_cast<QQuickItem*>(
                containment->property("wallpaperGraphicsObject").value<QObject*>());
            if (!wallpaper || grab)
                return;
            auto* native = wallpaper->findChild<QObject*>(QStringLiteral("nativeSession"));
            if (!native)
                return;
            const auto status = native->property("status").toInt();
            if (status == 2 || status == 3)
            {
                std::fprintf(stderr, "package native status=%d: %s\n", status,
                             native->property("errorString").toString().toUtf8().constData());
                app.quit();
                return;
            }
            if (status != 1)
                return;
            std::printf("actual WallpaperItem: %s, native status=%d, live diagnostic capture, "
                        "dynamic package=%s\n",
                        wallpaper->metaObject()->className(), native->property("status").toInt(),
                        package.path().toUtf8().constData());
            grab = window.contentItem()->grabToImage();
            if (!grab)
                return;
            QObject::connect(grab.get(), &QQuickItemGrabResult::ready, &callbacks,
                             [&]
                             {
                                 if (!grab->image().isNull() &&
                                     grab->image().save(
                                         QString::fromStdString((output / "package.png").string())))
                                     result = 0;
                                 app.quit();
                             });
        });
    window.show();
    poll.start();
    deadline.start(15000);
    app.exec();
    poll.stop();
    deadline.stop();
    item->setParentItem(nullptr);
    return result;
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
