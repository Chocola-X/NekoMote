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

namespace nekomote::qt_quick
{
enum class GraphicsBackend
{
    OpenGL,
    Vulkan
};
template <typename T, void (*Destroy)(T*)>
using Handle = std::unique_ptr<T, decltype(Destroy)>;

// Shared ownership spans the GUI item and its scene-graph renderer. The
// wallpaper handle is created lazily on the render thread when a GL context
// is available; config is buffered from the GUI thread and applied at that
// point. Thereafter only the render thread evaluates the wallpaper instance.
struct WallpaperState
{
    Handle<nekomote_wallpaper, nekomote_wallpaper_destroy> wallpaper{nullptr,
                                                                     nekomote_wallpaper_destroy};
    // Buffered config applied when the wallpaper handle is created.
    std::filesystem::path assets_dir;
    std::filesystem::path model_path;
    std::filesystem::path session_path;
    std::uint32_t seed{0};
    bool no_save{false};
    bool assets_mode{true};
    bool config_dirty{true};
    GraphicsBackend backend{GraphicsBackend::OpenGL};
    bool loaded{false};
    // Diagnostics.
    int fixed_steps{0};
    int trace_frames{0};
    bool deterministic{false};
    bool initialized_time{false};
    bool paused{false};
    std::chrono::steady_clock::time_point previous_time;
    std::atomic<unsigned> renderer_created{0};
    std::atomic<unsigned> renderer_destroyed{0};
    std::atomic<unsigned> rendered_frames{0};
    std::atomic<unsigned> fixed_steps_executed{0};
    std::atomic<int> fps_limit{60};
};

enum class InputKind
{
    Enter,
    Move,
    Leave,
    Touch,
    Press,
    Release,
    Scroll
};
struct Input
{
    InputKind kind;
    float x, y;
    float scroll{0};
    int button{0}; // ABI/GLFW convention: left=0, right=1.
    std::uint64_t timestamp_ms{0};
};

// Snapshot of GUI-side state copied into the renderer during synchronize.
// Inputs are bounded to 32 entries; exceeding capacity reports an error.
struct ItemState
{
    bool paused{false};
    std::uint64_t playback_revision{0};
    std::array<Input, 32> inputs{};
    std::size_t input_count{0};
    bool notify_frames{false};
};

// Abstract base for FBO items sharing the wallpaper renderer. Concrete items
// provide synchronize state and clear consumed inputs after sync.
class WallpaperItemBase : public QQuickFramebufferObject
{
    Q_OBJECT
  public:
    explicit WallpaperItemBase(QQuickItem* parent = nullptr) : QQuickFramebufferObject(parent)
    {
    }
    ~WallpaperItemBase() override = default;
    virtual ItemState get_item_state() const = 0;
    virtual void clear_inputs() = 0;
    virtual std::shared_ptr<WallpaperState> wallpaper_state() const = 0;
  signals:
    void frame_ready(QSize framebuffer_size, unsigned generation, double clock_delta,
                     QString reaction);
    void failed(QString diagnostic);
};

// Factory used by WallpaperItemBase subclasses' createRenderer().
QQuickFramebufferObject::Renderer* create_wallpaper_renderer(std::shared_ptr<WallpaperState> state);

class WallpaperItem final : public WallpaperItemBase
{
    Q_OBJECT
  public:
    explicit WallpaperItem(std::shared_ptr<WallpaperState> state, QQuickItem* parent = nullptr);
    Renderer* createRenderer() const override;
    void set_paused(bool paused);
    void replay_touch(QPointF logical_position);

  protected:
    void hoverEnterEvent(QHoverEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

  private:
    ItemState get_item_state() const override;
    void clear_inputs() override;
    std::shared_ptr<WallpaperState> wallpaper_state() const override;
    void enqueue(InputKind kind, QPointF position = {}, float scroll = 0, int button = 0,
                 std::uint64_t timestamp_ms = 0);
    std::shared_ptr<WallpaperState> state_;
    bool paused_{false};
    std::uint64_t playback_revision_{0};
    std::array<Input, 32> inputs_{};
    std::size_t input_count_{0};
};
} // namespace nekomote::qt_quick
