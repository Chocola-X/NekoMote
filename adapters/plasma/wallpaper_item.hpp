#pragma once

#include <nekomote/host_session/monitor.hpp>

#include <QPointer>
#include <QQuickItem>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <memory>
#include <optional>

#include "session_item.hpp"

namespace nekomote::plasma
{
class X11CursorSampler;
class KWinPointer;
// Status values exposed to QML as integers.
//   0 = Unconfigured: assetsDir not yet set or empty
//   1 = Loaded:       wallpaper initialized and ready to render
//   2 = Failed:       load error; errorString has the diagnostic
//   3 = UnsupportedBackend: host RHI is not OpenGL
class NekoWallpaperItem : public nekomote::qt_quick::WallpaperItemBase
{
    Q_OBJECT
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool pauseOnSessionEvents READ pauseOnSessionEvents WRITE setPauseOnSessionEvents
                   NOTIFY pauseOnSessionEventsChanged)
    Q_PROPERTY(bool globalGazeEnabled READ globalGazeEnabled WRITE setGlobalGazeEnabled NOTIFY
                   globalGazeEnabledChanged)
    Q_PROPERTY(int status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(QString assetsDir READ assetsDir WRITE setAssetsDir NOTIFY assetsDirChanged)
    Q_PROPERTY(QString instanceId READ instanceId WRITE setInstanceId NOTIFY instanceIdChanged)
    Q_PROPERTY(
        QString renderBackend READ renderBackend WRITE setRenderBackend NOTIFY renderBackendChanged)
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QML_ELEMENT
    QML_NAMED_ELEMENT(NekoWallpaperItem)
#endif
  public:
    explicit NekoWallpaperItem(QQuickItem* parent = nullptr);
    ~NekoWallpaperItem() override;
    Renderer* createRenderer() const override;
    unsigned queryRendererCreated() const;
    unsigned queryRendererDestroyed() const;
    unsigned queryRenderedFrames() const;

    bool paused() const;
    bool pauseOnSessionEvents() const;
    bool globalGazeEnabled() const;
    int status() const;
    QString errorString() const;
    QString assetsDir() const;
    QString renderBackend() const;
    QString instanceId() const;
    Q_INVOKABLE QString createInstanceId() const;

    void setPaused(bool value);
    void setPauseOnSessionEvents(bool value);
    void setGlobalGazeEnabled(bool value);
    void setAssetsDir(const QString& value);
    void setRenderBackend(const QString& value);
    void setInstanceId(const QString& value);

    bool configure_diagnostics(int steps, bool fixed, std::uint32_t seed);

    // Optional compositor feed. Call on the GUI thread with a position in Qt's
    // logical desktop coordinates, or nullopt when the feed becomes unavailable.
    void set_compositor_pointer_sample(std::optional<QPointF> position);

    std::shared_ptr<nekomote::qt_quick::WallpaperState> wallpaper_state() const override;

  signals:
    void pausedChanged();
    void pauseOnSessionEventsChanged();
    void globalGazeEnabledChanged();
    void statusChanged();
    void errorStringChanged();
    void assetsDirChanged();
    void instanceIdChanged();
    void renderBackendChanged();

  protected:
    void hoverEnterEvent(QHoverEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void componentComplete() override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    nekomote::qt_quick::ItemState get_item_state() const override;
    void clear_inputs() override;
    void enqueue(nekomote::qt_quick::InputKind kind, QPointF position = {}, float scroll = 0,
                 int button = 0, std::uint64_t timestamp_ms = 0);
    void set_status(int value);
    void set_error(const QString& value);
    void check_backend();
    void attach_window(QQuickWindow* window);
    void refresh_playback();
    void configure_session_monitor();
    void set_session_unavailable(bool unavailable);
    void request_playback_frame();
    void sample_global_gaze();
    void sync_kwin_pointer();
    void start_timer();
    void stop_timer();

    std::shared_ptr<nekomote::qt_quick::WallpaperState> state_;
    QPointer<X11CursorSampler> x11_cursor_;
    QPointer<KWinPointer> kwin_pointer_;
    bool native_x11_platform_{false};
    bool wayland_platform_{false};
    bool global_gaze_enabled_{true};
    std::optional<QPointF> compositor_pointer_;
    std::optional<QPointF> global_gaze_;
    std::unique_ptr<nekomote::host_session::Monitor> session_monitor_;
    QTimer* session_timer_{nullptr};
    bool pause_on_session_events_{true};
    bool session_unavailable_{false};
    bool paused_{false};
    bool effective_paused_{true};
    bool diagnostic_inputs_{false}, diagnostic_notification_{false};
    int status_{0};
    QString error_string_;
    QString assets_dir_;
    QString instance_id_;
    QString render_backend_{QStringLiteral("opengl")};
    std::uint64_t playback_revision_{0};
    std::array<nekomote::qt_quick::Input, nekomote::qt_quick::kInputCapacity> inputs_{};
    std::size_t input_count_{0};
    QTimer* timer_{nullptr};
    bool backend_checked_{false};
    QPointer<QQuickWindow> host_window_;
    QMetaObject::Connection profile_swap_connection_;
    std::array<QMetaObject::Connection, 4> window_connections_{};
};
} // namespace nekomote::plasma
