#pragma once

struct wl_display;
struct wl_global;
struct wl_resource;

// Private, single-client server. Only the supplied socketpair fd is ever used.
class GazeCompositor
{
  public:
    explicit GazeCompositor(int fd);
    ~GazeCompositor();
    GazeCompositor(const GazeCompositor&) = delete;
    GazeCompositor& operator=(const GazeCompositor&) = delete;
    void dispatch();

    struct Output
    {
        int x, y, width, height; // logical origin and physical mode (logical size is 1000x600)
        wl_global* global = nullptr;
        wl_resource* cursor = nullptr;
        void enter() const;
        void position(int px, int py) const;
        void leave() const;
    };
    Output left{-1000, -100, 1600, 960}, right{0, 0, 1800, 1080};
    void advertise(Output& output);
    void unplug(Output& output);

  private:
    wl_display* display;
};
