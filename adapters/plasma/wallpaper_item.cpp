#include "wallpaper_item.hpp"

#include <QEvent>
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
    timer_ = new QTimer(this);
    timer_->setInterval(33);
    connect(timer_, &QTimer::timeout, this, [this] { update(); });
    connect(this, &QQuickItem::visibleChanged, this, &NekoWallpaperItem::refresh_playback);
}

NekoWallpaperItem::~NekoWallpaperItem()
{
    stop_timer();
    if (host_window_)
        host_window_->removeEventFilter(this);
    for (const auto& connection : window_connections_)
        disconnect(connection);
}

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

unsigned NekoWallpaperItem::queryRendererCreated() const
{
    return state_ ? state_->renderer_created.load() : 0;
}

unsigned NekoWallpaperItem::queryRendererDestroyed() const
{
    return state_ ? state_->renderer_destroyed.load() : 0;
}

unsigned NekoWallpaperItem::queryRenderedFrames() const
{
    return state_ ? state_->rendered_frames.load() : 0;
}

void NekoWallpaperItem::setModelFile(const QUrl& value)
{
    if (model_file_ == value)
        return;
    // When a session is already loaded, the QML Loader rebuilds the item to
    // switch models. The in-place binding update reaches this setter on the
    // old item before it is destroyed; accept the value without reloading.
    model_file_ = value;
    emit modelFileChanged();
}

void NekoWallpaperItem::setSessionFile(const QUrl& value)
{
    if (session_file_ == value)
        return;
    session_file_ = value;
    emit sessionFileChanged();
}

void NekoWallpaperItem::setOriginX(float value)
{
    if (origin_x_ == value)
        return;
    if (!std::isfinite(value))
    {
        set_error(QStringLiteral("originX must be finite"));
        return;
    }
    origin_x_ = value;
    emit originXChanged();
}

void NekoWallpaperItem::setOriginY(float value)
{
    if (origin_y_ == value)
        return;
    if (!std::isfinite(value))
    {
        set_error(QStringLiteral("originY must be finite"));
        return;
    }
    origin_y_ = value;
    emit originYChanged();
}

void NekoWallpaperItem::setScale(float value)
{
    if (scale_ == value)
        return;
    if (!std::isfinite(value) || value <= 0)
    {
        set_error(QStringLiteral("scale must be positive and finite"));
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
    refresh_playback();
    emit pausedChanged();
    update();
}

void NekoWallpaperItem::set_status(int value)
{
    if (status_ == value)
        return;
    status_ = value;
    refresh_playback();
    emit statusChanged();
}

void NekoWallpaperItem::set_error(const QString& value)
{
    error_string_ = value;
    emit errorStringChanged();
    set_status(2);
    input_count_ = 0;
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
    return {effective_paused_, playback_revision_, inputs_, input_count_};
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
    if (diagnostic_inputs_ || effective_paused_ || status_ != 1)
        return;
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

void NekoWallpaperItem::itemChange(ItemChange change, const ItemChangeData& data)
{
    SessionItemBase::itemChange(change, data);
    if (change == ItemSceneChange)
        attach_window(data.window);
}

void NekoWallpaperItem::check_backend()
{
    auto* host = host_window_.data();
    if (!host || !host->isSceneGraphInitialized())
        return;
    backend_checked_ = true;
    const auto api = host->rendererInterface()->graphicsApi();
    if (api != QSGRendererInterface::OpenGL)
    {
        error_string_ =
            QStringLiteral("NekoMote requires an OpenGL Qt Quick backend; actual API=%1")
                .arg(static_cast<int>(api));
        emit errorStringChanged();
        set_status(3);
    }
    else if (status_ == 3)
    {
        error_string_.clear();
        emit errorStringChanged();
        set_status(state_ ? 1 : 0);
    }
    refresh_playback();
}

void NekoWallpaperItem::attach_window(QQuickWindow* host)
{
    if (host_window_ == host)
        return;
    if (host_window_)
        host_window_->removeEventFilter(this);
    for (const auto& connection : window_connections_)
        disconnect(connection);
    host_window_ = host;
    backend_checked_ = false;
    ++playback_revision_;
    if (host)
    {
        host->installEventFilter(this);
        window_connections_[0] = connect(host, &QQuickWindow::sceneGraphInitialized, this,
                                         &NekoWallpaperItem::check_backend, Qt::QueuedConnection);
        window_connections_[1] = connect(
            host, &QQuickWindow::sceneGraphInvalidated, this,
            [this, source = QPointer<QQuickWindow>(host)]
            {
                if (source != host_window_)
                    return;
                backend_checked_ = false;
                refresh_playback();
            },
            Qt::QueuedConnection);
        window_connections_[2] =
            connect(host, &QWindow::visibleChanged, this, &NekoWallpaperItem::refresh_playback);
        check_backend();
    }
    refresh_playback();
}

bool NekoWallpaperItem::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == host_window_ &&
        (event->type() == QEvent::Expose || event->type() == QEvent::Show ||
         event->type() == QEvent::Hide || event->type() == QEvent::WindowStateChange))
        QTimer::singleShot(0, this, [this] { refresh_playback(); });
    return SessionItemBase::eventFilter(watched, event);
}

void NekoWallpaperItem::refresh_playback()
{
    const bool suspended = paused_ || status_ != 1 || !backend_checked_ || !isVisible() ||
                           !host_window_ || !host_window_->isVisible() ||
                           !host_window_->isExposed();
    if (suspended != effective_paused_)
    {
        effective_paused_ = suspended;
        ++playback_revision_;
        if (suspended)
            input_count_ = 0;
        update();
    }
    if (suspended)
        stop_timer();
    else
        start_timer();
}

bool NekoWallpaperItem::configure_diagnostics(int steps, bool fixed)
{
    if (!state_ || steps < 0 || steps > 100000)
    {
        set_error(
            QStringLiteral("diagnostics require a loaded item and valid steps"));
        return false;
    }
    // Fixed-step or deterministic capture must be configured before the first
    // renderer is created. Pure input isolation (steps==0, !fixed) is safe at
    // any time because it only disables hover/mouse and enables notifications.
    if ((steps > 0 || fixed) && state_->renderer_created.load() != 0)
    {
        set_error(
            QStringLiteral("fixed-step diagnostics require configuration before rendering"));
        return false;
    }
    state_->fixed_steps = steps;
    state_->deterministic = fixed;
    state_->notify_frames = true;
    diagnostic_inputs_ = true;
    setAcceptHoverEvents(false);
    setAcceptedMouseButtons(Qt::NoButton);
    input_count_ = 0;
    return true;
}

void NekoWallpaperItem::start_timer()
{
    if (timer_ && !timer_->isActive() && !effective_paused_)
        timer_->start();
}

void NekoWallpaperItem::stop_timer()
{
    if (timer_)
        timer_->stop();
}
} // namespace nekomote::plasma
