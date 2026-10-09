#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
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
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

#include "wallpaper_item.hpp"

namespace
{
void wait_for(const std::function<bool()>& ready)
{
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(10);
    QObject::connect(&poll, &QTimer::timeout, &loop,
                     [&]
                     {
                         if (ready())
                             loop.quit();
                     });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    poll.start();
    loop.exec();
    if (!ready())
        throw std::runtime_error("timed out waiting for native frame progress");
}

void verify_playback(QQmlEngine& engine, const QString& model, const QString& session)
{
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport NekoMote.Wallpaper\n"
                      "NekoWallpaperItem { width: 128; height: 128; modelFile: '" +
                          QUrl::fromLocalFile(model).toString().toUtf8() + "'; sessionFile: '" +
                          QUrl::fromLocalFile(session).toString().toUtf8() + "'; }",
                      {});
    std::unique_ptr<QObject> object(component.create());
    auto* item = qobject_cast<nekomote::plasma::NekoWallpaperItem*>(object.get());
    if (!item || item->status() != 1 || !item->configure_diagnostics(0, false))
        throw std::runtime_error("playback fixture did not load");
    if (item->acceptHoverEvents() || item->acceptedMouseButtons() != Qt::NoButton)
        throw std::runtime_error("diagnostic setup did not isolate real pointer input");
    auto state = item->session_state();
    auto window = std::make_unique<QQuickWindow>();
    window->setFlags(Qt::Window | Qt::WindowDoesNotAcceptFocus);
    window->resize(128, 128);
    item->setParentItem(window->contentItem());
    std::vector<double> deltas;
    auto callbacks = std::make_unique<QObject>();
    QObject::connect(item, &nekomote::qt_quick::SessionItemBase::frame_ready, callbacks.get(),
                     [&](QSize, unsigned, double delta, const QString&)
                     { deltas.push_back(delta); });
    window->show();
    wait_for([&] { return deltas.size() >= 2 && deltas.back() > 0; });
    auto* timer = item->findChild<QTimer*>();
    if (!timer || !timer->isActive())
        throw std::runtime_error("visible normal playback scheduling did not start");
    item->setPaused(true);
    if (timer->isActive() ||
        !static_cast<nekomote::qt_quick::SessionItemBase*>(item)->get_item_state().paused)
        throw std::runtime_error("explicit pause did not suspend scheduling/runtime");
    const auto paused_count = deltas.size();
    wait_for([&] { return deltas.size() > paused_count; });
    const auto resume_count = deltas.size();
    item->setPaused(false);
    wait_for([&] { return deltas.size() > resume_count; });
    if (deltas[resume_count] != 0)
        throw std::runtime_error("pause resume replayed suspended wall-clock time");
    window->hide();
    if (timer->isActive() ||
        !static_cast<nekomote::qt_quick::SessionItemBase*>(item)->get_item_state().paused)
        throw std::runtime_error("hidden window did not suspend scheduling/runtime");
    window->show();
    wait_for([&] { return timer->isActive(); });
    item->failed(QStringLiteral("synthetic boundary failure"));
    QMouseEvent event(QEvent::MouseButtonPress, QPointF(64, 64), QPointF(64, 64), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(item, &event);
    const auto snapshot = static_cast<nekomote::qt_quick::SessionItemBase*>(item)->get_item_state();
    if (timer->isActive() || item->status() != 2 || !snapshot.paused || snapshot.input_count != 0)
        throw std::runtime_error("failed item still scheduled or accepted pointer input");
    callbacks.reset();
    window->hide();
    item->setParentItem(nullptr);
    object.reset();
    window.reset();
    if (state->renderer_created.load() != state->renderer_destroyed.load())
        throw std::runtime_error("playback renderer lifetime did not balance");
    std::puts(
        "normal playback: advance, pause/resume, hidden, failed, input isolation and cleanup PASS");
}

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
    auto callbacks = std::make_unique<QObject>();
    QObject::connect(&window, &QQuickWindow::frameSwapped, callbacks.get(),
                     [&]
                     {
                         if (grab)
                             return;
                         grab = item->grabToImage();
                         if (!grab)
                             return;
                         QObject::connect(
                             grab.get(), &QQuickItemGrabResult::ready, callbacks.get(),
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
    callbacks.reset();
    deadline.stop();
    const auto snapshot = static_cast<nekomote::qt_quick::SessionItemBase*>(item)->get_item_state();
    if (!snapshot.paused || snapshot.input_count != 0)
        throw std::runtime_error("unconfigured/failed item did not suspend runtime input");
    window.hide();
    window.releaseResources();
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
#ifdef NEKOMOTE_QML_IMPORT_PATH
    engine.addImportPath(QStringLiteral(NEKOMOTE_QML_IMPORT_PATH));
#endif
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
        const auto model = qEnvironmentVariable("NEKOMOTE_TEST_MODEL");
        const auto session = qEnvironmentVariable("NEKOMOTE_TEST_SESSION");
        if (!model.isEmpty() && !session.isEmpty())
            verify_playback(engine, model, session);
        else
            std::puts("normal playback contract: SKIPPED (set NEKOMOTE_TEST_MODEL and "
                      "NEKOMOTE_TEST_SESSION)");
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
