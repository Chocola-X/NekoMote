#include "wallpaper_item.hpp"

#include <QHoverEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QUrl>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace nekomote::plasma
{
namespace
{
std::filesystem::path to_local_path(const QUrl& url)
{
    if (!url.isLocalFile())
        throw std::runtime_error("model/session URL must be a local file: " +
                                 url.toString().toStdString());
    return std::filesystem::path(url.toLocalFile().toStdString());
}
} // namespace

NekoWallpaperItem::NekoWallpaperItem(QQuickItem* parent) : SessionItemBase(parent)
{
    setMirrorVertically(true);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    connect(this, &SessionItemBase::failed, this, &NekoWallpaperItem::set_error);
}

NekoWallpaperItem::~NekoWallpaperItem() = default;

QQuickFramebufferObject::Renderer* NekoWallpaperItem::createRenderer() const
{
    // Qt dereferences createRenderer() without a null check, including when
    // configuration/loading failed. The shared renderer clears an empty item.
    return nekomote::qt_quick::create_session_renderer(state_);
}

QUrl NekoWallpaperItem::modelFile() const
{
    return model_file_;
}
QUrl NekoWallpaperItem::sessionFile() const
{
    return session_file_;
}
float NekoWallpaperItem::originX() const
{
    return origin_x_;
}
float NekoWallpaperItem::originY() const
{
    return origin_y_;
}
float NekoWallpaperItem::scale() const
{
    return scale_;
}
bool NekoWallpaperItem::paused() const
{
    return paused_;
}
int NekoWallpaperItem::status() const
{
    return status_;
}
QString NekoWallpaperItem::errorString() const
{
    return error_string_;
}

void NekoWallpaperItem::setModelFile(const QUrl& value)
{
    if (model_file_ == value)
        return;
    if (state_)
    {
        set_error(
            QStringLiteral("changing modelFile after load is not supported in this prototype"));
        return;
    }
    model_file_ = value;
    emit modelFileChanged();
}

void NekoWallpaperItem::setSessionFile(const QUrl& value)
{
    if (session_file_ == value)
        return;
    if (state_)
    {
        set_error(
            QStringLiteral("changing sessionFile after load is not supported in this prototype"));
        return;
    }
    session_file_ = value;
    emit sessionFileChanged();
}

void NekoWallpaperItem::setOriginX(float value)
{
    if (origin_x_ == value)
        return;
    if (state_ || !std::isfinite(value))
    {
        set_error(QStringLiteral("originX must be finite and set before loading"));
        return;
    }
    origin_x_ = value;
    emit originXChanged();
}

void NekoWallpaperItem::setOriginY(float value)
{
    if (origin_y_ == value)
        return;
    if (state_ || !std::isfinite(value))
    {
        set_error(QStringLiteral("originY must be finite and set before loading"));
        return;
    }
    origin_y_ = value;
    emit originYChanged();
}

void NekoWallpaperItem::setScale(float value)
{
    if (scale_ == value)
        return;
    if (state_ || !std::isfinite(value) || value <= 0)
    {
        set_error(QStringLiteral("scale must be positive, finite and set before loading"));
        return;
    }
    scale_ = value;
    emit scaleChanged();
}

void NekoWallpaperItem::setPaused(bool value)
{
    if (paused_ == value)
        return;
    paused_ = value;
    ++playback_revision_;
    emit pausedChanged();
    update();
}

void NekoWallpaperItem::set_status(int value)
{
    if (status_ == value)
        return;
    status_ = value;
    emit statusChanged();
}

void NekoWallpaperItem::set_error(const QString& value)
{
    error_string_ = value;
    emit errorStringChanged();
    set_status(2);
}

void NekoWallpaperItem::try_load()
{
    if (model_file_.isEmpty() || session_file_.isEmpty())
        return;
    if (!error_string_.isEmpty())
        return;
    if (!std::isfinite(origin_x_) || !std::isfinite(origin_y_) || !std::isfinite(scale_) ||
        scale_ <= 0)
    {
        set_error(QStringLiteral("scale must be positive"));
        return;
    }
    try
    {
        const auto model_path = to_local_path(model_file_);
        const auto session_path = to_local_path(session_file_);
        state_ = nekomote::qt_quick::load_session(model_path, session_path, 1, origin_x_, origin_y_,
                                                  scale_, 0, false);
        set_status(1);
        update();
    }
    catch (const std::exception& error)
    {
        state_.reset();
        set_error(QString::fromUtf8(error.what()));
    }
}

nekomote::qt_quick::ItemState NekoWallpaperItem::get_item_state() const
{
    return {paused_, playback_revision_, inputs_, input_count_};
}

void NekoWallpaperItem::clear_inputs()
{
    input_count_ = 0;
}

std::shared_ptr<nekomote::qt_quick::SessionState> NekoWallpaperItem::session_state() const
{
    return state_;
}

void NekoWallpaperItem::enqueue(nekomote::qt_quick::InputKind kind, QPointF position)
{
    if (input_count_ == inputs_.size())
    {
        emit failed(QStringLiteral("input queue capacity exceeded"));
        return;
    }
    inputs_[input_count_++] = {kind, static_cast<float>(position.x()),
                               static_cast<float>(position.y())};
    update();
}

void NekoWallpaperItem::hoverEnterEvent(QHoverEvent* event)
{
    enqueue(nekomote::qt_quick::InputKind::Enter, event->position());
}
void NekoWallpaperItem::hoverMoveEvent(QHoverEvent* event)
{
    enqueue(nekomote::qt_quick::InputKind::Move, event->position());
}
void NekoWallpaperItem::hoverLeaveEvent(QHoverEvent*)
{
    enqueue(nekomote::qt_quick::InputKind::Leave);
}
void NekoWallpaperItem::mousePressEvent(QMouseEvent* event)
{
    enqueue(nekomote::qt_quick::InputKind::Touch, event->position());
    event->accept();
}

void NekoWallpaperItem::componentComplete()
{
    SessionItemBase::componentComplete();
    try_load();
}
} // namespace nekomote::plasma
