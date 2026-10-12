#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>

#include "wallpaper_item.hpp"

namespace
{
class InputItem final : public nekomote::plasma::NekoWallpaperItem
{
  public:
    using NekoWallpaperItem::componentComplete;
    using NekoWallpaperItem::mouseMoveEvent;
    using NekoWallpaperItem::mousePressEvent;
    using NekoWallpaperItem::mouseReleaseEvent;
    using NekoWallpaperItem::wheelEvent;
};
void require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

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
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    poll.start();
    loop.exec();
    require(ready(), "timed out waiting for wallpaper");
}

QImage capture(QQuickItem* item, const QString& path)
{
    auto grab = item->grabToImage();
    require(bool(grab), "grabToImage failed");
    bool ready = false;
    QObject callbacks;
    QObject::connect(grab.get(), &QQuickItemGrabResult::ready, &callbacks, [&] { ready = true; });
    wait_for([&] { return ready; });
    auto image = grab->image().convertToFormat(QImage::Format_RGBA8888);
    require(!image.isNull() && image.save(path), "capture save failed");
    return image;
}

int changed_pixels(const QImage& before, const QImage& after, const QRect& area)
{
    int count = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            count += before.pixelColor(x, y) != after.pixelColor(x, y);
    return count;
}
} // namespace

