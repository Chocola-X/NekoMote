#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

struct wl_registry;
struct wl_output;
struct wl_pointer;

namespace nekomote::layer_shell
{
namespace detail
{
struct GazeLayout
{
    int x = 0, y = 0, width = 0, height = 0;
    bool positioned = false;
    bool ready() const
    {
        return positioned && width > 0 && height > 0;
    }
};

// Position is in already-transformed pixels; only the extent swaps on quarter turns.
struct GazeSourcePoint
{
    int x, y, mode_width, mode_height;
    bool quarter_turn;
    int width() const
    {
        return quarter_turn ? mode_height : mode_width;
    }
    int height() const
    {
        return quarter_turn ? mode_width : mode_height;
    }
    bool inside() const
    {
        return x >= 0 && x < width() && y >= 0 && y < height();
    }

    std::pair<double, double> relative_to(const GazeLayout& source, const GazeLayout& target,
                                          int buffer_scale) const
    {
        const auto axis = [buffer_scale](int pos, int pixels, int logical, int origin, int dest)
        {
            return ((static_cast<double>(origin) - dest) +
                    static_cast<double>(pos) * logical / pixels) *
                   buffer_scale;
        };
        return {axis(x, width(), source.width, source.x, target.x),
                axis(y, height(), source.height, source.y, target.y)};
    }
};
} // namespace detail

// Single Wayland dispatch thread. Only forward events here in desktop-gaze mode.
// Outputs and pointer are borrowed: remove/clear them before the host releases them.
class DesktopGaze
{
  public:
    DesktopGaze();
    ~DesktopGaze();
    DesktopGaze(const DesktopGaze&) = delete;
    DesktopGaze& operator=(const DesktopGaze&) = delete;

    void global(wl_registry*, std::uint32_t name, const char* interface, std::uint32_t version);
    void remove_global(std::uint32_t name);
    void add_output(std::uint32_t name, wl_output*);
    void remove_output(std::uint32_t name);
    void set_output_mode(wl_output*, int width, int height); // CURRENT only
    void set_output_transform(wl_output*, int transform);
    void output_done(wl_output*);
    void set_pointer(wl_pointer*);
    [[nodiscard]] bool protocols_available() const;
    [[nodiscard]] bool available() const; // protocols + pointer, not necessarily a sample
    [[nodiscard]] std::optional<std::pair<double, double>> sample(wl_output* target,
                                                                  int target_buffer_scale) const;
    void reset(); // before wl_display_disconnect; idempotent

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nekomote::layer_shell
