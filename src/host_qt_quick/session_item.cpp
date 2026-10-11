#include "session_item.hpp"

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
#include <cstdio>
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
        if (state_->config_dirty)
        {
            config_applied_ = false;
            faulted_ = false;
            state_->config_dirty = false;
        }
        const auto item_state = item->get_item_state();
        notify_frames_ = state_->deterministic || item_state.notify_frames;
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
            const auto trace_start = state_->trace_frames > 0
                                         ? std::chrono::steady_clock::now()
                                         : std::chrono::steady_clock::time_point{};
            if (graphics_api_ != QSGRendererInterface::OpenGL || !QOpenGLContext::currentContext())
                throw std::runtime_error("Qt Quick host requires an OpenGL scene graph/context");
            const auto size = framebufferObject()->size();
            if (!state_->wallpaper)
            {
                if (state_->trace_frames)
                {
                    auto* gl = QOpenGLContext::currentContext()->functions();
                    std::fprintf(stderr, "Qt GL vendor=%s renderer=%s version=%s\n",
                                 gl->glGetString(GL_VENDOR), gl->glGetString(GL_RENDERER),
                                 gl->glGetString(GL_VERSION));
                }
                state_->wallpaper.reset(nekomote_wallpaper_create(load_gl));
                if (!state_->wallpaper)
                    throw std::runtime_error("create wallpaper");
                config_applied_ = false;
            }
            if (!config_applied_)
            {
                if (!state_->assets_dir.empty())
                    check(nekomote_wallpaper_set_assets_dir(state_->wallpaper.get(),
                                                            state_->assets_dir.c_str()),
                          "set assets dir");
                check(nekomote_wallpaper_set_seed(state_->wallpaper.get(), state_->seed),
                      "set seed");
                check(nekomote_wallpaper_set_muted(state_->wallpaper.get(), state_->muted ? 1 : 0),
                      "set muted");
                if (state_->no_save)
                    check(nekomote_wallpaper_set_no_save(state_->wallpaper.get(), 1),
                          "set no save");
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
                if (state_->assets_mode && !state_->assets_dir.empty())
                    check(nekomote_wallpaper_load_initial(state_->wallpaper.get()), "load initial");
                else if (!state_->assets_mode && !state_->model_path.empty())
                    check(nekomote_wallpaper_load_model(state_->wallpaper.get(),
                                                        state_->model_path.c_str(),
                                                        state_->session_path.c_str()),
                          "load model");
                state_->loaded = true;
            }
            nekomote_wallpaper_set_render_target(state_->wallpaper.get(),
                                                 framebufferObject()->handle());
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
            if (first && state_->deterministic && !paused_)
            {
                for (int step = 0; step < state_->fixed_steps; ++step)
                    check(nekomote_wallpaper_advance(state_->wallpaper.get(), 1.0 / 60),
                          "fixed advance");
                state_->fixed_steps_executed.store(static_cast<unsigned>(state_->fixed_steps));
            }
            if (!state_->deterministic && !first)
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
            check(nekomote_wallpaper_render_frame(state_->wallpaper.get()), "render frame");
            check(nekomote_wallpaper_render_ui(state_->wallpaper.get(),
                                               paused_ ? 0.0f : static_cast<float>(elapsed)),
                  "render UI");
            if (state_->trace_frames > 0)
            {
                --state_->trace_frames;
                const auto trace_end = std::chrono::steady_clock::now();
                std::fprintf(
                    stderr, "Native frame CPU_us=%.3f render_us=%.3f elapsed_s=%.6f\n",
                    std::chrono::duration<double, std::micro>(trace_start - trace_start).count(),
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
                    result = nekomote_wallpaper_pointer_move(state_->wallpaper.get(), x, y);
                    break;
                case InputKind::Leave:
                    result = nekomote_wallpaper_pointer_leave(state_->wallpaper.get());
                    break;
                case InputKind::Touch:
                    result = nekomote_wallpaper_touch(state_->wallpaper.get(), x, y);
                    break;
                case InputKind::Press:
                case InputKind::Release:
                    check(nekomote_wallpaper_pointer_move(state_->wallpaper.get(), x, y),
                          "button position");
                    result =
                        nekomote_wallpaper_mouse_button(state_->wallpaper.get(), input.button,
                                                        input.kind == InputKind::Press ? 1 : 0);
                    if (result == NEKOMOTE_OK && input.kind == InputKind::Press &&
                        input.button == 0)
                        result = nekomote_wallpaper_touch(state_->wallpaper.get(), x, y);
                    break;
                case InputKind::Scroll:
                    check(nekomote_wallpaper_pointer_move(state_->wallpaper.get(), x, y),
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
    bool config_applied_{false};
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
    setAcceptedMouseButtons(state_->deterministic ? Qt::NoButton
                                                  : Qt::LeftButton | Qt::RightButton);
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
void WallpaperItem::enqueue(InputKind kind, QPointF position, float scroll, int button)
{
    if (input_count_ == inputs_.size())
    {
        emit failed(QStringLiteral("Qt Quick input queue capacity exceeded"));
        return;
    }
    inputs_[input_count_++] = {kind, static_cast<float>(position.x()),
                               static_cast<float>(position.y()), scroll, button};
    update();
}
void WallpaperItem::replay_touch(QPointF logical_position)
{
    enqueue(InputKind::Touch, logical_position);
}
void WallpaperItem::hoverEnterEvent(QHoverEvent* event)
{
    enqueue(InputKind::Enter, event->position());
}
void WallpaperItem::hoverMoveEvent(QHoverEvent* event)
{
    enqueue(InputKind::Move, event->position());
}
void WallpaperItem::hoverLeaveEvent(QHoverEvent*)
{
    enqueue(InputKind::Leave);
}
void WallpaperItem::mousePressEvent(QMouseEvent* event)
{
    enqueue(InputKind::Press, event->position(), 0, event->button() == Qt::RightButton ? 1 : 0);
    event->accept();
}
void WallpaperItem::mouseReleaseEvent(QMouseEvent* event)
{
    enqueue(InputKind::Release, event->position(), 0, event->button() == Qt::RightButton ? 1 : 0);
    event->accept();
}
void WallpaperItem::mouseMoveEvent(QMouseEvent* event)
{
    enqueue(InputKind::Move, event->position());
    event->accept();
}
void WallpaperItem::wheelEvent(QWheelEvent* event)
{
    enqueue(InputKind::Scroll, event->position(),
            static_cast<float>(event->angleDelta().y()) / 120);
    event->accept();
}
} // namespace nekomote::qt_quick

#include "session_item.moc"
