#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/ui/layout.hpp>

#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace nekomote::ui;

namespace
{

// CPU-only mock painter. Records draw calls for verification; no GL.
class MockPainter : public Painter
{
  public:
    int vw{800}, vh{1080};
    std::vector<std::string> draw_log;

    void begin_frame() override
    {
    }
    void end_frame() override
    {
    }
    void draw_rounded_rect(float, float, float, float, float, Color, Color, float) override
    {
    }
    void draw_text(float, float, const char*, float, Color) override
    {
    }
    void draw_icon(float, float, float, float, std::uint32_t, Color) override
    {
    }
    void draw_circle(float, float, float, Color, Color, float) override
    {
    }
    void draw_solid_rect(float, float, float, float, Color) override
    {
    }
    void draw_arrow(float, float, float, bool, Color) override
    {
    }
    void push_clip_rect(float, float, float, float) override
    {
    }
    void pop_clip_rect() override
    {
    }
    void set_global_alpha(float) override
    {
    }
    float global_alpha() const override
    {
        return 1.0f;
    }
    int viewport_width() const override
    {
        return vw;
    }
    int viewport_height() const override
    {
        return vh;
    }
    FontMetrics* font() override
    {
        return nullptr;
    }
    bool is_initialized() const override
    {
        return true;
    }
};

} // namespace

TEST_CASE("UI Sidebar docked animation open and close timing", "[ui_layout]")
{
    MockPainter painter;
    Context ctx;
    ctx.init(&painter);

    SidebarState sidebar{};
    REQUIRE(sidebar.open == false);
    REQUIRE(sidebar.animation == 0.0f);
    REQUIRE(sidebar.is_free == false);

    ctx.sidebar(sidebar, 0.016f);
    REQUIRE(sidebar.animation == 0.0f);
    REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(8.0f - (sidebar.width + 16.0f), 0.01f));
    REQUIRE(sidebar.y == 8.0f);

    sidebar.open = true;
    float total_open_time = 0.0f;
    const float dt = 0.016f;
    while (sidebar.animation < 1.0f && total_open_time < 1.0f)
    {
        ctx.sidebar(sidebar, dt);
        total_open_time += dt;
    }

    REQUIRE_THAT(sidebar.animation, Catch::Matchers::WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(total_open_time, Catch::Matchers::WithinAbs(0.28f, 0.04f));
    REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(8.0f, 0.01f));
    REQUIRE_THAT(sidebar.y, Catch::Matchers::WithinAbs(8.0f, 0.01f));

    sidebar.open = false;
    float total_close_time = 0.0f;
    while (sidebar.animation > 0.0f && total_close_time < 1.0f)
    {
        ctx.sidebar(sidebar, dt);
        total_close_time += dt;
    }

    REQUIRE(sidebar.animation == 0.0f);
    REQUIRE_THAT(total_close_time, Catch::Matchers::WithinAbs(0.24f, 0.04f));
    REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(8.0f - (sidebar.width + 16.0f), 0.01f));
}

TEST_CASE("UI Sidebar dragging coordinates and synchronous title position", "[ui_layout]")
{
    MockPainter painter;
    Context ctx;
    ctx.init(&painter);

    SidebarState sidebar{};
    sidebar.open = true;
    sidebar.animation = 1.0f;
    sidebar.x = 8.0f;
    sidebar.y = 8.0f;
    sidebar.height = 600.0f;

    ctx.mouse_move(100.0, 24.0);
    ctx.mouse_button(0, 1);

    ctx.sidebar(sidebar, 0.016f);
    REQUIRE(sidebar.dragging == true);
    REQUIRE(sidebar.is_free == true);

    const float expected_drag_offset_x = 100.0f - 8.0f;
    const float expected_drag_offset_y = 24.0f - 8.0f;
    REQUIRE_THAT(sidebar.drag_offset_x, Catch::Matchers::WithinAbs(expected_drag_offset_x, 0.01f));
    REQUIRE_THAT(sidebar.drag_offset_y, Catch::Matchers::WithinAbs(expected_drag_offset_y, 0.01f));

    ctx.mouse_move(150.0, 54.0);
    ctx.sidebar(sidebar, 0.016f);

    REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(150.0f - expected_drag_offset_x, 0.01f));
    REQUIRE_THAT(sidebar.y, Catch::Matchers::WithinAbs(54.0f - expected_drag_offset_y, 0.01f));
    REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(58.0f, 0.01f));
    REQUIRE_THAT(sidebar.y, Catch::Matchers::WithinAbs(38.0f, 0.01f));
    REQUIRE(sidebar.moved);

    const float title_x = sidebar.x + 16.0f;
    const float title_y = sidebar.y + 12.0f;
    REQUIRE_THAT(title_x, Catch::Matchers::WithinAbs(74.0f, 0.01f));
    REQUIRE_THAT(title_y, Catch::Matchers::WithinAbs(50.0f, 0.01f));

    ctx.mouse_button(0, 0);
    ctx.sidebar(sidebar, 0.016f);
    REQUIRE(sidebar.dragging == false);
    REQUIRE(sidebar.is_free == true);
    REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(58.0f, 0.01f));
    REQUIRE_THAT(sidebar.y, Catch::Matchers::WithinAbs(38.0f, 0.01f));
    REQUIRE(sidebar.moved);
}

