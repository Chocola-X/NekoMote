#include "wallpaper_item.hpp"
#include "kwin_pointer.hpp"

#include <nekomote/host_session/display_context.hpp>

#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QEvent>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QWheelEvent>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <utility>

#include "qt_input.hpp"

namespace nekomote::plasma
{
// GUI-thread sampler owned by the Plasma process. Its cache is shared across
// wallpaper items; it has no timer and runs only when an active item asks.
class X11CursorSampler final : public QObject
{
    Q_OBJECT

  public:
    using QObject::QObject;

    QPointF position()
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_query_)
        {
            const QPointF position = QCursor::pos();
            next_query_ = now + host_session::native_gaze_poll_interval(position != position_);
            position_ = position;
        }
        return position_;
    }

  private:
    std::chrono::steady_clock::time_point next_query_{};
    QPointF position_;
};

NekoWallpaperItem::NekoWallpaperItem(QQuickItem* parent) : WallpaperItemBase(parent)
{
    state_ = std::make_shared<nekomote::qt_quick::WallpaperState>();
    const auto platform = QGuiApplication::platformName();
    wayland_platform_ = platform.startsWith(QLatin1String("wayland"));
    native_x11_platform_ =
        platform == QLatin1String("xcb") && !host_session::wayland_session_hint();
    setMirrorVertically(true);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    connect(this, &WallpaperItemBase::failed, this, &NekoWallpaperItem::set_error);
    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    timer_->setInterval(17);
    connect(timer_, &QTimer::timeout, this,
            [this]
            {
                start_timer();
                request_playback_frame();
            });
    connect(this, &QQuickItem::visibleChanged, this, &NekoWallpaperItem::refresh_playback);
    session_timer_ = new QTimer(this);
    session_timer_->setInterval(1000);
    connect(session_timer_, &QTimer::timeout, this,
            [this] { set_session_unavailable(session_monitor_->poll()); });
    state_->profile = qEnvironmentVariableIntValue("NEKOMOTE_QT_PROFILE") == 1;
    if (state_->profile)
    {
        state_->trace_frames = 1;
        auto swaps = std::make_shared<unsigned>(0);
        connect(this, &QQuickItem::windowChanged, this,
                [this, swaps](QQuickWindow* host)
                {
                    disconnect(profile_swap_connection_);
                    if (host)
                        profile_swap_connection_ = connect(
                            host, &QQuickWindow::frameSwapped, this,
                            [this, swaps, source = QPointer<QQuickWindow>(host)]
                            {
                                if (source && source == window())
                                    ++*swaps;
                            },
                            Qt::QueuedConnection);
                });
        auto* diagnostics = new QTimer(this);
        diagnostics->setInterval(5000);
        connect(diagnostics, &QTimer::timeout, this,
                [this, swaps, previous_draws = 0U, previous_swaps = 0U, previous_inputs = 0U,
                 previous = std::chrono::steady_clock::now()]() mutable
                {
                    const auto now = std::chrono::steady_clock::now();
                    const auto seconds = std::chrono::duration<double>(now - previous).count();
                    const auto draws = state_->rendered_frames.load();
                    std::fprintf(
                        stderr,
                        "NekoMote profile: draw_fps=%.2f window_fps=%.2f inputs_s=%.2f limit=%d "
                        "paused=%d checked=%d visible=%d exposed=%d initialized=%d\n",
                        (draws - previous_draws) / seconds, (*swaps - previous_swaps) / seconds,
                        (state_->profile_inputs - previous_inputs) / seconds,
                        state_->fps_limit.load(), effective_paused_, backend_checked_, isVisible(),
                        window() ? window()->isExposed() : false,
                        window() ? window()->isSceneGraphInitialized() : false);
                    previous = now;
                    previous_draws = draws;
                    previous_swaps = *swaps;
                    previous_inputs = state_->profile_inputs;
                });
        diagnostics->start();
    }
}

