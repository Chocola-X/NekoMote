#include "session_item.hpp"
#if defined(NEKOMOTE_HAS_VULKAN)
#include "vulkan_gl_bridge.hpp"
#endif

#include <QHoverEvent>
#include <QMouseEvent>
#include <QObject>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQuickOpenGLUtils>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QWheelEvent>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <string>
#include <utility>

namespace nekomote::qt_quick
{
namespace
{
void check(nekomote_error result, const char* operation)
{
    if (result != NEKOMOTE_OK)
    {
        const auto* message = nekomote_error_message();
        throw std::runtime_error(std::string(operation) + ": " +
                                 (message ? message : "unknown ABI error"));
    }
}
nekomote_gl_proc load_gl(const char* name)
{
    auto* context = QOpenGLContext::currentContext();
    return context ? context->getProcAddress(name) : nullptr;
}
} // namespace

class WallpaperRenderer final : public QObject, public QQuickFramebufferObject::Renderer
{
    Q_OBJECT
  public:
    explicit WallpaperRenderer(std::shared_ptr<WallpaperState> state) : state_(std::move(state))
    {
        if (state_)
            generation_ = state_->renderer_created.fetch_add(1) + 1;
    }
    ~WallpaperRenderer() override
    {
        if (state_)
        {
#if defined(NEKOMOTE_HAS_VULKAN)
            if (bridge_)
            {
                bridge_->wait_noexcept();
                state_->wallpaper.reset();
                bridge_.reset();
            }
            else
#endif
                if (state_->wallpaper)
                nekomote_wallpaper_reset_gl(state_->wallpaper.get());
            state_->loaded = false;
            state_->initialized_time = false;
            state_->renderer_destroyed.fetch_add(1);
        }
    }
    void synchronize(QQuickFramebufferObject* base) override
    {
        auto* item = static_cast<WallpaperItemBase*>(base);
        if (!state_)
        {
            state_ = item->wallpaper_state();
            if (!state_)
                return;
            generation_ = state_->renderer_created.fetch_add(1) + 1;
        }
        if (!connected_)
        {
            connect(this, &WallpaperRenderer::frame_ready, item, &WallpaperItemBase::frame_ready,
                    Qt::QueuedConnection);
            connect(this, &WallpaperRenderer::failed, item, &WallpaperItemBase::failed,
                    Qt::QueuedConnection);
            connected_ = true;
        }
        // A recreated scene graph gets a new renderer even when GUI settings
        // are unchanged. Copy its initial snapshot before attempting a reload.
        if (!config_received_ || state_->config_dirty)
        {
            // GUI setters buffer configuration; synchronize is the only point
            // at which Qt blocks GUI writes while the render thread copies it.
            // Catalog, icon/font bindings and GPU resources belong to one asset
            // root. Switching it must not retain a UI initialized for the old root.
            needs_rebuild_ = needs_rebuild_ || assets_dir_ != state_->assets_dir ||
                             assets_mode_ != state_->assets_mode;
            const bool reload =
                assets_dir_ != state_->assets_dir || model_path_ != state_->model_path ||
                session_path_ != state_->session_path || assets_mode_ != state_->assets_mode ||
                deterministic_ != state_->deterministic || seed_ != state_->seed;
            assets_dir_ = state_->assets_dir;
            model_path_ = state_->model_path;
            session_path_ = state_->session_path;
            assets_mode_ = state_->assets_mode;
            muted_ = state_->muted;
            no_save_ = state_->no_save;
            seed_ = state_->seed;
            deterministic_ = state_->deterministic;
            fixed_steps_ = state_->fixed_steps;
            trace_frames_ = state_->trace_frames;
            requested_backend_ = state_->backend;
            if (reload)
            {
                state_->loaded = false;
                state_->initialized_time = false;
            }
            config_applied_ = false;
            faulted_ = false;
            state_->config_dirty = false;
            config_received_ = true;
        }
        const auto item_state = item->get_item_state();
        notify_frames_ = deterministic_ || item_state.notify_frames;
        if (notify_frames_ && !notifications_connected_)
        {
            connect(
                item->window(), &QQuickWindow::afterFrameEnd, this,
                [this]
                {
                    if (!diagnostic_pending_)
                        return;
                    diagnostic_pending_ = false;
                    emit frame_ready(output_size_, generation_, diagnostic_delta_,
                                     diagnostic_reaction_);
                },
                Qt::DirectConnection);
            notifications_connected_ = true;
        }
        logical_size_ = item->size();
        paused_ = item_state.paused;
        if (playback_revision_ != item_state.playback_revision)
        {
            clock_attached_ = false;
            playback_revision_ = item_state.playback_revision;
        }
        input_count_ = item_state.input_count;
        inputs_ = item_state.inputs;
        item->clear_inputs();
        graphics_api_ = item->window()->rendererInterface()->graphicsApi();
    }
    void render() override
    {
        if (!state_)
        {
            if (auto* context = QOpenGLContext::currentContext())
            {
                auto* gl = context->functions();
                gl->glDisable(GL_SCISSOR_TEST);
                gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                gl->glClearColor(0, 0, 0, 0);
                gl->glClear(GL_COLOR_BUFFER_BIT);
                QQuickOpenGLUtils::resetOpenGLState();
            }
            return;
        }
        if (faulted_)
            return;
        try
        {
            const auto trace_start = trace_frames_ > 0 ? std::chrono::steady_clock::now()
                                                       : std::chrono::steady_clock::time_point{};
            timespec cpu_start{};
            const bool cpu_clock_valid =
                trace_frames_ > 0 && ::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_start) == 0;
            if (graphics_api_ != QSGRendererInterface::OpenGL || !QOpenGLContext::currentContext())
                throw std::runtime_error("Qt Quick host requires an OpenGL scene graph/context");
            const auto size = framebufferObject()->size();
            if (active_backend_ != requested_backend_ || needs_rebuild_)
            {
#if defined(NEKOMOTE_HAS_VULKAN)
                if (bridge_)
                    bridge_->wait_ready();
#endif
                state_->wallpaper.reset();
#if defined(NEKOMOTE_HAS_VULKAN)
                bridge_.reset();
#endif
                active_backend_ = requested_backend_;
                state_->loaded = false;
                state_->initialized_time = false;
                output_size_ = {};
                clock_attached_ = false;
                needs_rebuild_ = false;
            }
#if defined(NEKOMOTE_HAS_VULKAN)
            if (bridge_)
                bridge_->wait_ready();
#endif
            if (!state_->wallpaper)
            {
                if (trace_frames_)
                {
                    auto* gl = QOpenGLContext::currentContext()->functions();
                    std::fprintf(stderr, "Qt GL vendor=%s renderer=%s version=%s\n",
                                 gl->glGetString(GL_VENDOR), gl->glGetString(GL_RENDERER),
                                 gl->glGetString(GL_VERSION));
                }
                if (active_backend_ == GraphicsBackend::Vulkan)
                {
#if defined(NEKOMOTE_HAS_VULKAN)
                    if (!bridge_)
                        bridge_ = std::make_unique<VulkanGlBridge>();
                    state_->wallpaper.reset(
                        nekomote_wallpaper_create_vk(bridge_->device_context()));
#else
                    throw std::runtime_error(
                        "NekoMote was built without Vulkan support; select OpenGL");
#endif
                }
                else
                    state_->wallpaper.reset(nekomote_wallpaper_create(load_gl));
                if (!state_->wallpaper)
                    throw std::runtime_error("create wallpaper");
                assets_applied_ = false;
                config_applied_ = false;
            }
            if (!config_applied_)
            {
                check(nekomote_wallpaper_set_no_save(state_->wallpaper.get(), no_save_ ? 1 : 0),
                      "set no save");
                if (assets_mode_ && !assets_dir_.empty() && !assets_applied_)
                {
                    check(nekomote_wallpaper_set_assets_dir(state_->wallpaper.get(),
                                                            assets_dir_.c_str()),
                          "set assets dir");
                    assets_applied_ = true;
                }
                check(nekomote_wallpaper_set_seed(state_->wallpaper.get(), seed_), "set seed");
                check(nekomote_wallpaper_set_muted(state_->wallpaper.get(), muted_ ? 1 : 0),
                      "set muted");
                config_applied_ = true;
            }
            // Loading may fit the character and initialize output-dependent
            // services. Supply the actual FBO size before that first load.
            if (size != output_size_ || !state_->loaded)
            {
                check(nekomote_wallpaper_set_output_size(state_->wallpaper.get(), size.width(),
                                                         size.height()),
                      "set output size");
                output_size_ = size;
            }
            if (!state_->loaded)
            {
                if (assets_mode_ && !assets_dir_.empty())
                    check(nekomote_wallpaper_load_initial(state_->wallpaper.get()), "load initial");
                else if (!assets_mode_ && !model_path_.empty())
                    check(nekomote_wallpaper_load_model(state_->wallpaper.get(),
                                                        model_path_.c_str(), session_path_.c_str()),
                          "load model");
                state_->loaded = true;
            }
            const auto now = std::chrono::steady_clock::now();
            const bool first = !state_->initialized_time;
            if (first || !clock_attached_ || state_->paused != paused_)
            {
                check(nekomote_wallpaper_set_paused(state_->wallpaper.get(), paused_ ? 1 : 0),
                      "set playback state");
                state_->paused = paused_;
                state_->previous_time = now;
            }
            clock_attached_ = true;
            const double elapsed =
                std::chrono::duration<double>(now - state_->previous_time).count();
            dispatch_inputs();
            if (first && deterministic_ && !paused_)
            {
                for (int step = 0; step < fixed_steps_; ++step)
                    check(nekomote_wallpaper_advance(state_->wallpaper.get(), 1.0 / 60),
                          "fixed advance");
                state_->fixed_steps_executed.store(static_cast<unsigned>(fixed_steps_));
            }
            if (!deterministic_ && !first)
                check(nekomote_wallpaper_advance(state_->wallpaper.get(), elapsed),
                      "advance wallpaper");
            state_->initialized_time = state_->initialized_time || !paused_;
            state_->previous_time = now;
            {
                auto* gl = QOpenGLContext::currentContext()->functions();
                gl->glDisable(GL_SCISSOR_TEST);
                gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                gl->glClearColor(0, 0, 0, 0);
                gl->glClear(GL_COLOR_BUFFER_BIT);
            }
#if defined(NEKOMOTE_HAS_VULKAN)
            if (bridge_)
                check(nekomote_wallpaper_set_vk_target(state_->wallpaper.get(),
                                                       bridge_->begin_frame(size)),
                      "set Vulkan frame target");
            else
#endif
                nekomote_wallpaper_set_render_target(state_->wallpaper.get(),
                                                     framebufferObject()->handle());
            check(nekomote_wallpaper_render_frame(state_->wallpaper.get()), "render frame");
            check(nekomote_wallpaper_render_ui(state_->wallpaper.get(),
                                               paused_ ? 0.0f : static_cast<float>(elapsed)),
                  "render UI");
            const int fps_limit = nekomote_wallpaper_fps_limit(state_->wallpaper.get());
            if (fps_limit < 0)
                throw std::runtime_error("query wallpaper frame limit");
            state_->fps_limit.store(fps_limit, std::memory_order_release);
#if defined(NEKOMOTE_HAS_VULKAN)
            if (bridge_)
                bridge_->present(framebufferObject()->handle());
#endif
            if (trace_frames_ > 0)
            {
                --trace_frames_;
                const auto trace_end = std::chrono::steady_clock::now();
                timespec cpu_end{};
                const double cpu_us =
                    cpu_clock_valid && ::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_end) == 0
                        ? static_cast<double>(cpu_end.tv_sec - cpu_start.tv_sec) * 1e6 +
                              static_cast<double>(cpu_end.tv_nsec - cpu_start.tv_nsec) / 1e3
                        : -1.0;
                std::fprintf(
                    stderr,
                    "Native frame thread_CPU_us=%.3f submission_wall_us=%.3f elapsed_s=%.6f\n",
                    cpu_us,
                    std::chrono::duration<double, std::micro>(trace_end - trace_start).count(),
                    elapsed);
            }
            state_->rendered_frames.fetch_add(1, std::memory_order_relaxed);
            if (notify_frames_ && state_->initialized_time)
            {
                diagnostic_delta_ = elapsed;
                diagnostic_reaction_ = QString{};
                diagnostic_pending_ = true;
            }
        }
        catch (const std::exception& error)
        {
            faulted_ = true;
            // A failed ABI call can leave a recorded, unsubmitted target. A
            // later config retry must recreate owners before recording again.
            needs_rebuild_ = true;
            const auto diagnostic = QString::fromUtf8(error.what());
            emit failed(diagnostic);
        }
        QQuickOpenGLUtils::resetOpenGLState();
    }

  signals:
    void frame_ready(QSize framebuffer_size, unsigned generation, double clock_delta,
                     QString reaction);
    void failed(QString diagnostic);

  private:
    void dispatch_inputs()
    {
        for (std::size_t i = 0; i < input_count_; ++i)
        {
            const auto& input = inputs_[i];
            const float x = input.x * static_cast<float>(output_size_.width()) /
                            static_cast<float>(logical_size_.width());
            const float y = input.y * static_cast<float>(output_size_.height()) /
                            static_cast<float>(logical_size_.height());
            nekomote_error result = NEKOMOTE_OK;
            switch (input.kind)
            {
                case InputKind::Enter:
                    result = nekomote_wallpaper_pointer_enter(state_->wallpaper.get(), x, y);
                    break;
                case InputKind::Move:
                    result = nekomote_wallpaper_pointer_move(state_->wallpaper.get(), x, y,
                                                             input.timestamp_ms);
                    break;
                case InputKind::Leave:
                    result = nekomote_wallpaper_pointer_leave(state_->wallpaper.get());
                    break;
                case InputKind::Touch:
                    result = nekomote_wallpaper_touch(state_->wallpaper.get(), x, y);
                    break;
                case InputKind::Press:
                case InputKind::Release:
                    check(nekomote_wallpaper_pointer_move(state_->wallpaper.get(), x, y,
                                                          input.timestamp_ms),
                          "button position");
                    result = nekomote_wallpaper_mouse_button(state_->wallpaper.get(), input.button,
                                                             input.kind == InputKind::Press ? 1 : 0,
                                                             input.timestamp_ms);
                    break;
                case InputKind::Scroll:
                    check(nekomote_wallpaper_pointer_move(state_->wallpaper.get(), x, y,
                                                          input.timestamp_ms),
                          "scroll position");
                    result = nekomote_wallpaper_mouse_scroll(state_->wallpaper.get(), input.scroll);
                    break;
            }
            if (result != NEKOMOTE_ERROR_BUSY && result != NEKOMOTE_ERROR_INPUT_UNAVAILABLE)
                check(result, "input event");
        }
        input_count_ = 0;
    }
    std::shared_ptr<WallpaperState> state_;
    QSizeF logical_size_;
    QSize output_size_;
    QSGRendererInterface::GraphicsApi graphics_api_{QSGRendererInterface::Unknown};
    double diagnostic_delta_{0};
    QString diagnostic_reaction_;
    bool diagnostic_pending_{false};
    unsigned generation_{0};
    std::uint64_t playback_revision_{0};
    bool paused_{false}, faulted_{false}, connected_{false}, clock_attached_{false};
    bool config_received_{false}, config_applied_{false};
    bool assets_applied_{false};
    bool needs_rebuild_{false};
    std::filesystem::path assets_dir_, model_path_, session_path_;
    std::uint32_t seed_{0};
    int fixed_steps_{0}, trace_frames_{0};
    bool muted_{false}, no_save_{false}, assets_mode_{true}, deterministic_{false};
    GraphicsBackend requested_backend_{GraphicsBackend::OpenGL};
    GraphicsBackend active_backend_{GraphicsBackend::OpenGL};