TEST_CASE("UI Sidebar dragging viewport bounds clamping", "[ui_layout]")
{
    MockPainter painter;
    Context ctx;
    ctx.init(&painter);

    SidebarState sidebar{};
    sidebar.open = true;
    sidebar.animation = 1.0f;
    sidebar.x = 8.0f;
    sidebar.y = 8.0f;
    sidebar.height = 500.0f;

    ctx.mouse_move(50.0, 20.0);
    ctx.mouse_button(0, 1);
    ctx.sidebar(sidebar, 0.016f);
    REQUIRE(sidebar.dragging == true);

    ctx.mouse_move(-200.0, -100.0);
    ctx.sidebar(sidebar, 0.016f);
    REQUIRE(sidebar.x == 8.0f);
    REQUIRE(sidebar.y == 8.0f);

    ctx.mouse_move(2000.0, 2000.0);
    ctx.sidebar(sidebar, 0.016f);
    const float max_x = 800.0f - sidebar.width - 8.0f;
    const float max_y = 1080.0f - sidebar.height - 8.0f;
    REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(max_x, 0.01f));
    REQUIRE_THAT(sidebar.y, Catch::Matchers::WithinAbs(max_y, 0.01f));

    ctx.mouse_button(0, 0);
    ctx.sidebar(sidebar, 0.016f);
    REQUIRE(sidebar.dragging == false);
}

TEST_CASE("UI Sidebar free fade-in / fade-out", "[ui_layout]")
{
    MockPainter painter;
    Context ctx;
    ctx.init(&painter);

    SidebarState sidebar{};
    sidebar.open = true;
    sidebar.animation = 1.0f;
    sidebar.is_free = true;
    sidebar.x = 300.0f;
    sidebar.y = 150.0f;
    sidebar.height = 400.0f;

    sidebar.open = false;
    const float dt = 0.016f;
    float elapsed = 0.0f;
    while (sidebar.animation > 0.0f && elapsed < 1.0f)
    {
        ctx.sidebar(sidebar, dt);
        elapsed += dt;
        REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(300.0f, 0.001f));
        REQUIRE_THAT(sidebar.y, Catch::Matchers::WithinAbs(150.0f, 0.001f));
    }

    REQUIRE(sidebar.animation == 0.0f);
    REQUIRE_THAT(elapsed, Catch::Matchers::WithinAbs(0.24f, 0.04f));
    REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(300.0f, 0.001f));
    REQUIRE_THAT(sidebar.y, Catch::Matchers::WithinAbs(150.0f, 0.001f));

    sidebar.open = true;
    elapsed = 0.0f;
    while (sidebar.animation < 1.0f && elapsed < 1.0f)
    {
        ctx.sidebar(sidebar, dt);
        elapsed += dt;
        REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(300.0f, 0.001f));
        REQUIRE_THAT(sidebar.y, Catch::Matchers::WithinAbs(150.0f, 0.001f));
    }

    REQUIRE_THAT(sidebar.animation, Catch::Matchers::WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(elapsed, Catch::Matchers::WithinAbs(0.28f, 0.04f));
}

TEST_CASE("UI Sidebar mid-animation reversal", "[ui_layout]")
{
    MockPainter painter;
    Context ctx;
    ctx.init(&painter);

    SidebarState sidebar{};
    sidebar.open = true;

    for (int i = 0; i < 5; ++i)
        ctx.sidebar(sidebar, 0.016f);
    const float mid_anim = sidebar.animation;
    REQUIRE(mid_anim > 0.1f);
    REQUIRE(mid_anim < 0.8f);

    sidebar.open = false;
    ctx.sidebar(sidebar, 0.016f);
    REQUIRE(sidebar.animation < mid_anim);

    while (sidebar.animation > 0.0f)
        ctx.sidebar(sidebar, 0.016f);
    REQUIRE(sidebar.animation == 0.0f);
}

