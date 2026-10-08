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
template <typename T>
T* require(T* pointer, const char* operation)
{
    if (!pointer)
        check(nekomote_last_error() == NEKOMOTE_OK ? NEKOMOTE_ERROR_FAULTED : nekomote_last_error(),
              operation);
    return pointer;
}
nekomote_gl_proc load_gl(const char* name)
{
    auto* context = QOpenGLContext::currentContext();
    return context ? context->getProcAddress(name) : nullptr;
}
} // namespace

std::shared_ptr<SessionState> load_session(const std::filesystem::path& model_path,
                                           const std::filesystem::path& session_path,
                                           std::uint32_t seed, float origin_x, float origin_y,
                                           float scale, int steps, bool deterministic)
{
    Handle<nekomote_model, nekomote_model_destroy> model{
        require(nekomote_model_load(model_path.c_str()), "load model"), nekomote_model_destroy};
    Handle<nekomote_session_asset, nekomote_session_asset_destroy> definition{
        require(nekomote_session_asset_load(session_path.c_str()), "load session"),
        nekomote_session_asset_destroy};
    auto state = std::make_shared<SessionState>();
    state->program.reset(
        require(nekomote_program_compile(model.get(), definition.get()), "compile program"));
    state->session.reset(
        require(nekomote_session_create(state->program.get(), {seed, NEKOMOTE_POINTER_LOCAL_SURFACE,
                                                               NEKOMOTE_AUDIO_DISABLED}),
                "create session"));
    check(nekomote_session_set_origin(state->session.get(), origin_x, origin_y), "set origin");
    check(nekomote_session_set_scale(state->session.get(), scale), "set scale");
    check(nekomote_session_start(state->session.get()), "start session");
    check(nekomote_session_prepare_frame(state->session.get()), "validate initial geometry");
    const auto count = nekomote_program_texture_count(state->program.get());
    state->textures.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto info = nekomote_program_texture(state->program.get(), i);
        if (!info.path || !info.id)
            throw std::runtime_error("program texture has no path/id");
        const auto path = model_path.parent_path() / info.path;
        Texture texture{info.id};
        texture.image.reset(require(nekomote_image_load_png(path.c_str()), "decode texture"));
        state->textures.push_back(std::move(texture));
    }
    state->fixed_steps = steps;
    state->deterministic = deterministic;
    return state;
}