int main(int argc, char** argv)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc, argv);
    const auto source = qEnvironmentVariable("NEKOMOTE_TEST_ASSETS_DIR");
    if (!std::filesystem::exists(source.toStdString() +
                                 "/characters/chocola/date-a/model.nkm.json"))
    {
        std::puts("SKIPPED: set NEKOMOTE_TEST_ASSETS_DIR with chocola/date-a");
        return 77;
    }
    QTemporaryDir directory;
    require(directory.isValid(), "temporary directory failed");
    const auto output = qEnvironmentVariable("NEKOMOTE_LAYER_EVIDENCE", directory.path());
    std::filesystem::create_directories(output.toStdString());
    try
    {
        namespace fs = std::filesystem;
        const fs::path assets(directory.path().toStdString());
        fs::create_directories(assets / "characters" / "chocola");
        fs::create_directories(assets / "backgrounds");
        fs::create_directories(assets / "icons");
        fs::create_directory_symlink(fs::absolute(source.toStdString()) /
                                         "characters/chocola/date-a",
                                     assets / "characters/chocola/date-a");
        QImage icon(44, 44, QImage::Format_RGBA8888);
        icon.fill(QColor(255, 0, 255));
        const bool no_icon = qEnvironmentVariableIsSet("NEKOMOTE_TEST_NO_ICON");
        if (!no_icon)
            require(icon.save(directory.filePath("icons/toggle.png")), "icon fixture failed");
        QImage background(800, 900, QImage::Format_RGBA8888);
        background.fill(QColor(20, 90, 220));
        for (int y = 0; y < 450; ++y)
            for (int x = 0; x < 800; ++x)
                background.setPixelColor(x, y, QColor(220, 60, 20));
        require(background.save(directory.filePath("backgrounds/test.png")),
                "background fixture failed");
        background.fill(QColor(20, 180, 70));
        require(background.save(directory.filePath("backgrounds/second.png")),
                "second fixture failed");
        // Keep settings scrollable even though this fixture has just one group.
        background.fill(QColor(50, 50, 50));
        for (int i = 0; i < 8; ++i)
            require(
                background.save(directory.filePath(QString("backgrounds/z-extra-%1.png").arg(i))),
                "scroll background fixture failed");
        {
            std::ofstream config(assets / "config.json");
            config
                << R"({"character":"chocola/date-a","background":"test","mute":true,"fit":true})";
        }
        auto object = std::make_unique<InputItem>();
        auto* item = object.get();
        item->setWidth(800);
        item->setHeight(900);
        item->setMuted(true);
        const auto backend = qEnvironmentVariable("NEKOMOTE_TEST_BACKEND", "opengl");
        item->setRenderBackend(backend);
        // Controlled replay uses only these callbacks; ignore the actual desktop pointer.
        item->setAcceptHoverEvents(false);
        item->setAcceptedMouseButtons(Qt::NoButton);
        item->componentComplete();
        item->setAssetsDir(directory.path());
        if (qEnvironmentVariableIsSet("NEKOMOTE_LAYER_TRACE"))
            item->wallpaper_state()->trace_frames = 256;
        QQuickWindow window;
        window.setFlags(Qt::Window | Qt::WindowDoesNotAcceptFocus);
        window.resize(800, 900);
        item->setParentItem(window.contentItem());
        window.show();
        unsigned timer_ticks = 0;
        QObject::connect(item->findChild<QTimer*>(), &QTimer::timeout, &window,
                         [&] { ++timer_ticks; });
        const auto frames = [&](unsigned n)
        {
            const auto target = item->queryRenderedFrames() + n;
            try
            {
                wait_for([&]
                         { return item->queryRenderedFrames() >= target || item->status() == 2; });
            }
            catch (...)
            {
                std::fprintf(stderr,
                             "frames=%u target=%u ticks=%u status=%d visible=%d exposed=%d "
                             "paused=%d timer=%d: %s\n",
                             item->queryRenderedFrames(), target, timer_ticks, item->status(),
                             window.isVisible(), window.isExposed(),
                             static_cast<nekomote::qt_quick::WallpaperItemBase*>(item)
                                 ->get_item_state()
                                 .paused,
                             item->findChild<QTimer*>()->isActive(),
                             qPrintable(item->errorString()));
                throw;
            }
            require(item->status() == 1, qPrintable(item->errorString()));
        };
        frames(3);
        const auto initial = capture(item, output + "/initial.png");
        require(initial.pixelColor(2, 2) == QColor(220, 60, 20),
                "top background missing or inverted");
        require(initial.pixelColor(2, initial.height() / 2 + 20) == QColor(20, 90, 220),
                "bottom background missing or inverted");
        const auto icon_pixel = initial.pixelColor(initial.width() - 30, 30);
        require(no_icon ? icon_pixel != QColor(220, 60, 20)
                        : icon_pixel.red() > 240 && icon_pixel.blue() > 220,
                "UI icon missing from host FBO");
        // The window manager may resize the top-level window independently;
        // pointer conversion must use the wallpaper item's logical extent.
        const double dpi = static_cast<double>(initial.width()) / item->width();
        const auto click = [&](QPointF position)
        {
            QMouseEvent press(QEvent::MouseButtonPress, position / dpi, position / dpi,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            item->mousePressEvent(&press);
            require(static_cast<nekomote::qt_quick::WallpaperItemBase*>(item)
                            ->get_item_state()
                            .input_count > 0,
                    "press callback did not enqueue");
            frames(2);
            QMouseEvent release(QEvent::MouseButtonRelease, position / dpi, position / dpi,
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            item->mouseReleaseEvent(&release);
            frames(12);
        };
        click({static_cast<double>(initial.width()) - 30, 30});
        const auto open = capture(item, output + "/open.png");
        require(open.pixelColor(20, 70) != initial.pixelColor(20, 70),
                "UI panel did not open via Qt events");
        // The isolated catalog has one character group, followed by settings.
        click({140, 115});
        const auto settings = capture(item, output + "/settings.png");
        require(changed_pixels(open, settings, {20, 440, 220, 350}) > 5000,
                "settings did not expand");
        // First static background row (second.png sorts before test.png).
        click({140, 502});
        frames(35);
        // The shared background transition is 1.8 seconds. Frame count alone
        // does not establish elapsed time when Qt also schedules grab frames.
        QEventLoop settle_background;
        QTimer::singleShot(2000, &settle_background, &QEventLoop::quit);
        settle_background.exec();
        frames(2);
        const auto switched = capture(item, output + "/background-switched.png");
        require(switched.pixelColor(2, 2) == QColor(20, 180, 70) &&
                    switched.pixelColor(2, initial.height() / 2 + 20) == QColor(20, 180, 70),
                "UI background selection failed");
        QWheelEvent wheel(QPointF(140, 500) / dpi, QPointF(140, 500) / dpi, {}, QPoint(0, -240),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        item->wheelEvent(&wheel);
        frames(3);
        const auto scrolled = capture(item, output + "/scrolled.png");
        require(changed_pixels(switched, scrolled, {20, 60, 220, 700}) > 1000,
                "UI wheel not forwarded");
        item->setWidth(640);
        item->setHeight(720);
        window.resize(640, 720);
        frames(4);
        const auto resized = capture(item, output + "/resized.png");
        require(resized.size() == QSize(static_cast<int>(640 * dpi), static_cast<int>(720 * dpi)),
                "FBO resize failed");
        const auto resized_icon = resized.pixelColor(resized.width() - 30, 30);
        require(no_icon ? resized_icon != QColor(20, 180, 70)
                        : resized_icon.red() > 200 && resized_icon.blue() > 200,
                "UI icon did not follow output resize");
        const QPointF from(resized.width() - 30, 30);
        const QPointF to(resized.width() - 130, 100);
        QMouseEvent press(QEvent::MouseButtonPress, from / dpi, from / dpi, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        item->mousePressEvent(&press);
        frames(2);
        QMouseEvent move(QEvent::MouseMove, to / dpi, to / dpi, Qt::NoButton, Qt::LeftButton,
                         Qt::NoModifier);
        item->mouseMoveEvent(&move);
        frames(2);
        QMouseEvent release(QEvent::MouseButtonRelease, to / dpi, to / dpi, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        item->mouseReleaseEvent(&release);
        frames(3);
        const auto dragged = capture(item, output + "/dragged.png");
        require(dragged.pixelColor(from.toPoint()) == QColor(20, 180, 70),
                "icon drag did not clear its original position");
        const auto dragged_icon = dragged.pixelColor(to.toPoint());
        require(no_icon ? dragged_icon != resized.pixelColor(to.toPoint())
                        : dragged_icon.red() > 200 && dragged_icon.blue() > 200,
                "pressed mouse move did not drag the UI icon");
        // Hold on the model, then move in output pixels. The UI is left of x=272.
        const QPointF model_from(420, 360), model_to(460, 390);
        // Settle gaze before the reference image: dragging keeps the pointer's
        // position relative to the model, rather than initiating a new gaze pose.
        QMouseEvent model_hover(QEvent::MouseMove, model_from / dpi, model_from / dpi, Qt::NoButton,
                                Qt::NoButton, Qt::NoModifier);
        item->mouseMoveEvent(&model_hover);
        frames(12);
        const auto before_model_drag = capture(item, output + "/before-model-drag.png");
        const auto event_time =
            static_cast<ulong>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
        QMouseEvent model_press(QEvent::MouseButtonPress, model_from / dpi, model_from / dpi,
                                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        model_press.setTimestamp(event_time);
        item->mousePressEvent(&model_press);
        // Replay delayed press/move/release together; their collection times,
        // preserving event order; held movement must translate exactly once.
        QMouseEvent model_move(QEvent::MouseMove, model_to / dpi, model_to / dpi, Qt::NoButton,
                               Qt::LeftButton, Qt::NoModifier);
        model_move.setTimestamp(event_time + 350);
        item->mouseMoveEvent(&model_move);
        QMouseEvent model_release(QEvent::MouseButtonRelease, model_to / dpi, model_to / dpi,
                                  Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        model_release.setTimestamp(event_time + 360);
        item->mouseReleaseEvent(&model_release);
        require(static_cast<nekomote::qt_quick::WallpaperItemBase*>(item)
                        ->get_item_state()
                        .input_count >= 3,
                "long hold replay was not batched");
        frames(2);
        const auto model_dragged = capture(item, output + "/model-dragged.png");
        long long shifted_error = 0, unmoved_error = 0;
        for (int y = 200; y < 500; ++y)
            for (int x = 280; x < 590; ++x)
            {
                const auto before = before_model_drag.pixelColor(x, y);
                const auto shifted = model_dragged.pixelColor(x + 40, y + 30);
                const auto unmoved = model_dragged.pixelColor(x, y);
                shifted_error += std::abs(before.red() - shifted.red()) +
                                 std::abs(before.green() - shifted.green()) +
                                 std::abs(before.blue() - shifted.blue());
                unmoved_error += std::abs(before.red() - unmoved.red()) +
                                 std::abs(before.green() - unmoved.green()) +
                                 std::abs(before.blue() - unmoved.blue());
            }
        require(shifted_error * 2 < unmoved_error,
                "left hold did not translate model by the pointer displacement");
        QWheelEvent model_wheel(model_to / dpi, model_to / dpi, {}, QPoint(0, 120), Qt::NoButton,
                                Qt::NoModifier, Qt::NoScrollPhase, false);
        model_wheel.setTimestamp(event_time + 370);
        item->wheelEvent(&model_wheel);
        frames(2);
        const auto model_scaled = capture(item, output + "/model-scaled.png");
        require(changed_pixels(model_dragged, model_scaled, {280, 200, 310, 300}) > 10000,
                "wheel outside UI did not scale model");
        if (qEnvironmentVariableIsSet("NEKOMOTE_TEST_DUAL_BACKEND"))
        {
            const auto other =
                backend == "vulkan" ? QStringLiteral("opengl") : QStringLiteral("vulkan");
            for (const auto& selected : {other, backend})
            {
                item->setRenderBackend(selected);
                frames(3);
                const auto rebuilt = capture(item, output + "/backend-" + selected + ".png");
                require(item->renderBackend() == selected && item->errorString().isEmpty(),
                        "backend selection did not recover the item");
                require(rebuilt.pixelColor(2, 2) == QColor(20, 180, 70),
                        "backend switch lost the saved background");
                const auto rebuilt_icon = rebuilt.pixelColor(rebuilt.width() - 30, 30);
                require(rebuilt_icon.red() > 200 && rebuilt_icon.blue() > 200,
                        "backend switch did not rebuild shared UI resources");
            }
        }
        // Fail during render_ui after model commands have been recorded, rather
        // than only rejecting a path before recording starts. Recovery must
        // discard that unsubmitted Vulkan target and rebuild its resource owners.
        const auto bad_assets = assets / "bad-ui";
        fs::create_directories(bad_assets / "icons");
        fs::create_directory_symlink(assets / "characters", bad_assets / "characters");
        fs::create_directory_symlink(assets / "backgrounds", bad_assets / "backgrounds");
        fs::copy_file(assets / "config.json", bad_assets / "config.json");
        {
            std::ofstream invalid_icon(bad_assets / "icons/toggle.png");
            invalid_icon << "invalid PNG fixture";
        }
        item->setAssetsDir(QString::fromStdString(bad_assets.string()));
        wait_for([&] { return item->status() == 2; });
        require(item->errorString().startsWith("render UI:"),
                "failure fixture did not reach the recorded-frame UI branch");
        item->setAssetsDir(directory.path());
        frames(3);
        const auto recovered = capture(item, output + "/ui-failure-recovered.png");
        require(recovered.pixelColor(2, 2) == QColor(20, 180, 70),
                "recorded-frame recovery lost the background");
        window.hide();
        item->setParentItem(nullptr);
        object.reset();
        std::puts(
            "PASS: background orientation, UI icon/open/settings/selection/wheel/resize/drag, "
            "left hold model drag and wheel zoom "
            "through Qt host; recorded-frame UI failure recovery");
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
