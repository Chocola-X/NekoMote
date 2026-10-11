#include "wallpaper_item.hpp"

#include <QEvent>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QUrl>
#include <QWheelEvent>
#include <algorithm>
#include <chrono>

namespace nekomote::plasma
{
NekoWallpaperItem::NekoWallpaperItem(QQuickItem* parent) : WallpaperItemBase(parent)
{
    state_ = std::make_shared<nekomote::qt_quick::WallpaperState>();
    setMirrorVertically(true);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    connect(this, &WallpaperItemBase::failed, this, &NekoWallpaperItem::set_error);
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
    return nekomote::qt_quick::create_wallpaper_renderer(state_);
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
QString NekoWallpaperItem::assetsDir() const
{
    return assets_dir_;
}
bool NekoWallpaperItem::muted() const
{
    return muted_;
}

void NekoWallpaperItem::setAssetsDir(const QString& value)
{
    if (assets_dir_ == value)
        return;
    assets_dir_ = value;
    if (state_)
    {
        QString local = value;
        if (local.startsWith(QStringLiteral("file://")))
            local = QUrl(value).toLocalFile();
        state_->assets_dir = local.toStdString();
        state_->config_dirty = true;
        state_->loaded = false;
    }
    if (!value.isEmpty())
        set_status(1);
    else
        set_status(0);
    emit assetsDirChanged();
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

void NekoWallpaperItem::setMuted(bool value)
{
    if (muted_ == value)
        return;
    muted_ = value;
    if (state_)
    {
        state_->muted = value;
        state_->config_dirty = true;
    }
    emit mutedChanged();
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

nekomote::qt_quick::ItemState NekoWallpaperItem::get_item_state() const
{
    return {effective_paused_, playback_revision_, inputs_, input_count_, diagnostic_notification_};
}

void NekoWallpaperItem::clear_inputs()
{
    input_count_ = 0;
}

std::shared_ptr<nekomote::qt_quick::WallpaperState> NekoWallpaperItem::wallpaper_state() const
{
    return state_;
}

void NekoWallpaperItem::enqueue(nekomote::qt_quick::InputKind kind, QPointF position, float scroll,
                                int button, std::uint64_t timestamp_ms)
{
    if (diagnostic_inputs_ || effective_paused_ || status_ != 1)
        return;
    if (input_count_ == inputs_.size())
    {
        emit failed(QStringLiteral("input queue capacity exceeded"));
        return;
    }
    inputs_[input_count_++] = {
        kind,
        static_cast<float>(position.x()),
        static_cast<float>(position.y()),
        scroll,
        button,
        timestamp_ms
            ? timestamp_ms
            : static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now().time_since_epoch())
                                             .count())};
    update();
}

void NekoWallpaperItem::hoverEnterEvent(QHoverEvent* event)
{
    enqueue(nekomote::qt_quick::InputKind::Enter, event->position(), 0, 0, event->timestamp());
}
void NekoWallpaperItem::hoverMoveEvent(QHoverEvent* event)
{
    enqueue(nekomote::qt_quick::InputKind::Move, event->position(), 0, 0, event->timestamp());
}
void NekoWallpaperItem::hoverLeaveEvent(QHoverEvent*)
{
    enqueue(nekomote::qt_quick::InputKind::Leave);
}
void NekoWallpaperItem::mousePressEvent(QMouseEvent* event)
{
    enqueue(nekomote::qt_quick::InputKind::Press, event->position(), 0,
            event->button() == Qt::RightButton ? 1 : 0, event->timestamp());
    event->accept();
}
void NekoWallpaperItem::mouseReleaseEvent(QMouseEvent* event)
{
    enqueue(nekomote::qt_quick::InputKind::Release, event->position(), 0,
            event->button() == Qt::RightButton ? 1 : 0, event->timestamp());
    event->accept();
}
void NekoWallpaperItem::mouseMoveEvent(QMouseEvent* event)
{
    enqueue(nekomote::qt_quick::InputKind::Move, event->position(), 0, 0, event->timestamp());
    event->accept();
}
void NekoWallpaperItem::wheelEvent(QWheelEvent* event)
{
    enqueue(nekomote::qt_quick::InputKind::Scroll, event->position(),
            static_cast<float>(event->angleDelta().y()) / 120, 0, event->timestamp());
    event->accept();
}

void NekoWallpaperItem::componentComplete()
{
    WallpaperItemBase::componentComplete();
    if (!assets_dir_.isEmpty() && status_ == 0)
        set_status(1);
}

void NekoWallpaperItem::itemChange(ItemChange change, const ItemChangeData& data)
{
    WallpaperItemBase::itemChange(change, data);
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
        set_status(!assets_dir_.isEmpty() ? 1 : 0);
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
    return WallpaperItemBase::eventFilter(watched, event);
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

bool NekoWallpaperItem::configure_diagnostics(int steps, bool fixed, std::uint32_t seed)
{
    if (!state_ || steps < 0 || steps > 100000 || (!fixed && steps != 0))
        return false;
    if (fixed)
    {
        if (state_->renderer_created.load() != 0)
            return false;
        state_->seed = seed;
        state_->fixed_steps = steps;
        state_->deterministic = true;
        state_->config_dirty = true;
        state_->loaded = false;
    }
    diagnostic_inputs_ = true;
    diagnostic_notification_ = true;
    setAcceptHoverEvents(false);
    setAcceptedMouseButtons(Qt::NoButton);
    input_count_ = 0;
    update();
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
