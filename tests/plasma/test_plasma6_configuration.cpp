#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>
#include <cstdio>
#include <memory>

class ProbeConfiguration final : public QQmlPropertyMap
{
    Q_OBJECT
  public:
    ProbeConfiguration() : QQmlPropertyMap(this, nullptr)
    {
        insert("assetsDir", QStringLiteral("/assets/configuration-test"));
        insert("panelInstanceId", QString{});
        insert("renderBackend", QStringLiteral("opengl"));
    }
    Q_INVOKABLE void writeConfig()
    {
        ++writes;
    }
    unsigned writes{0};
};

namespace
{
void settle()
{
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}
} // namespace

// Only the host's configuration property is substituted. The real package QML
// and staged native plugin run unchanged; no display or copyrighted assets.
int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    if (argc != 3)
        return 1;
    QTemporaryDir fixture;
    QDir directory(fixture.path());
    if (!directory.mkpath("org/kde/plasma/plasmoid"))
        return 1;
    QFile qmldir(directory.filePath("org/kde/plasma/plasmoid/qmldir"));
    QFile host(directory.filePath("org/kde/plasma/plasmoid/WallpaperItem.qml"));
    if (!qmldir.open(QIODevice::WriteOnly) || !host.open(QIODevice::WriteOnly))
        return 1;
    qmldir.write("module org.kde.plasma.plasmoid\nWallpaperItem 1.0 WallpaperItem.qml\n");
    host.write("import QtQuick\nItem { property var configuration }\n");
    qmldir.close();
    host.close();
    QQmlEngine engine;
    engine.addImportPath(QString::fromLocal8Bit(argv[1]));
    engine.addImportPath(fixture.path());
    ProbeConfiguration initial;
    ProbeConfiguration replacement;
    replacement.insert("renderBackend", QStringLiteral("vulkan"));
    QQmlComponent component(&engine, QUrl::fromLocalFile(QString::fromLocal8Bit(argv[2])));
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{"configuration", QVariant::fromValue<QObject*>(&initial)}}));
    if (!object || component.isError())
    {
        std::fprintf(stderr, "%s\n", qPrintable(component.errorString()));
        return 1;
    }
    auto* native = object->findChild<QQuickItem*>("nativeSession");
    if (!native || native->property("status").toInt() != 1 || initial.writes != 1)
        return 2;
    const auto initial_id = native->property("instanceId").toString();
    if (QUuid(initial_id).isNull())
        return 2;
    // Applying backend settings may replace/reset the configuration map without
    // recreating the wallpaper item. Its asset gate must recover on that item.
    object->setProperty("configuration", QVariant::fromValue<QObject*>(&replacement));
    settle();
    const auto id = replacement.value("panelInstanceId").toString();
    if (id != initial_id || replacement.writes != 1 ||
        native->property("instanceId").toString() != id ||
        native->property("assetsDir").toString() != "/assets/configuration-test" ||
        native->property("renderBackend").toString() != "vulkan" ||
        native->property("status").toInt() != 1)
    {
        std::fprintf(stderr, "Backend apply stalled: id=%s assets=%s backend=%s status=%d\n",
                     qPrintable(id), qPrintable(native->property("assetsDir").toString()),
                     qPrintable(native->property("renderBackend").toString()),
                     native->property("status").toInt());
        return 3;
    }
    replacement.insert("renderBackend", QStringLiteral("opengl"));
    settle();
    if (replacement.writes != 1 || native->property("instanceId").toString() != id ||
        native->property("renderBackend").toString() != "opengl")
        return 4;
    replacement.insert("panelInstanceId", QString{});
    settle();
    if (replacement.writes != 2 || native->property("instanceId").toString() != initial_id ||
        native->property("status").toInt() != 1)
        return 5;
    // A deferred repair must consult the latest map and preserve an existing ID.
    ProbeConfiguration transient;
    object->setProperty("configuration", QVariant::fromValue<QObject*>(&transient));
    object->setProperty("configuration", QVariant::fromValue<QObject*>(&initial));
    settle();
    if (initial.writes != 1 || transient.writes != 0 ||
        native->property("instanceId").toString() != initial_id)
        return 6;
    ProbeConfiguration explicit_identity;
    const auto explicit_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    explicit_identity.insert("panelInstanceId", explicit_id);
    object->setProperty("configuration", QVariant::fromValue<QObject*>(&explicit_identity));
    settle();
    if (explicit_identity.writes != 0 || native->property("instanceId").toString() != explicit_id)
        return 7;
    explicit_identity.insert("panelInstanceId", QString{});
    settle();
    if (explicit_identity.writes != 1 || native->property("instanceId").toString() != explicit_id)
        return 7;
    object.reset();
    object.reset(component.createWithInitialProperties(
        {{"configuration", QVariant::fromValue<QObject*>(&initial)}}));
    if (!object || component.isError())
        return 7;
    native = object->findChild<QQuickItem*>("nativeSession");
    if (!native || initial.writes != 1 || native->property("instanceId").toString() != initial_id ||
        native->property("status").toInt() != 1)
        return 7;
    std::puts("Plasma6 configuration replacement/reset and GL/Vulkan selection PASS; UUID gate "
              "recovered");
    return 0;
}

#include "test_plasma6_configuration.moc"
