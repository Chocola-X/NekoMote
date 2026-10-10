#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <memory>

#include "session_item.hpp"

namespace nekomote::plasma
{
// Status values exposed to QML as integers.
//   0 = Unconfigured: modelFile or sessionFile not yet set
//   1 = Loaded:       session compiled and ready to render
//   2 = Failed:       load error; errorString has the diagnostic
//   3 = UnsupportedBackend: host RHI is not OpenGL
class NekoWallpaperItem : public nekomote::qt_quick::SessionItemBase
{
    Q_OBJECT
    Q_PROPERTY(QUrl modelFile READ modelFile WRITE setModelFile NOTIFY modelFileChanged)
    Q_PROPERTY(QUrl sessionFile READ sessionFile WRITE setSessionFile NOTIFY sessionFileChanged)
    Q_PROPERTY(float originX READ originX WRITE setOriginX NOTIFY originXChanged)
    Q_PROPERTY(float originY READ originY WRITE setOriginY NOTIFY originYChanged)
    Q_PROPERTY(float scale READ scale WRITE setScale NOTIFY scaleChanged)
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
    // C++ diagnostics; excluded from the product QML API.
    unsigned queryRendererCreated() const;
    unsigned queryRendererDestroyed() const;
    unsigned queryRenderedFrames() const;

    QUrl modelFile() const;
    QUrl sessionFile() const;
    float originX() const;
    float originY() const;
    float scale() const;
    bool paused() const;
    int status() const;
    QString errorString() const;
    QString assetsDir() const;

    void setModelFile(const QUrl& value);
    void setSessionFile(const QUrl& value);
    void setOriginX(float value);
    void setOriginY(float value);
    void setScale(float value);
    void setPaused(bool value);
    void setAssetsDir(const QString& value);

    Q_INVOKABLE QVariantList scanCharacters() const;
    Q_INVOKABLE QVariantList scanBackgrounds() const;
    Q_INVOKABLE bool setBackground(const QUrl& url);
    Q_INVOKABLE void clearBackground();
    // Input isolation/notifications may be enabled at any time via synchronize.
    // Fixed playback replaces the CPU initial state before renderer creation only;
    // late requests fail without mutating the live session or its status.
    bool configure_diagnostics(int steps, bool fixed, std::uint32_t seed);

    std::shared_ptr<nekomote::qt_quick::SessionState> session_state() const override;

  signals:
    void modelFileChanged();
    void sessionFileChanged();
    void originXChanged();
    void originYChanged();
    void scaleChanged();
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
    void try_load();
    void set_status(int value);
    void set_error(const QString& value);
    void check_backend();
    void attach_window(QQuickWindow* window);
    void refresh_playback();
    void start_timer();
    void stop_timer();

    std::shared_ptr<nekomote::qt_quick::SessionState> state_;
    QUrl model_file_;
    QUrl session_file_;
    float origin_x_{0};
    float origin_y_{400};
    float scale_{1};
    bool paused_{false};
    bool effective_paused_{true};
    bool diagnostic_inputs_{false}, diagnostic_notifications_{false};
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
