#include <QEventLoop>
#include <QGuiApplication>
#include <QProcess>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <fstream>
#include <stdexcept>

#include "wallpaper_item.hpp"

namespace
{
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
void wait(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
void control(const char* action)
{
    require(QProcess::execute("busctl",
                              {"--address=" + qEnvironmentVariable("NEKOMOTE_TEST_SESSION_BUS"),
                               "call", "org.freedesktop.ScreenSaver", "/ScreenSaver",
                               "nekomote.Test", "Set", "s", QString::fromUtf8(action)}) == 0,
            "private session control failed");
}
class Item : public nekomote::plasma::NekoWallpaperItem
{
  public:
    using NekoWallpaperItem::componentComplete;
};
} // namespace
int main(int argc, char** argv)
{
    const auto address = qEnvironmentVariable("NEKOMOTE_TEST_SESSION_BUS");
    if (address.isEmpty() || address != qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS") ||
        address != qEnvironmentVariable("DBUS_SYSTEM_BUS_ADDRESS"))
    {
        std::puts("SKIPPED: requires the audit's private session bus");
        return 77;
    }
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication application(argc, argv);
    try
    {
        QTemporaryDir directory;
        require(directory.isValid(), "temporary directory failed");
        const auto config = directory.filePath("panel.json");
        std::ofstream(config.toStdString())
            << R"({"version":1,"character":"chocola/date-a","mute":true,"fps":60})";
        Item item;
        auto state = item.wallpaper_state();
        state->no_save = true;
        state->config_path = config.toStdString();
        item.setRenderBackend(qEnvironmentVariable("NEKOMOTE_TEST_BACKEND", "opengl"));
        item.setSize({640, 480});
        item.componentComplete();
        item.setAssetsDir(qEnvironmentVariable("NEKOMOTE_TEST_ASSETS_DIR"));
        QQuickWindow window;
        window.setFlags(Qt::Window | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
        window.resize(640, 480);
        item.setParentItem(window.contentItem());
        window.show();
        wait(2500);
        require(item.status() == 1 && item.queryRenderedFrames() > 3, "wallpaper did not load");
        const auto assert_stopped = [&]
        {
            const auto before = item.queryRenderedFrames();
            wait(1200);
            require(item.queryRenderedFrames() == before, "frames continued while suspended");
        };
        control("lock");
        wait(1600);
        assert_stopped();
        control("unlock");
        const auto locked_frames = item.queryRenderedFrames();
        wait(1600);
        require(item.queryRenderedFrames() > locked_frames + 3, "unlock did not resume");
        control("sleep");
        wait(1600);
        assert_stopped();
        item.setPauseOnSessionEvents(false);
        const auto asleep_frames = item.queryRenderedFrames();
        wait(500);
        require(item.queryRenderedFrames() > asleep_frames + 3, "disabling pause did not resume");
        control("wake");
        control("lock");
        const auto disabled_frames = item.queryRenderedFrames();
        wait(1600);
        require(item.queryRenderedFrames() > disabled_frames + 3, "disabled pause blocked lock");
        item.setPauseOnSessionEvents(true);
        wait(2500);
        assert_stopped(); // Re-enabling must query a lock that is already active.
        control("unlock");
        wait(1600);
        // Host resume must preserve a user's separate pause preference.
        item.setPaused(true);
        control("lock");
        wait(1500);
        control("unlock");
        wait(1500);
        assert_stopped();
        item.setPaused(false);
        wait(500);
        const auto resumed = item.queryRenderedFrames();
        wait(500);
        require(item.queryRenderedFrames() > resumed, "user resume did not work");
        window.hide();
        wait(300);
        control("lock");
        wait(1500);
        control("unlock");
        wait(1500);
        assert_stopped(); // Unlock alone cannot resume a hidden host.
        const auto hidden_frames = item.queryRenderedFrames();
        window.show();
        wait(1500);
        require(item.queryRenderedFrames() > hidden_frames + 3,
                "window re-exposure did not recover");
        item.setParentItem(nullptr);
        window.releaseResources();
        std::puts("PASS: Plasma lock/unlock stops frames, preserves user pause and hidden host");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
