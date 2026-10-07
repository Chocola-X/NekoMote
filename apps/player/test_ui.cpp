#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ui.hpp"

using namespace nekomote::player::ui;

TEST_CASE("UI Sidebar docked animation open and close timing", "[player_ui]")
{
    Renderer renderer;
    renderer.resize(800, 1080);
    Context ctx;
    ctx.init(&renderer);

    SidebarState sidebar{};
    REQUIRE(sidebar.open == false);
    REQUIRE(sidebar.animation == 0.0f);
    REQUIRE(sidebar.is_free == false);

    // Initial docked position before open
    ctx.sidebar(sidebar, 0.016f);
    REQUIRE(sidebar.animation == 0.0f);
    REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(8.0f - (sidebar.width + 16.0f), 0.01f));
    REQUIRE(sidebar.y == 8.0f);

    // Trigger open
    sidebar.open = true;
    float total_open_time = 0.0f;
    const float dt = 0.016f;
    while (sidebar.animation < 1.0f && total_open_time < 1.0f)
    {
        ctx.sidebar(sidebar, dt);
        total_open_time += dt;
    }

    // Should reach 1.0f in approximately 0.28s (allow +/- 0.04s)
    REQUIRE_THAT(sidebar.animation, Catch::Matchers::WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(total_open_time, Catch::Matchers::WithinAbs(0.28f, 0.04f));
    // When fully open and docked, x should be exactly 8.0f
    REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(8.0f, 0.01f));
    REQUIRE_THAT(sidebar.y, Catch::Matchers::WithinAbs(8.0f, 0.01f));

    // Trigger close
    sidebar.open = false;
    float total_close_time = 0.0f;
    while (sidebar.animation > 0.0f && total_close_time < 1.0f)
    {
        ctx.sidebar(sidebar, dt);
        total_close_time += dt;
    }

    // Should reach 0.0f in approximately 0.24s (no infinite asymptotic tail)
    REQUIRE(sidebar.animation == 0.0f);
    REQUIRE_THAT(total_close_time, Catch::Matchers::WithinAbs(0.24f, 0.04f));
    // When fully closed and docked, x slides off-screen to the left
    REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(8.0f - (sidebar.width + 16.0f), 0.01f));
}

TEST_CASE("UI Sidebar dragging coordinates and synchronous title position", "[player_ui]")
{
    Renderer renderer;
    renderer.resize(800, 1080);
    Context ctx;
    ctx.init(&renderer);

    SidebarState sidebar{};
    sidebar.open = true;
    sidebar.animation = 1.0f;
    sidebar.x = 8.0f;
    sidebar.y = 8.0f;
    sidebar.height = 600.0f;

    // Simulate mouse hover over header (y in [8, 8 + 44]) and press
    ctx.mouse_move(100.0, 24.0);
    ctx.mouse_button(0, 1); // Left press

    // Advance 1 frame of sidebar logic
    ctx.sidebar(sidebar, 0.016f);
    REQUIRE(sidebar.dragging == true);
    REQUIRE(sidebar.is_free == true);

    // Initial drag offsets
    const float expected_drag_offset_x = 100.0f - 8.0f;
    const float expected_drag_offset_y = 24.0f - 8.0f;
    REQUIRE_THAT(sidebar.drag_offset_x, Catch::Matchers::WithinAbs(expected_drag_offset_x, 0.01f));
    REQUIRE_THAT(sidebar.drag_offset_y, Catch::Matchers::WithinAbs(expected_drag_offset_y, 0.01f));

    // Move mouse by (+50, +30)
    ctx.mouse_move(150.0, 54.0);
    ctx.sidebar(sidebar, 0.016f);

    // Sidebar position must immediately equal (mouse - drag_offset) in the same frame
    REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(150.0f - expected_drag_offset_x, 0.01f));
    REQUIRE_THAT(sidebar.y, Catch::Matchers::WithinAbs(54.0f - expected_drag_offset_y, 0.01f));
    REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(58.0f, 0.01f));
    REQUIRE_THAT(sidebar.y, Catch::Matchers::WithinAbs(38.0f, 0.01f));

    // Title position is sidebar.x + 16.0f, sidebar.y + 12.0f; verify strict synchronicity
    const float title_x = sidebar.x + 16.0f;
    const float title_y = sidebar.y + 12.0f;
    REQUIRE_THAT(title_x, Catch::Matchers::WithinAbs(74.0f, 0.01f));
    REQUIRE_THAT(title_y, Catch::Matchers::WithinAbs(50.0f, 0.01f));

    // Release mouse
    ctx.mouse_button(0, 0); // Release
    ctx.sidebar(sidebar, 0.016f);
    REQUIRE(sidebar.dragging == false);
    REQUIRE(sidebar.is_free == true);
    // Post-release position remains exactly where released
    REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(58.0f, 0.01f));
    REQUIRE_THAT(sidebar.y, Catch::Matchers::WithinAbs(38.0f, 0.01f));
}

