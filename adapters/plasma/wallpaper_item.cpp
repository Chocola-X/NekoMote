#include "wallpaper_item.hpp"

#include <QDir>
#include <QEvent>
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
#include <cmath>
#include <cstdio>
#include <utility>

#include "idle_fps.hpp"
#include "qt_input.hpp"

namespace nekomote::plasma
{
NekoWallpaperItem::NekoWallpaperItem(QQuickItem* parent) : WallpaperItemBase(parent)
{
    last_activity_ = std::chrono::steady_clock::now();
    state_ = std::make_shared<nekomote::qt_quick::WallpaperState>();
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
                update();
            });
    pointer_provider_ = make_global_pointer_provider();
    if (pointer_provider_)
    {
        pointer_timer_ = new QTimer(this);
        pointer_timer_->setTimerType(Qt::PreciseTimer);
        pointer_timer_->setInterval(16);
        connect(pointer_timer_, &QTimer::timeout, this, &NekoWallpaperItem::poll_global_pointer);
    }
    occlusion_provider_ = make_occlusion_provider();
    if (occlusion_provider_)
    {
        occlusion_timer_ = new QTimer(this);
        occlusion_timer_->setInterval(250);
        connect(occlusion_timer_, &QTimer::timeout, this, &NekoWallpaperItem::poll_occlusion);
    }
    connect(this, &QQuickItem::visibleChanged, this, &NekoWallpaperItem::refresh_playback);
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
                        "paused=%d checked=%d visible=%d exposed=%d initialized=%d pointer=%d "
                        "occluded=%d eff_limit=%d\n",
                        (draws - previous_draws) / seconds, (*swaps - previous_swaps) / seconds,
                        (state_->profile_inputs - previous_inputs) / seconds,
                        state_->fps_limit.load(), effective_paused_, backend_checked_, isVisible(),
                        window() ? window()->isExposed() : false,
                        window() ? window()->isSceneGraphInitialized() : false,
                        pointer_provider_ ? static_cast<int>(pointer_provider_->scope()) : 0,
                        occluded_, effective_fps_limit());
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
    stop_timer();
    stop_pointer_timer();
    stop_occlusion_timer();
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
            diagnostic_notification_};
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
    last_activity_ = std::chrono::steady_clock::now();
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
    if (global_pointer_active())
        return;
    enqueue(nekomote::qt_quick::InputKind::Enter, nekomote::qt_quick::input_position(*event), 0, 0,
            event->timestamp());
}
void NekoWallpaperItem::hoverMoveEvent(QHoverEvent* event)
{
    if (global_pointer_active())
        return;
    enqueue(nekomote::qt_quick::InputKind::Move, nekomote::qt_quick::input_position(*event), 0, 0,
            event->timestamp());
}
void NekoWallpaperItem::hoverLeaveEvent(QHoverEvent*)
{
    if (global_pointer_active())
        return;
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
    if (!global_pointer_active())
        enqueue(nekomote::qt_quick::InputKind::Move, nekomote::qt_quick::input_position(*event), 0,
                0, event->timestamp());
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
    window_connections_[3] = {};
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
    const bool base_suspended = paused_ || status_ != 1 || !backend_checked_ || !isVisible() ||
                                !host_window_ || !host_window_->isVisible() ||
                                !host_window_->isExposed();
    const bool suspended = base_suspended || occluded_;
    if (suspended != effective_paused_)
    {
        effective_paused_ = suspended;
        ++playback_revision_;
        if (suspended)
            input_count_ = 0;
        else
            last_activity_ = std::chrono::steady_clock::now();
        update();
    }
    if (base_suspended)
        stop_occlusion_timer();
    else
        start_occlusion_timer();
    if (suspended)
    {
        stop_timer();
        stop_pointer_timer();
    }
    else
    {
        start_timer();
        start_pointer_timer();
    }
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
    diagnostic_notification_ = true;
    setAcceptHoverEvents(false);
    setAcceptedMouseButtons(Qt::NoButton);
    input_count_ = 0;
    update();
    return true;
}

void NekoWallpaperItem::start_timer()
{
    if (!timer_ || effective_paused_)
        return;
    const int configured_limit = state_->fps_limit.load(std::memory_order_acquire);
    if (configured_limit == 0)
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
                        update();
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
    const int fps_limit = effective_fps_limit();
    const int interval = std::max(1, (1000 + fps_limit / 2) / fps_limit);
    if (timer_->interval() != interval)
        timer_->setInterval(interval);
    if (!timer_->isActive())
        timer_->start();
}

int NekoWallpaperItem::effective_fps_limit() const
{
    const int configured = state_->fps_limit.load(std::memory_order_acquire);
    constexpr auto kIdleDelay = std::chrono::seconds(10);
    const bool idle = std::chrono::steady_clock::now() - last_activity_ >= kIdleDelay;
    return idle_fps_limit(configured, idle);
}

bool NekoWallpaperItem::global_pointer_active() const
{
    return pointer_provider_ && pointer_provider_->scope() == PointerScope::GlobalDesktop;
}

void NekoWallpaperItem::stop_timer()
{
    if (timer_)
        timer_->stop();
}

void NekoWallpaperItem::poll_global_pointer()
{
    if (!pointer_provider_ || effective_paused_)
        return;
    const auto position = pointer_provider_->positionFor(*this);
    if (!position)
        return;
    const qreal dx = position->x() - last_pointer_position_.x();
    const qreal dy = position->y() - last_pointer_position_.y();
    if (has_last_pointer_ && std::abs(dx) + std::abs(dy) < 0.5)
        return;
    last_pointer_position_ = *position;
    has_last_pointer_ = true;
    enqueue(nekomote::qt_quick::InputKind::Move, *position);
}

void NekoWallpaperItem::start_pointer_timer()
{
    if (!pointer_timer_ || !pointer_provider_)
        return;
    if (!pointer_timer_->isActive())
    {
        has_last_pointer_ = false;
        pointer_timer_->start();
    }
}

void NekoWallpaperItem::stop_pointer_timer()
{
    if (pointer_timer_)
        pointer_timer_->stop();
    has_last_pointer_ = false;
}

void NekoWallpaperItem::poll_occlusion()
{
    if (!occlusion_provider_)
        return;
    const bool occluded = occlusion_provider_->fullyObscured(*this);
    if (occluded == occluded_)
        return;
    occluded_ = occluded;
    refresh_playback();
}

void NekoWallpaperItem::start_occlusion_timer()
{
    if (occlusion_timer_ && occlusion_provider_ && !occlusion_timer_->isActive())
        occlusion_timer_->start();
}

void NekoWallpaperItem::stop_occlusion_timer()
{
    if (occlusion_timer_)
        occlusion_timer_->stop();
}
} // namespace nekomote::plasma
