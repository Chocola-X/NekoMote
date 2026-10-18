#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QUrl>
#include <cstdio>
#include <memory>
#include <string_view>

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
    QQmlPropertyMap configuration;
    configuration.insert(QStringLiteral("assetsDir"), QString{});
    configuration.insert(QStringLiteral("renderBackend"), QStringLiteral("opengl"));
    QQmlPropertyMap wallpaper;
    wallpaper.insert(QStringLiteral("configuration"),
                     QVariant::fromValue<QObject*>(&configuration));
    engine.rootContext()->setContextProperty(QStringLiteral("wallpaper"), &wallpaper);
    const QString pages = stage + "/share/plasma/wallpapers/uk.nekopara.nekomote/contents/ui/";
    for (const auto* page : {"main.qml", "config.qml"})
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
        }
    }
    std::puts("Plasma 5 staged module, context and configuration page imported successfully");
    return 0;
}