TEST_CASE("UI Sidebar dragging viewport bounds clamping", "[player_ui]")
{
    Renderer renderer;
    renderer.resize(800, 1080);
    Context ctx;
    ctx.init(&renderer);

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

    // Drag far left and above screen
    ctx.mouse_move(-200.0, -100.0);
    ctx.sidebar(sidebar, 0.016f);
    REQUIRE(sidebar.x == 8.0f);
    REQUIRE(sidebar.y == 8.0f);

    // Drag far right and below screen
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

TEST_CASE("UI Sidebar free fade-in / fade-out (in-place opacity transition)", "[player_ui]")
{
    Renderer renderer;
    renderer.resize(800, 1080);
    Context ctx;
    ctx.init(&renderer);

    SidebarState sidebar{};
    sidebar.open = true;
    sidebar.animation = 1.0f;
    sidebar.is_free = true;
    sidebar.x = 300.0f;
    sidebar.y = 150.0f;
    sidebar.height = 400.0f;

    // Close the sidebar while in free dragged mode
    sidebar.open = false;
    const float dt = 0.016f;
    float elapsed = 0.0f;
    while (sidebar.animation > 0.0f && elapsed < 1.0f)
    {
        ctx.sidebar(sidebar, dt);
        elapsed += dt;
        // In free mode, x and y must NEVER slide off-screen; they stay locked in place
        REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(300.0f, 0.001f));
        REQUIRE_THAT(sidebar.y, Catch::Matchers::WithinAbs(150.0f, 0.001f));
    }

    REQUIRE(sidebar.animation == 0.0f);
    REQUIRE_THAT(elapsed, Catch::Matchers::WithinAbs(0.24f, 0.04f));
    // Final coordinates stay at free position
    REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(300.0f, 0.001f));
    REQUIRE_THAT(sidebar.y, Catch::Matchers::WithinAbs(150.0f, 0.001f));

    // Reopen the sidebar while in free dragged mode
    sidebar.open = true;
    elapsed = 0.0f;
    while (sidebar.animation < 1.0f && elapsed < 1.0f)
    {
        ctx.sidebar(sidebar, dt);
        elapsed += dt;
        // Coordinates remain stationary, fading in smoothly
        REQUIRE_THAT(sidebar.x, Catch::Matchers::WithinAbs(300.0f, 0.001f));
        REQUIRE_THAT(sidebar.y, Catch::Matchers::WithinAbs(150.0f, 0.001f));
    }

    REQUIRE_THAT(sidebar.animation, Catch::Matchers::WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(elapsed, Catch::Matchers::WithinAbs(0.28f, 0.04f));
}

TEST_CASE("UI Sidebar mid-animation reversal", "[player_ui]")
{
    Renderer renderer;
    renderer.resize(800, 1080);
    Context ctx;
    ctx.init(&renderer);

    SidebarState sidebar{};
    sidebar.open = true;

    // Advance 5 frames (approx 0.08s, partial open)
    for (int i = 0; i < 5; ++i)
        ctx.sidebar(sidebar, 0.016f);
    const float mid_anim = sidebar.animation;
    REQUIRE(mid_anim > 0.1f);
    REQUIRE(mid_anim < 0.8f);

    // Reverse direction mid-animation to close
    sidebar.open = false;
    ctx.sidebar(sidebar, 0.016f);
    REQUIRE(sidebar.animation < mid_anim); // Immediately starts decreasing

    while (sidebar.animation > 0.0f)
        ctx.sidebar(sidebar, 0.016f);
    REQUIRE(sidebar.animation == 0.0f);
}

