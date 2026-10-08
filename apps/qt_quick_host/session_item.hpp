#pragma once

#include <nekomote/abi.h>

#include <QPointF>
#include <QQuickFramebufferObject>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace nekomote::qt_quick
{
template <typename T, void (*Destroy)(T*)>
using Handle = std::unique_ptr<T, decltype(Destroy)>;

struct Texture
{
    std::uint32_t id;
    Handle<nekomote_image, nekomote_image_destroy> image{nullptr, nekomote_image_destroy};
};

// Shared ownership only spans the GUI item and its scene-graph renderer. CPU
// initialization occurs before showing the window; thereafter only the render
// thread evaluates the session. GUI writes go through synchronize(), never here.
// The CPU state survives scene-graph invalidation; GPU resources do not.
struct SessionState
{
    Handle<nekomote_program, nekomote_program_destroy> program{nullptr, nekomote_program_destroy};
    Handle<nekomote_session, nekomote_session_destroy> session{nullptr, nekomote_session_destroy};
    std::vector<Texture> textures;
    int fixed_steps{0};
    int trace_frames{0};
    bool deterministic{false};
    bool initialized_time{false};
    bool paused{false};
    std::chrono::steady_clock::time_point previous_time;
    std::atomic<unsigned> renderer_created{0};
    std::atomic<unsigned> renderer_destroyed{0};
    std::atomic<unsigned> rendered_frames{0};
};

std::shared_ptr<SessionState> load_session(const std::filesystem::path& model,
                                           const std::filesystem::path& session, std::uint32_t seed,
                                           float origin_x, float origin_y, float scale, int steps,
                                           bool deterministic);

class SessionItem final : public QQuickFramebufferObject
{
    Q_OBJECT
  public:
    explicit SessionItem(std::shared_ptr<SessionState> state, QQuickItem* parent = nullptr);
    Renderer* createRenderer() const override;
    void set_paused(bool paused);
    // Diagnostic GUI callback; goes through the same bounded queue as mouse input.
    void replay_touch(QPointF logical_position);

  signals:
    void frame_ready(QSize framebuffer_size, unsigned generation, double clock_delta,
                     QString reaction);
    void failed(QString diagnostic);

  protected:
    void hoverEnterEvent(QHoverEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

  private:
    friend class SessionRenderer;
    enum class InputKind
    {
        Enter,
        Move,
        Leave,
        Touch
    };
    struct Input
    {
        InputKind kind;
        float x, y;
    };
    void enqueue(InputKind kind, QPointF position = {});
    std::shared_ptr<SessionState> state_;
    bool paused_{false};
    std::uint64_t playback_revision_{0};
    std::array<Input, 32> inputs_{};
    std::size_t input_count_{0};
};
} // namespace nekomote::qt_quick