TEST_CASE("UI Input capture state machine", "[ui_layout]")
{
    MockPainter painter;
    Context ctx;
    ctx.init(&painter);

    IconState icon{};
    icon.x = 748.0f;
    icon.y = 8.0f;
    icon.size = 44.0f;
    icon.initialized = true;

    SidebarState sidebar{};
    sidebar.x = 8.0f;
    sidebar.y = 8.0f;
    sidebar.width = 264.0f;
    sidebar.height = 500.0f;
    sidebar.open = true;
    sidebar.animation = 1.0f;

    ctx.mouse_move(500.0, 500.0);
    REQUIRE(ctx.eval_captures_mouse(icon, sidebar) == false);

    ctx.mouse_move(750.0, 15.0);
    REQUIRE(ctx.eval_captures_mouse(icon, sidebar) == true);

    ctx.mouse_move(100.0, 200.0);
    REQUIRE(ctx.eval_captures_mouse(icon, sidebar) == true);

    sidebar.animation = 0.0f;
    sidebar.open = false;
    ctx.mouse_move(100.0, 200.0);
    REQUIRE(ctx.eval_captures_mouse(icon, sidebar) == false);

    icon.dragging = true;
    ctx.mouse_move(500.0, 500.0);
    REQUIRE(ctx.eval_captures_mouse(icon, sidebar) == true);
    icon.dragging = false;

    sidebar.dragging = true;
    REQUIRE(ctx.eval_captures_mouse(icon, sidebar) == true);
    sidebar.dragging = false;
}

TEST_CASE("UI Icon toggle vs drag distinction", "[ui_layout]")
{
    MockPainter painter;
    Context ctx;
    ctx.init(&painter);

    IconState icon{};
    icon.x = 748.0f;
    icon.y = 8.0f;
    icon.size = 44.0f;
    icon.initialized = true;

    ctx.mouse_move(750.0, 20.0);
    ctx.mouse_button(0, 1);
    ctx.toggle_icon(icon, 1);
    ctx.end_frame();

    ctx.mouse_button(0, 0);
    bool clicked = ctx.toggle_icon(icon, 1);
    ctx.end_frame();
    REQUIRE(clicked == true);
    REQUIRE(icon.dragging == false);

    ctx.mouse_button(0, 1);
    ctx.toggle_icon(icon, 1);
    ctx.end_frame();

    ctx.mouse_move(700.0, 60.0);
    ctx.toggle_icon(icon, 1);
    REQUIRE(icon.dragging == true);
    REQUIRE(icon.moved == true);
    ctx.end_frame();

    ctx.mouse_button(0, 0);
    clicked = ctx.toggle_icon(icon, 1);
    ctx.end_frame();
    REQUIRE(clicked == false);
    REQUIRE(icon.dragging == false);
}

TEST_CASE("UI Viewport bounds safety reset for free sidebar", "[ui_layout]")
{
    MockPainter painter;
    Context ctx;
    ctx.init(&painter);

    SidebarState sidebar{};
    sidebar.open = false;
    sidebar.animation = 0.0f;
    sidebar.is_free = true;
    sidebar.x = 900.0f;
    sidebar.y = 1200.0f;

    sidebar.open = true;
    ctx.sidebar(sidebar, 0.016f);

    REQUIRE(sidebar.is_free == false);
    REQUIRE(sidebar.x <= 8.0f);
    REQUIRE(sidebar.y == 8.0f);
}

TEST_CASE("UI mouse_in_rect hit testing", "[ui_layout]")
{
    MockPainter painter;
    Context ctx;
    ctx.init(&painter);

    ctx.mouse_move(100.0, 200.0);
    REQUIRE(ctx.mouse_in_rect(50, 150, 100, 100) == true);
    REQUIRE(ctx.mouse_in_rect(50, 150, 49, 49) == false);
    REQUIRE(ctx.mouse_in_rect(101, 201, 100, 100) == false);
    REQUIRE(ctx.mouse_in_rect(0, 0, 200, 300) == true);
}

