#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <memory>

#include "session_item.hpp"

namespace nekomote::plasma
{
// Status values exposed to QML as integers.
//   0 = Unconfigured: assetsDir not yet set or empty
//   1 = Loaded:       wallpaper initialized and ready to render
//   2 = Failed:       load error; errorString has the diagnostic
//   3 = UnsupportedBackend: host RHI is not OpenGL
class NekoWallpaperItem : public nekomote::qt_quick::WallpaperItemBase
{
    Q_OBJECT
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(int status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(QString assetsDir READ assetsDir WRITE setAssetsDir NOTIFY assetsDirChanged)
    QML_ELEMENT
    QML_NAMED_ELEMENT(NekoWallpaperItem)
  public:
    explicit NekoWallpaperItem(QQuickItem* parent = nullptr);
    ~NekoWallpaperItem() override;
    Renderer* createRenderer() const override;
    unsigned queryRendererCreated() const;
    unsigned queryRendererDestroyed() const;
    unsigned queryRenderedFrames() const;

    bool paused() const;
    int status() const;
    QString errorString() const;
    QString assetsDir() const;

    void setPaused(bool value);
    void setAssetsDir(const QString& value);

    bool configure_diagnostics(int steps, bool fixed, std::uint32_t seed);

    std::shared_ptr<nekomote::qt_quick::WallpaperState> wallpaper_state() const override;

  signals:
    void pausedChanged();
    void statusChanged();
    void errorStringChanged();
    void assetsDirChanged();

  protected:
    void hoverEnterEvent(QHoverEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void componentComplete() override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    nekomote::qt_quick::ItemState get_item_state() const override;
    void clear_inputs() override;
    void enqueue(nekomote::qt_quick::InputKind kind, QPointF position = {});
    void set_status(int value);
    void set_error(const QString& value);
    void check_backend();
    void attach_window(QQuickWindow* window);
    void refresh_playback();
    void start_timer();
    void stop_timer();

    std::shared_ptr<nekomote::qt_quick::WallpaperState> state_;
    bool paused_{false};
    bool effective_paused_{true};
    bool diagnostic_inputs_{false}, diagnostic_notification_{false};
    int status_{0};
    QString error_string_;
    QString assets_dir_;
    std::uint64_t playback_revision_{0};
    std::array<nekomote::qt_quick::Input, 32> inputs_{};
    std::size_t input_count_{0};
    QTimer* timer_{nullptr};
    bool backend_checked_{false};
    QPointer<QQuickWindow> host_window_;
    std::array<QMetaObject::Connection, 3> window_connections_{};
};
} // namespace nekomote::plasma