#if defined(NEKOMOTE_HAS_VULKAN)
    std::unique_ptr<VulkanGlBridge> bridge_;
#endif
    bool notify_frames_{false}, notifications_connected_{false};
    std::array<Input, 32> inputs_{};
    std::size_t input_count_{0};
};

QQuickFramebufferObject::Renderer* create_wallpaper_renderer(std::shared_ptr<WallpaperState> state)
{
    return new WallpaperRenderer(std::move(state));
}

WallpaperItem::WallpaperItem(std::shared_ptr<WallpaperState> state, QQuickItem* parent)
    : WallpaperItemBase(parent), state_(std::move(state))
{
    setMirrorVertically(true);
    setAcceptHoverEvents(!state_->deterministic);
    setAcceptedMouseButtons(state_->deterministic ? Qt::NoButton : Qt::LeftButton);
}
QQuickFramebufferObject::Renderer* WallpaperItem::createRenderer() const
{
    return create_wallpaper_renderer(state_);
}
void WallpaperItem::set_paused(bool paused)
{
    if (paused_ == paused)
        return;
    paused_ = paused;
    ++playback_revision_;
    update();
}
ItemState WallpaperItem::get_item_state() const
{
    return {paused_, playback_revision_, inputs_, input_count_};
}
std::shared_ptr<WallpaperState> WallpaperItem::wallpaper_state() const
{
    return state_;
}
void WallpaperItem::clear_inputs()
{
    input_count_ = 0;
}
void WallpaperItem::enqueue(InputKind kind, QPointF position, float scroll, int button,
                            std::uint64_t timestamp_ms)
{
    if (input_count_ == inputs_.size())
    {
        emit failed(QStringLiteral("Qt Quick input queue capacity exceeded"));
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
void WallpaperItem::replay_touch(QPointF logical_position)
{
    enqueue(InputKind::Touch, logical_position);
}
void WallpaperItem::hoverEnterEvent(QHoverEvent* event)
{
    enqueue(InputKind::Enter, event->position(), 0, 0, event->timestamp());
}
void WallpaperItem::hoverMoveEvent(QHoverEvent* event)
{
    enqueue(InputKind::Move, event->position(), 0, 0, event->timestamp());
}
void WallpaperItem::hoverLeaveEvent(QHoverEvent*)
{
    enqueue(InputKind::Leave);
}
void WallpaperItem::mousePressEvent(QMouseEvent* event)
{
    enqueue(InputKind::Press, event->position(), 0, event->button() == Qt::RightButton ? 1 : 0,
            event->timestamp());
    event->accept();
}
void WallpaperItem::mouseReleaseEvent(QMouseEvent* event)
{
    enqueue(InputKind::Release, event->position(), 0, event->button() == Qt::RightButton ? 1 : 0,
            event->timestamp());
    event->accept();
}
void WallpaperItem::mouseMoveEvent(QMouseEvent* event)
{
    enqueue(InputKind::Move, event->position(), 0, 0, event->timestamp());
    event->accept();
}
void WallpaperItem::wheelEvent(QWheelEvent* event)
{
    enqueue(InputKind::Scroll, event->position(), static_cast<float>(event->angleDelta().y()) / 120,
            0, event->timestamp());
    event->accept();
}
} // namespace nekomote::qt_quick

#include "session_item.moc"
