#pragma once

#include "session_item.hpp"

#include <QQuickItem>
#include <QString>
#include <QUrl>
#include <memory>

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
    QML_ELEMENT
    QML_NAMED_ELEMENT(NekoWallpaperItem)
  public:
    explicit NekoWallpaperItem(QQuickItem* parent = nullptr);
    ~NekoWallpaperItem() override;
    Renderer* createRenderer() const override;

    QUrl modelFile() const;
    QUrl sessionFile() const;
    float originX() const;
    float originY() const;
    float scale() const;
    bool paused() const;
    int status() const;
    QString errorString() const;

    void setModelFile(const QUrl& value);
    void setSessionFile(const QUrl& value);
    void setOriginX(float value);
    void setOriginY(float value);
    void setScale(float value);
    void setPaused(bool value);

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

  protected:
    void hoverEnterEvent(QHoverEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void componentComplete() override;

  private:
    nekomote::qt_quick::ItemState get_item_state() const override;
    void clear_inputs() override;
    void enqueue(nekomote::qt_quick::InputKind kind, QPointF position = {});
    void try_load();
    void set_status(int value);
    void set_error(const QString& value);

    std::shared_ptr<nekomote::qt_quick::SessionState> state_;
    QUrl model_file_;
    QUrl session_file_;
    float origin_x_{0};
    float origin_y_{400};
    float scale_{1};
    bool paused_{false};
    int status_{0};
    QString error_string_;
    std::uint64_t playback_revision_{0};
    std::array<nekomote::qt_quick::Input, 32> inputs_{};
    std::size_t input_count_{0};
};
} // namespace nekomote::plasma
