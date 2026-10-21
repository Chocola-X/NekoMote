#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QUrl>
#include <QUuid>
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
    QString instance_id;
    if (!QMetaObject::invokeMethod(item.get(), "createInstanceId",
                                   Q_RETURN_ARG(QString, instance_id)) ||
        QUuid(instance_id).isNull() || !item->setProperty("instanceId", instance_id) ||
        item->property("instanceId").toString() != instance_id)
        return 1;
    if (!item->setProperty("instanceId", QStringLiteral("../../outside")) ||
        item->property("status").toInt() != 2 ||
        item->property("instanceId").toString() != instance_id)
        return 1;
    if (!item->setProperty("instanceId", instance_id) || item->property("status").toInt() != 0)
        return 1;
    if (!item->setProperty("renderBackend", QStringLiteral("unknown")) ||
        item->property("status").toInt() != 2 || item->property("errorString").toString().isEmpty())
        return 1;
    std::puts("Staged QML module loaded; default state, stable instance UUID and invalid backend "
              "verified");
    return 0;
}
