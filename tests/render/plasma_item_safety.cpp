#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>

#include "wallpaper_item.hpp"

namespace
{
void verify(QQmlEngine& engine, const QByteArray& properties, int expected_status)
{
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport NekoMote.Wallpaper\n"
                      "NekoWallpaperItem { width: 128; height: 128; " +
                          properties + " }",
                      {});
    std::unique_ptr<QObject> object(component.create());
    auto* item = qobject_cast<nekomote::plasma::NekoWallpaperItem*>(object.get());
    if (!item || item->status() != expected_status)
        throw std::runtime_error("QML configuration status incorrect");
    QQuickWindow window;
    window.setFlags(Qt::Window | Qt::WindowDoesNotAcceptFocus);
    window.setColor(Qt::transparent);
    window.resize(128, 128);
    item->setParentItem(window.contentItem());
    bool completed = false;
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QSharedPointer<QQuickItemGrabResult> grab;
    QObject::connect(&window, &QQuickWindow::frameSwapped, &loop,
                     [&]
                     {
                         if (grab)
                             return;
                         grab = item->grabToImage();
                         if (!grab)
                             return;
                         QObject::connect(
                             grab.get(), &QQuickItemGrabResult::ready, &loop,
                             [&]
                             {
                                 const auto image =
                                     grab->image().convertToFormat(QImage::Format_RGBA8888);
                                 completed = !image.isNull() && item->status() == expected_status;
                                 for (int y = 0; y < image.height(); ++y)
                                     for (int x = 0; x < image.width(); ++x)
                                         if (image.pixelColor(x, y).alpha() != 0)
                                             completed = false;
                                 loop.quit();
                             });
                     });
    window.show();
    deadline.start(5000);
    loop.exec();
    item->setParentItem(nullptr);
    if (!completed)
        throw std::runtime_error("empty/failed visible item did not produce transparent content");
    std::printf("visible status=%d: transparent, host alive\n", expected_status);
}
} // namespace

int main(int argc, char** argv)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication application(argc, argv);
    QQmlEngine engine;
    QTemporaryDir directory;
    if (!directory.isValid())
        return 1;
    const auto invalid = directory.filePath("invalid.json");
    std::ofstream(invalid.toStdString()) << "{\"schema_version\":999999}";
    try
    {
        verify(engine, {}, 0);
        const auto missing = QUrl::fromLocalFile(directory.filePath("missing.json")).toString();
        verify(engine,
               "modelFile: '" + missing.toUtf8() + "'; sessionFile: '" + missing.toUtf8() + "';",
               2);
        const auto bad_url = QUrl::fromLocalFile(invalid).toString();
        verify(engine,
               "modelFile: '" + bad_url.toUtf8() + "'; sessionFile: '" + bad_url.toUtf8() + "';",
               2);
        verify(engine, "scale: -1;", 2);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
