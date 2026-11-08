#include <nekomote/ui/menu.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

bool track = false;
std::size_t allocations = 0;
void* operator new(std::size_t n)
{
    if (track)
        ++allocations;
    if (void* p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n)
{
    return ::operator new(n);
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}
class Painter final : public nekomote::ui::Painter
{
  public:
    std::size_t draws = 0;
    void begin_frame() override
    {
    }
    void end_frame() override
    {
    }
    void draw_rounded_rect(float, float, float, float, float, nekomote::ui::Color,
                           nekomote::ui::Color, float) override
    {
        ++draws;
    }
    void draw_text(float, float, const char*, float, nekomote::ui::Color) override
    {
        ++draws;
    }
    void draw_icon(float, float, float, float, std::uint32_t, nekomote::ui::Color) override
    {
        ++draws;
    }
    void draw_circle(float, float, float, nekomote::ui::Color, nekomote::ui::Color, float) override
    {
        ++draws;
    }
    void draw_solid_rect(float, float, float, float, nekomote::ui::Color) override
    {
        ++draws;
    }
    void draw_arrow(float, float, float, bool, nekomote::ui::Color) override
    {
        ++draws;
    }
    void push_clip_rect(float, float, float, float) override
    {
        ++draws;
    }
    void pop_clip_rect() override
    {
        ++draws;
    }
    void set_global_alpha(float) override
    {
    }
    float global_alpha() const override
    {
        return 1;
    }
    int viewport_width() const override
    {
        return 800;
    }
    int viewport_height() const override
    {
        return 1080;
    }
    nekomote::ui::FontMetrics* font() override
    {
        return nullptr;
    }
    bool is_initialized() const override
    {
        return true;
    }
};
int main()
{
    Painter painter;
    nekomote::ui::Context context;
    context.init(&painter);
    context.mouse_move(-100, -100);
    nekomote::application::AssetCatalog catalog;
    catalog.character_groups.resize(1);
    catalog.character_groups[0].items.resize(195);
    for (auto& item : catalog.character_groups[0].items)
        item.fallback_name = "item";
    nekomote::application::WallpaperController controller;
    for (int expanded : {-1, 0})
    {
        nekomote::ui::MenuState menu;
        menu.sidebar.open = true;
        menu.sidebar.animation = 1;
        menu.sidebar.is_free = true;
        menu.sidebar.x = 300;
        menu.sidebar.y = 8;
        menu.sidebar.expanded_drawer = expanded;
        const auto frame = [&]
        {
            context.sidebar(menu.sidebar);
            nekomote::ui::build_sidebar(context, painter, catalog, controller, menu, 1080);
            context.end_frame();
        };
        for (int i = 0; i < 2000; ++i)
            frame();
        std::vector<double> samples;
        samples.reserve(100);
        allocations = 0;
        track = true;
        for (int batch = 0; batch < 100; ++batch)
        {
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < 1000; ++i)
                frame();
            samples.push_back(
                std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start)
                    .count() /
                1000);
        }
        track = false;
        std::sort(samples.begin(), samples.end());
        std::printf("expanded=%d p50_ns=%.1f p95_ns=%.1f p99_ns=%.1f allocations=%zu height=%.1f "
                    "x=%.1f draws=%zu\n",
                    expanded, samples[49], samples[94], samples[98], allocations,
                    menu.sidebar.height, menu.sidebar.x, painter.draws);
    }
}