class SessionRenderer final : public QObject, public QQuickFramebufferObject::Renderer
{
    Q_OBJECT
  public:
    explicit SessionRenderer(std::shared_ptr<SessionState> state) : state_(std::move(state))
    {
        generation_ = state_->renderer_created.fetch_add(1) + 1;
    }
    ~SessionRenderer() override
    {
        // Qt deletes FBO renderers on the render thread while cleaning scene
        // graph resources. GPU destruction precedes release of borrowed CPU state.
        renderer_.reset();
        state_->renderer_destroyed.fetch_add(1);
    }
    void synchronize(QQuickFramebufferObject* base) override
    {
        auto* item = static_cast<SessionItemBase*>(base);
        if (!connected_)
        {
            connect(this, &SessionRenderer::frame_ready, item, &SessionItemBase::frame_ready,
                    Qt::QueuedConnection);
            connect(this, &SessionRenderer::failed, item, &SessionItemBase::failed,
                    Qt::QueuedConnection);
            if (state_->deterministic)
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
            connected_ = true;
        }
        logical_size_ = item->size();
        const auto item_state = item->get_item_state();
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
            if (!renderer_)
            {
                if (state_->trace_frames)
                {
                    auto* gl = QOpenGLContext::currentContext()->functions();
                    std::fprintf(stderr, "Qt GL vendor=%s renderer=%s version=%s\n",
                                 gl->glGetString(GL_VENDOR), gl->glGetString(GL_RENDERER),
                                 gl->glGetString(GL_VERSION));
                }
                renderer_.reset(require(nekomote_renderer_create(load_gl), "create renderer"));
                check(nekomote_renderer_reserve(renderer_.get(), nekomote_program_renderer_capacity(
                                                                     state_->program.get())),
                      "reserve renderer");
                for (const auto& texture : state_->textures)
                {
                    const auto* image = texture.image.get();
                    check(nekomote_renderer_upload_texture(
                              renderer_.get(), texture.id, nekomote_image_width(image),
                              nekomote_image_height(image), nekomote_image_pixels(image),
                              nekomote_image_byte_count(image)),
                          "upload texture");
                }
            }
            const bool resize_output = size != output_size_;
            if (resize_output)
            {
                check(nekomote_session_set_output_size(state_->session.get(), size.width(),
                                                       size.height()),
                      "set output size");
                output_size_ = size;
            }
            const auto now = std::chrono::steady_clock::now();
            const bool first = !state_->initialized_time;
            if (first || !clock_attached_ || state_->paused != paused_)
            {
                check(nekomote_session_set_playback_state(state_->session.get(),
                                                          paused_ ? NEKOMOTE_PLAYBACK_PAUSED
                                                                  : NEKOMOTE_PLAYBACK_RUNNING),
                      "set playback state");
                state_->paused = paused_;
                state_->previous_time = now;
            }
            clock_attached_ = true;
            const double elapsed =
                std::chrono::duration<double>(now - state_->previous_time).count();
            dispatch_inputs();
            if (first && state_->deterministic)
                for (int step = 0; step < state_->fixed_steps; ++step)
                    check(nekomote_session_advance(state_->session.get(), 1.0 / 60),
                          "fixed advance");
            if (!state_->deterministic && !first)
                check(nekomote_session_advance(state_->session.get(), elapsed), "advance session");
            state_->initialized_time = true;
            state_->previous_time = now;
            check(nekomote_session_prepare_frame(state_->session.get()), "prepare session");
            const auto trace_cpu = state_->trace_frames > 0
                                       ? std::chrono::steady_clock::now()
                                       : std::chrono::steady_clock::time_point{};
            check(nekomote_renderer_prepare(renderer_.get(),
                                            nekomote_session_frame(state_->session.get())),
                  "prepare renderer");
            // prepare selects protected composition; its intermediate output is
            // allocated at the explicit resize boundary after that selection.
            if (resize_output)
                check(nekomote_renderer_resize_output(renderer_.get(), size.width(), size.height()),
                      "resize renderer");
            nekomote_render_view view{};
            view.origin_x = static_cast<float>(size.width()) * .5f;
            view.origin_y = static_cast<float>(size.height()) * .5f;
            view.scale = view.alpha = 1;
            view.flip_x = nekomote_program_mirror(state_->program.get());
            check(nekomote_renderer_render(
                      renderer_.get(), {framebufferObject()->handle(), size.width(), size.height()},
                      view, NEKOMOTE_TARGET_CLEAR),
                  "render frame");
            if (state_->trace_frames > 0)
            {
                --state_->trace_frames;
                const auto trace_end = std::chrono::steady_clock::now();
                std::fprintf(
                    stderr, "Native frame CPU_us=%.3f prepare_render_us=%.3f elapsed_s=%.6f\n",
                    std::chrono::duration<double, std::micro>(trace_cpu - trace_start).count(),
                    std::chrono::duration<double, std::micro>(trace_end - trace_cpu).count(),
                    elapsed);
            }
            state_->rendered_frames.fetch_add(1, std::memory_order_relaxed);
            // Notifications are diagnostic only and not the playback frame path.
            if (state_->deterministic)
            {
                const auto reaction = nekomote_session_active_reaction(state_->session.get());
                const auto* label =
                    nekomote_program_reaction_label(state_->program.get(), reaction);
                diagnostic_delta_ = elapsed;
                diagnostic_reaction_ = label ? QString::fromUtf8(label) : QString{};
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
            // Logical item pixels -> actual FBO pixels -> centered output. No X flip.
            const float x = input.x * static_cast<float>(output_size_.width()) /
                                static_cast<float>(logical_size_.width()) -
                            static_cast<float>(output_size_.width()) * .5f;
            const float y = input.y * static_cast<float>(output_size_.height()) /
                                static_cast<float>(logical_size_.height()) -
                            static_cast<float>(output_size_.height()) * .5f;
            nekomote_error result = NEKOMOTE_OK;
            switch (input.kind)
            {
                case InputKind::Enter:
                    result = nekomote_session_pointer_enter(state_->session.get(), x, y);
                    break;
                case InputKind::Move:
                    result = nekomote_session_pointer_move(state_->session.get(), x, y);
                    break;
                case InputKind::Leave:
                    result = nekomote_session_pointer_leave(state_->session.get());
                    break;
                case InputKind::Touch:
                    result = nekomote_session_touch(state_->session.get(), x, y);
                    break;
            }
            if (result != NEKOMOTE_ERROR_BUSY && result != NEKOMOTE_ERROR_INPUT_UNAVAILABLE)
                check(result, "input event");
        }
        input_count_ = 0;
    }
    std::shared_ptr<SessionState> state_;
    Handle<nekomote_renderer, nekomote_renderer_destroy> renderer_{nullptr,
                                                                   nekomote_renderer_destroy};
    QSizeF logical_size_;
    QSize output_size_;
    QSGRendererInterface::GraphicsApi graphics_api_{QSGRendererInterface::Unknown};
    double diagnostic_delta_{0};
    QString diagnostic_reaction_;
    bool diagnostic_pending_{false};
    unsigned generation_{0};
    std::uint64_t playback_revision_{0};
    bool paused_{false}, faulted_{false}, connected_{false}, clock_attached_{false};
    std::array<Input, 32> inputs_{};
    std::size_t input_count_{0};
};