NekoWallpaperItem::~NekoWallpaperItem()
{
    if (kwin_pointer_)
        kwin_pointer_->unsubscribe();
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
bool NekoWallpaperItem::pauseOnSessionEvents() const
{
    return pause_on_session_events_;
}
bool NekoWallpaperItem::globalGazeEnabled() const
{
    return global_gaze_enabled_;
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
QString NekoWallpaperItem::renderBackend() const
{
    return render_backend_;
}

QString NekoWallpaperItem::instanceId() const
{
    return instance_id_;
}

QString NekoWallpaperItem::createInstanceId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void NekoWallpaperItem::setInstanceId(const QString& value)
{
    // The package persists this ID in its containment-specific KConfig group.
    // Only UUIDs become filenames; QML/configuration cannot inject a path.
    QString normalized;
    std::filesystem::path config_path;
    if (!value.isEmpty())
    {
        const QUuid uuid(value);
        normalized = uuid.toString(QUuid::WithoutBraces);
        if (uuid.isNull() || value.compare(normalized, Qt::CaseInsensitive) != 0)
        {
            set_error(QStringLiteral("Invalid NekoMote wallpaper instance UUID: %1").arg(value));
            return;
        }
        const auto directory =
            QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        if (!QDir::isAbsolutePath(directory))
        {
            set_error(QStringLiteral("NekoMote configuration directory must be absolute"));
            return;
        }
        config_path = QDir(directory)
                          .filePath(QStringLiteral("nekomote/plasma/%1.json").arg(normalized))
                          .toStdString();
    }
    if (instance_id_ == normalized && status_ != 2)
        return;
    instance_id_ = normalized;
    state_->config_path = std::move(config_path);
    state_->config_dirty = true;
    error_string_.clear();
    emit errorStringChanged();
    if (status_ != 3)
        set_status(assets_dir_.isEmpty() ? 0 : 1);
    emit instanceIdChanged();
    update();
}

void NekoWallpaperItem::setRenderBackend(const QString& value)
{
    if (value != QStringLiteral("opengl") && value != QStringLiteral("vulkan"))
    {
        set_error(QStringLiteral("Unknown NekoMote rendering backend: %1").arg(value));
        return;
    }
    if (render_backend_ == value && status_ != 2)
        return;
    render_backend_ = value;
    state_->backend = value == QStringLiteral("vulkan")
                          ? nekomote::qt_quick::GraphicsBackend::Vulkan
                          : nekomote::qt_quick::GraphicsBackend::OpenGL;
    state_->config_dirty = true;
    if (status_ != 3)
    {
        error_string_.clear();
        emit errorStringChanged();
        set_status(assets_dir_.isEmpty() ? 0 : 1);
    }
    // Choosing the engine backend cannot make an unsupported Qt scene graph
    // available. Keep that capability failure and suspended scheduling visible.
    check_backend();
    emit renderBackendChanged();
    update();
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

void NekoWallpaperItem::setPauseOnSessionEvents(bool value)
{
    if (pause_on_session_events_ == value)
        return;
    pause_on_session_events_ = value;
    if (isComponentComplete())
        configure_session_monitor();
    emit pauseOnSessionEventsChanged();
}

void NekoWallpaperItem::setGlobalGazeEnabled(bool value)
{
    if (global_gaze_enabled_ == value)
        return;
    global_gaze_enabled_ = value;
    sync_kwin_pointer();
    if (!value)
    {
        compositor_pointer_.reset();
        global_gaze_.reset();
    }
    emit globalGazeEnabledChanged();
    update();
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
    return {effective_paused_, playback_revision_,
            std::span<const nekomote::qt_quick::Input>(inputs_.data(), input_count_),
            diagnostic_notification_, global_gaze_};
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
    if (state_->profile)
        ++state_->profile_inputs;
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
    // Input must not bypass the playback limit. The active timer consumes the
    // queued events on its next frame, as the layer-shell host does.
    if (!timer_->isActive() || state_->fps_limit.load(std::memory_order_acquire) == 0)
        update();
}

void NekoWallpaperItem::hoverEnterEvent(QHoverEvent* event)
{
    enqueue(nekomote::qt_quick::InputKind::Enter, nekomote::qt_quick::input_position(*event), 0, 0,
            event->timestamp());
}
void NekoWallpaperItem::hoverMoveEvent(QHoverEvent* event)
{
    enqueue(nekomote::qt_quick::InputKind::Move, nekomote::qt_quick::input_position(*event), 0, 0,
            event->timestamp());
}
void NekoWallpaperItem::hoverLeaveEvent(QHoverEvent*)
{
    enqueue(nekomote::qt_quick::InputKind::Leave);
}
void NekoWallpaperItem::mousePressEvent(QMouseEvent* event)
{
    enqueue(nekomote::qt_quick::InputKind::Press, nekomote::qt_quick::input_position(*event), 0,
            event->button() == Qt::RightButton ? 1 : 0, event->timestamp());
    event->accept();
}
void NekoWallpaperItem::mouseReleaseEvent(QMouseEvent* event)
{
    enqueue(nekomote::qt_quick::InputKind::Release, nekomote::qt_quick::input_position(*event), 0,
            event->button() == Qt::RightButton ? 1 : 0, event->timestamp());
    event->accept();
}
void NekoWallpaperItem::mouseMoveEvent(QMouseEvent* event)
{
    enqueue(nekomote::qt_quick::InputKind::Move, nekomote::qt_quick::input_position(*event), 0, 0,
            event->timestamp());
    event->accept();
}
void NekoWallpaperItem::wheelEvent(QWheelEvent* event)
{
    enqueue(nekomote::qt_quick::InputKind::Scroll, nekomote::qt_quick::input_position(*event),
            static_cast<float>(event->angleDelta().y()) / 120, 0, event->timestamp());
    event->accept();
}

void NekoWallpaperItem::componentComplete()
{
    WallpaperItemBase::componentComplete();
    configure_session_monitor();
    if (!assets_dir_.isEmpty() && status_ == 0)
        set_status(1);
}

void NekoWallpaperItem::configure_session_monitor()
{
    if (pause_on_session_events_)
    {
        if (!session_monitor_)
            session_monitor_ = std::make_unique<nekomote::host_session::Monitor>();
        session_timer_->start();
    }
    else
    {
        session_timer_->stop();
        session_monitor_.reset();
        set_session_unavailable(false);
    }
}

void NekoWallpaperItem::set_session_unavailable(bool unavailable)
{
    if (session_unavailable_ == unavailable)
        return;
    session_unavailable_ = unavailable;
    std::fputs(unavailable ? "[host_session] pause engaged (lock or sleep)\n"
                           : "[host_session] pause released (lock or sleep)\n",
               stderr);
    refresh_playback();
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
    window_connections_[3] = {};
    host_window_ = host;
    compositor_pointer_.reset();
    global_gaze_.reset();
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
    const bool suspended = session_unavailable_ || paused_ || status_ != 1 || !backend_checked_ ||
                           !isVisible() || !host_window_ || !host_window_->isVisible() ||
                           !host_window_->isExposed();
    if (suspended != effective_paused_)
    {
        effective_paused_ = suspended;
        sync_kwin_pointer();
        ++playback_revision_;
        if (suspended)
        {
            input_count_ = 0;
            compositor_pointer_.reset();
            global_gaze_.reset();
        }
        request_playback_frame();
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
    }
    diagnostic_inputs_ = true;
    sync_kwin_pointer();
    diagnostic_notification_ = true;
    compositor_pointer_.reset();
    global_gaze_.reset();
    setAcceptHoverEvents(false);
    setAcceptedMouseButtons(Qt::NoButton);
    input_count_ = 0;
    update();
    return true;
}

void NekoWallpaperItem::set_compositor_pointer_sample(std::optional<QPointF> position)
{
    if (global_gaze_enabled_ && wayland_platform_ && !effective_paused_ && !diagnostic_inputs_)
        compositor_pointer_ = position;
}

void NekoWallpaperItem::sync_kwin_pointer()
{
    if (wayland_platform_ && global_gaze_enabled_ && !effective_paused_ && !diagnostic_inputs_)
    {
        if (!kwin_pointer_)
        {
            auto* application = QCoreApplication::instance();
            auto* bridge = application->findChild<KWinPointer*>(QString(), Qt::FindDirectChildrenOnly);
            kwin_pointer_ = bridge ? bridge : new KWinPointer(application);
            kwin_pointer_->subscribe();
        }
    }
    else if (kwin_pointer_)
    {
        kwin_pointer_->unsubscribe();
        kwin_pointer_.clear();
    }
}

void NekoWallpaperItem::sample_global_gaze()
{
    if (!global_gaze_enabled_ || effective_paused_ || diagnostic_inputs_)
        return;
    if (native_x11_platform_)
    {
        if (!x11_cursor_)
        {
            auto* application = QCoreApplication::instance();
            auto* sampler =
                application->findChild<X11CursorSampler*>(QString(), Qt::FindDirectChildrenOnly);
            x11_cursor_ = sampler ? sampler : new X11CursorSampler(application);
        }
        global_gaze_ = mapFromGlobal(x11_cursor_->position());
    }
    else
    {
        auto pointer = compositor_pointer_;
        if (!pointer && kwin_pointer_)
            pointer = kwin_pointer_->position();
        global_gaze_ = pointer ? std::optional<QPointF>{mapFromGlobal(*pointer)} : std::nullopt;
    }
}

void NekoWallpaperItem::request_playback_frame()
{
    sample_global_gaze();
    update();
}

void NekoWallpaperItem::start_timer()
{
    if (!timer_ || effective_paused_)
        return;
    const int fps_limit = state_->fps_limit.load(std::memory_order_acquire);
    if (fps_limit == 0)
    {
        stop_timer();
        if (!window_connections_[3] && host_window_)
        {
            window_connections_[3] = connect(
                host_window_.data(), &QQuickWindow::frameSwapped, this,
                [this, source = host_window_]
                {
                    if (source != host_window_ || effective_paused_)
                        return;
                    // Unlimited playback requests one next frame after submission.
                    // A zero-interval GUI timer otherwise spins between submissions.
                    // Also restore the timer when the panel switches back to a limit.
                    start_timer();
                    if (state_->fps_limit.load(std::memory_order_acquire) == 0)
                        request_playback_frame();
                },
                Qt::QueuedConnection);
        }
        return;
    }
    // Finite playback already has a timer. Avoid queueing a GUI notification on
    // every host submission (including submissions caused by other items).
    if (window_connections_[3])
    {
        disconnect(window_connections_[3]);
        window_connections_[3] = {};
    }
    const int interval = fps_limit > 0 ? std::max(1, (1000 + fps_limit / 2) / fps_limit) : 17;
    if (timer_->interval() != interval)
        timer_->setInterval(interval);
    if (!timer_->isActive())
        timer_->start();
}

void NekoWallpaperItem::stop_timer()
{
    if (timer_)
        timer_->stop();
}
} // namespace nekomote::plasma

#include "wallpaper_item.moc"
