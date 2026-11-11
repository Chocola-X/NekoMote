#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QUrl>
#include <QUuid>
#include <cstdio>
#include <memory>
#include <string_view>

class ProbeConfiguration final : public QQmlPropertyMap
{
    Q_OBJECT
  public:
    ProbeConfiguration() : QQmlPropertyMap(this, nullptr)
    {
    }
    Q_INVOKABLE void writeConfig()
    {
        ++writes;
    }
    unsigned writes{0};
};

// Load the installed module (including its relative RPATH), both package pages,
// and the Plasma 5 context contract. No copyrighted assets or display are needed.
int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    if (argc != 3)
        return 1;
    QQmlEngine engine;
    const QString stage = QString::fromLocal8Bit(argv[1]);
    engine.addImportPath(QString::fromLocal8Bit(argv[2]));
    ProbeConfiguration configuration;
    configuration.insert(QStringLiteral("assetsDir"), QString{});
    configuration.insert(QStringLiteral("panelInstanceId"), QString{});
    configuration.insert(QStringLiteral("renderBackend"), QStringLiteral("opengl"));
    QQmlPropertyMap wallpaper;
    wallpaper.insert(QStringLiteral("configuration"),
                     QVariant::fromValue<QObject*>(&configuration));
    engine.rootContext()->setContextProperty(QStringLiteral("wallpaper"), &wallpaper);
    QQmlComponent containment_component(
        &engine,
        QUrl::fromLocalFile(stage + "/share/plasma/plasmoids/org.nekomote.validation/contents/ui/"
                                    "main.qml"));
    std::unique_ptr<QObject> containment(containment_component.create());
    if (!containment || containment_component.isError())
    {
        std::fprintf(stderr, "containment: %s\n",
                     containment_component.errorString().toUtf8().constData());
        return 1;
    }
    const QString pages = stage + "/share/plasma/wallpapers/uk.nekopara.nekomote/contents/ui/";
    QString persisted_id;
    for (const auto* page : {"main.qml", "main.qml", "config.qml"})
    {
        QQmlComponent component(&engine, QUrl::fromLocalFile(pages + page));
        std::unique_ptr<QObject> object(component.create());
        if (!object || component.isError())
        {
            std::fprintf(stderr, "%s: %s\n", page, component.errorString().toUtf8().constData());
            return 1;
        }
        if (std::string_view(page) == "main.qml")
        {
            auto* native = object->findChild<QQuickItem*>(QStringLiteral("nativeSession"));
            if (!native || native->property("status").toInt() != 0 ||
                native->property("renderBackend").toString() != "opengl")
                return 1;
            const auto id = configuration.value(QStringLiteral("panelInstanceId")).toString();
            if (configuration.writes != 1 || QUuid(id).isNull() ||
                native->property("instanceId").toString() != id ||
                (!persisted_id.isEmpty() && persisted_id != id))
                return 1;
            persisted_id = id;
            // Applying the initial wallpaper configuration can replace the
            // property's map while keeping the QML item alive.
            ProbeConfiguration replacement;
            replacement.insert(QStringLiteral("assetsDir"), QStringLiteral("/assets/first-use"));
            replacement.insert(QStringLiteral("panelInstanceId"), QString{});
            replacement.insert(QStringLiteral("renderBackend"), QStringLiteral("opengl"));
            wallpaper.insert(QStringLiteral("configuration"),
                             QVariant::fromValue<QObject*>(&replacement));
            QCoreApplication::processEvents();
            const auto replacement_id =
                replacement.value(QStringLiteral("panelInstanceId")).toString();
            if (QUuid(replacement_id).isNull() || replacement.writes != 1 ||
                native->property("instanceId").toString() != replacement_id ||
                native->property("assetsDir").toString() != "/assets/first-use" ||
                native->property("status").toInt() != 1)
            {
                std::fprintf(stderr,
                             "First-use config replacement stalled: id=%s assets=%s status=%d\n",
                             replacement_id.toUtf8().constData(),
                             native->property("assetsDir").toString().toUtf8().constData(),
                             native->property("status").toInt());
                return 1;
            }
            // Settled bindings and subsequent asset edits must retain the ID.
            replacement.insert(QStringLiteral("assetsDir"), QStringLiteral("/assets/edited"));
            QCoreApplication::processEvents();
            if (replacement.writes != 1 ||
                replacement.value(QStringLiteral("panelInstanceId")).toString() != replacement_id ||
                native->property("assetsDir").toString() != "/assets/edited")
                return 1;
            wallpaper.insert(QStringLiteral("configuration"),
                             QVariant::fromValue<QObject*>(&configuration));
            QCoreApplication::processEvents();
            if (configuration.writes != 1 || native->property("instanceId").toString() != id)
                return 1;
        }
    }
    std::puts("Plasma 5 staged module, containment, context and configuration page imported "
              "successfully");
    return 0;
}

#include "test_plasma5_package.moc"