TEST_CASE("font atlas growth preserves glyph locations and final UVs", "[ui][font][integration]")
{
    FontMetrics metrics;
    if (!metrics.init("", 256.0f))
        SKIP("No system font available for CPU atlas growth integration");
    std::map<unsigned int, Glyph> glyphs;
    int growth_count = 0;
    metrics.on_atlas_resized = [&](int, int) { ++growth_count; };
    metrics.on_glyph_added = [&](const Glyph& glyph, const unsigned char*)
    {
        const auto existing = glyphs.find(glyph.codepoint);
        if (existing != glyphs.end())
        {
            REQUIRE(glyph.atlas_x == existing->second.atlas_x);
            REQUIRE(glyph.atlas_y == existing->second.atlas_y);
        }
        glyphs[glyph.codepoint] = glyph;
    };
    // Force growth during one draw after its first already-known glyph. A late
    // ensure_char must not invalidate the first quad's texture coordinates.
    metrics.ensure_char('A');
    std::string text = "A";
    for (unsigned int c = 33; c < 127; ++c)
        text.push_back(static_cast<char>(c));
    std::vector<float> vertices, uvs;
    metrics.draw_text(vertices, uvs, 0, 0, text.c_str(), 16.0f);
    REQUIRE(growth_count > 0);
    REQUIRE(uvs.size() == text.size() * 12);
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const auto& glyph = glyphs.at(static_cast<unsigned char>(text[i]));
        REQUIRE_THAT(uvs[i * 12],
                     Catch::Matchers::WithinAbs(static_cast<float>(glyph.atlas_x) /
                                                    static_cast<float>(metrics.texture_width()),
                                                0.000001));
        REQUIRE_THAT(uvs[i * 12 + 1],
                     Catch::Matchers::WithinAbs(static_cast<float>(glyph.atlas_y) /
                                                    static_cast<float>(metrics.texture_height()),
                                                0.000001));
    }
    // All rectangles must remain disjoint after growth and insertion. Restarting
    // atlas packing at zero silently aliases old and newly added glyphs.
    for (auto a = glyphs.begin(); a != glyphs.end(); ++a)
        for (auto b = std::next(a); b != glyphs.end(); ++b)
        {
            const auto& first = a->second;
            const auto& second = b->second;
            const bool separated = first.atlas_x + first.atlas_w <= second.atlas_x ||
                                   second.atlas_x + second.atlas_w <= first.atlas_x ||
                                   first.atlas_y + first.atlas_h <= second.atlas_y ||
                                   second.atlas_y + second.atlas_h <= first.atlas_y;
            REQUIRE(separated);
        }
}

TEST_CASE("Saved UI coordinates outside new viewport return to defaults", "[ui_layout][config]")
{
    MockPainter painter;
    painter.vw = 640;
    painter.vh = 480;
    Context ctx;
    ctx.init(&painter);
    IconState icon;
    icon.initialized = true;
    SidebarState sidebar;
    sidebar.is_free = true;
    sidebar.height = 300;
    sidebar.open = true;
    sidebar.animation = 1;
    SECTION("right edge")
    {
        icon.x = 620;
        sidebar.x = 500;
    }
    SECTION("bottom edge")
    {
        icon.y = 470;
        sidebar.y = 400;
    }
    SECTION("negative coordinates")
    {
        icon.x = -10;
        sidebar.y = -500;
    }
    SECTION("nonfinite coordinates")
    {
        icon.y = std::numeric_limits<float>::quiet_NaN();
        sidebar.x = std::numeric_limits<float>::infinity();
    }
    ctx.toggle_icon(icon, 0);
    ctx.sidebar(sidebar, .016f);
    REQUIRE(icon.x == 588.0f);
    REQUIRE(icon.y == 8.0f);
    REQUIRE(icon.position_reset);
    REQUIRE_FALSE(sidebar.is_free);
    REQUIRE(sidebar.x == 8.0f);
    REQUIRE(sidebar.y == 8.0f);
    REQUIRE(sidebar.position_reset);
}

TEST_CASE("Valid saved UI coordinates survive reopen and viewport resize", "[ui_layout][config]")
{
    MockPainter painter;
    Context ctx;
    ctx.init(&painter);
    IconState icon;
    icon.initialized = true;
    icon.x = 320;
    icon.y = 100;
    SidebarState sidebar;
    sidebar.is_free = true;
    sidebar.x = 100;
    sidebar.y = 120;
    sidebar.height = 300;
    ctx.toggle_icon(icon, 0);
    ctx.sidebar(sidebar, .016f);
    REQUIRE(sidebar.is_free);
    painter.vw = 640;
    painter.vh = 480;
    ctx.toggle_icon(icon, 0);
    ctx.sidebar(sidebar, .016f);
    REQUIRE(icon.x == 320.0f);
    REQUIRE(icon.y == 100.0f);
    REQUIRE(sidebar.x == 100.0f);
    REQUIRE(sidebar.y == 120.0f);
    REQUIRE(sidebar.is_free);
    REQUIRE_FALSE(icon.position_reset);
    REQUIRE_FALSE(sidebar.position_reset);
}
