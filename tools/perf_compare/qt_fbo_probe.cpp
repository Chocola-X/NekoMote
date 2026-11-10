// Model-free Qt FBO submission reproducer. Compare QSG_RENDER_LOOP=basic and
// threaded on the same output. This diagnostic intentionally requests 59 FPS;
// frameSwapped counts Qt submissions, not final compositor presentation.
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickFramebufferObject>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <cstdio>

namespace
{
struct FrameCounts
{
    std::atomic<unsigned> draws{0};
    unsigned swaps{0}; // GUI thread only; worker notifications are queued.
};

class ClearRenderer final : public QQuickFramebufferObject::Renderer
{
  public:
    explicit ClearRenderer(FrameCounts& counts) : counts_(counts)
    {
    }

    void render() override
    {
        auto* functions = QOpenGLContext::currentContext()->functions();
        functions->glClearColor(0.1F, 0.1F, 0.1F, 1.0F);
        functions->glClear(GL_COLOR_BUFFER_BIT);
        ++counts_.draws;
    }

  private:
    FrameCounts& counts_;
};

class ClearItem final : public QQuickFramebufferObject
{
  public:
    explicit ClearItem(FrameCounts& counts) : counts_(counts)
    {
    }

    Renderer* createRenderer() const override
    {
        return new ClearRenderer(counts_);
    }

  private:
    FrameCounts& counts_;
};
} // namespace

int main(int argc, char** argv)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#else
    QQuickWindow::setSceneGraphBackend(QSGRendererInterface::OpenGL);
#endif
    QGuiApplication application(argc, argv);
    FrameCounts counts;
    QQuickWindow window;
    window.setFlags(Qt::Window | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
    // Ensure the renderer and its borrowed counts are released before local
    // destruction even if the selected scene graph uses a worker thread.
    window.setPersistentSceneGraph(false);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    window.setPersistentGraphics(false);
#else
    window.setPersistentOpenGLContext(false);
#endif
    window.resize(320, 240);
    ClearItem item(counts);
    item.setSize({320, 240});
    item.setParentItem(window.contentItem());
    QObject::connect(&window, &QQuickWindow::frameSwapped, &application, [&] { ++counts.swaps; });
    QTimer timer;
    timer.setInterval(17);
    QObject::connect(&timer, &QTimer::timeout, &item, [&] { item.update(); });
    timer.start();
    window.show();
    auto started = std::chrono::steady_clock::now();
    QTimer::singleShot(1000, &application,
                       [&]
                       {
                           counts.draws = 0;
                           counts.swaps = 0;
                           started = std::chrono::steady_clock::now();
                       });
    QTimer::singleShot(
        6000, &application,
        [&]
        {
            const auto seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            std::printf("empty FBO: seconds=%.3f draw_fps=%.2f window_fps=%.2f\n", seconds,
                        counts.draws.load() / seconds, counts.swaps / seconds);
            application.quit();
        });
    const auto result = application.exec();
    timer.stop();
    window.hide();
    window.releaseResources();
    return result;
}