QQuickFramebufferObject::Renderer* create_session_renderer(std::shared_ptr<SessionState> state)
{
    return new SessionRenderer(std::move(state));
}

SessionItem::SessionItem(std::shared_ptr<SessionState> state, QQuickItem* parent)
    : SessionItemBase(parent), state_(std::move(state))
{
    setMirrorVertically(true);
    // Real pointer motion must not contaminate deterministic captures. Diagnostic
    // replay still uses the very same queue and framebuffer conversion.
    setAcceptHoverEvents(!state_->deterministic);
    setAcceptedMouseButtons(state_->deterministic ? Qt::NoButton : Qt::LeftButton);
}
QQuickFramebufferObject::Renderer* SessionItem::createRenderer() const
{
    return create_session_renderer(state_);
}
void SessionItem::set_paused(bool paused)
{
    if (paused_ == paused)
        return;
    paused_ = paused;
    ++playback_revision_;
    update();
}
ItemState SessionItem::get_item_state() const
{
    return {paused_, playback_revision_, inputs_, input_count_};
}
std::shared_ptr<SessionState> SessionItem::session_state() const
{
    return state_;
}
void SessionItem::clear_inputs()
{
    input_count_ = 0;
}
void SessionItem::enqueue(InputKind kind, QPointF position)
{
    if (input_count_ == inputs_.size())
    {
        emit failed(QStringLiteral("Qt Quick input queue capacity exceeded"));
        return;
    }
    inputs_[input_count_++] = {kind, static_cast<float>(position.x()),
                               static_cast<float>(position.y())};
    update();
}
void SessionItem::replay_touch(QPointF logical_position)
{
    enqueue(InputKind::Touch, logical_position);
}
void SessionItem::hoverEnterEvent(QHoverEvent* event)
{
    enqueue(InputKind::Enter, event->position());
}
void SessionItem::hoverMoveEvent(QHoverEvent* event)
{
    enqueue(InputKind::Move, event->position());
}
void SessionItem::hoverLeaveEvent(QHoverEvent*)
{
    enqueue(InputKind::Leave);
}
void SessionItem::mousePressEvent(QMouseEvent* event)
{
    enqueue(InputKind::Touch, event->position());
    event->accept();
}
} // namespace nekomote::qt_quick

#include "session_item.moc"
