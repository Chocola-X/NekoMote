#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QUrl>
#include <cstdio>
#include <memory>

int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    if (argc != 3)
        return 1;
    QQmlEngine engine;
    engine.addImportPath(QString::fromLocal8Bit(argv[2]));
    QQmlComponent component(&engine);
    component.setData("import QtQuick 2.12\nimport NekoMote.Wallpaper 1.0\n"
                      "NekoWallpaperItem { width: 64; height: 64; }",
                      QUrl{});
    std::unique_ptr<QObject> item(component.create());
    if (!item || component.isError())
    {
        std::fprintf(stderr, "%s\n", component.errorString().toUtf8().constData());
        return 1;
    }
    if (item->property("status").toInt() != 0 ||
        item->property("renderBackend").toString() != "opengl")
        return 1;
    if (!item->setProperty("renderBackend", QStringLiteral("unknown")) ||
        item->property("status").toInt() != 2 || item->property("errorString").toString().isEmpty())
        return 1;
    std::puts("Staged QML module loaded; default state and invalid backend verified");
    return 0;
}