TEST_CASE("UI Input capture state machine (eval_captures_mouse)", "[player_ui]")
{
    Renderer renderer;
    renderer.resize(800, 1080);
    Context ctx;
    ctx.init(&renderer);

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

    // 1. Mouse in empty area: does NOT capture
    ctx.mouse_move(500.0, 500.0);
    REQUIRE(ctx.eval_captures_mouse(icon, sidebar) == false);

    // 2. Mouse over cat-paw icon: captures
    ctx.mouse_move(750.0, 15.0);
    REQUIRE(ctx.eval_captures_mouse(icon, sidebar) == true);

    // 3. Mouse over sidebar: captures
    ctx.mouse_move(100.0, 200.0);
    REQUIRE(ctx.eval_captures_mouse(icon, sidebar) == true);

    // 4. Mouse over closed sidebar area: does NOT capture
    sidebar.animation = 0.0f;
    sidebar.open = false;
    ctx.mouse_move(100.0, 200.0);
    REQUIRE(ctx.eval_captures_mouse(icon, sidebar) == false);

    // 5. Dragging icon captures even if mouse moves outside icon rect
    icon.dragging = true;
    ctx.mouse_move(500.0, 500.0);
    REQUIRE(ctx.eval_captures_mouse(icon, sidebar) == true);
    icon.dragging = false;

    // 6. Dragging sidebar captures even if mouse temporarily leaves rect
    sidebar.dragging = true;
    REQUIRE(ctx.eval_captures_mouse(icon, sidebar) == true);
    sidebar.dragging = false;
}

TEST_CASE("UI Icon toggle vs drag distinction", "[player_ui]")
{
    Renderer renderer;
    renderer.resize(800, 1080);
    Context ctx;
    ctx.init(&renderer);

    IconState icon{};
    icon.x = 748.0f;
    icon.y = 8.0f;
    icon.size = 44.0f;
    icon.initialized = true;

    // Click without move (< 3px): triggers toggle (clicked == true)
    ctx.mouse_move(750.0, 20.0);
    ctx.mouse_button(0, 1);
    ctx.toggle_icon(icon, 1); // Begin press
    ctx.end_frame();

    ctx.mouse_button(0, 0); // Release
    bool clicked = ctx.toggle_icon(icon, 1);
    ctx.end_frame();
    REQUIRE(clicked == true);
    REQUIRE(icon.dragging == false);

    // Drag with move (> 3px): does NOT trigger toggle
    ctx.mouse_button(0, 1);
    ctx.toggle_icon(icon, 1);
    ctx.end_frame();

    ctx.mouse_move(700.0, 60.0); // Move > 3px
    ctx.toggle_icon(icon, 1);
    REQUIRE(icon.dragging == true);
    REQUIRE(icon.moved == true);
    ctx.end_frame();

    ctx.mouse_button(0, 0); // Release after drag
    clicked = ctx.toggle_icon(icon, 1);
    ctx.end_frame();
    REQUIRE(clicked == false); // Dragged, must NOT trigger toggle
    REQUIRE(icon.dragging == false);
}

TEST_CASE("UI Viewport bounds safety reset for free sidebar", "[player_ui]")
{
    Renderer renderer;
    renderer.resize(800, 1080);
    Context ctx;
    ctx.init(&renderer);

    SidebarState sidebar{};
    sidebar.open = false;
    sidebar.animation = 0.0f;
    sidebar.is_free = true;
    sidebar.x = 900.0f; // Was dragged off-screen or window was resized smaller
    sidebar.y = 1200.0f;

    // Reopen sidebar: should detect out-of-bounds and reset to docked initial
    sidebar.open = true;
    ctx.sidebar(sidebar, 0.016f);

    REQUIRE(sidebar.is_free == false);
    REQUIRE(sidebar.x <= 8.0f);
    REQUIRE(sidebar.y == 8.0f);
}
