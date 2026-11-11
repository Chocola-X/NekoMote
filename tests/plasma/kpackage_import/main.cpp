#include <QGuiApplication>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <cstdio>
#include <memory>

// Run in a fresh process with both QML import environment variables unset.
// The settings page goes first to exercise its translation-module import.
int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 2)
        return 1;
    const auto ui = QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1]) + "/contents/ui/");
    QQmlEngine engine;
    QQmlComponent settings(&engine, ui.resolved(QUrl("config.qml")));
    std::unique_ptr<QObject> page(settings.create());
    if (!page || settings.isError())
    {
        std::fprintf(stderr, "%s\n", settings.errorString().toUtf8().constData());
        return 1;
    }
    QQmlComponent native(&engine);
    native.setData("import QtQuick 2.12\nimport \"NekoMote/Wallpaper\" 1.0\n"
                   "NekoWallpaperItem { width: 64; height: 64 }",
                   ui.resolved(QUrl("probe.qml")));
    std::unique_ptr<QObject> item(native.create());
    if (!item || native.isError() || item->property("status").toInt() != 0 ||
        item->property("renderBackend").toString() != "opengl")
    {
        std::fprintf(stderr, "%s\n", native.errorString().toUtf8().constData());
        return 1;
    }
    std::puts("Settings-first and native item directory imports PASS without a global module path");
    return 0;
}
