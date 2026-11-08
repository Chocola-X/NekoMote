#include <catch2/catch_test_macros.hpp>
#include <nekomote/ui/menu.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace nekomote::ui;
using nekomote::application::Language;

namespace
{
namespace fs = std::filesystem;

// Records text draw calls so menu rendering can be verified per language.
class RecordingPainter : public Painter
{
  public:
    int vw{800}, vh{1080};
    struct TextCall
    {
        float x;
        float y;
        std::string text;
    };
    std::vector<TextCall> texts;
    struct Rect
    {
        float x;
        float y;
        float w;
        float h;
    };
    std::vector<Rect> rects;
    std::vector<Rect> clips;

    void begin_frame() override
    {
    }
    void end_frame() override
    {
    }
    void draw_rounded_rect(float x, float y, float w, float h, float, Color, Color, float) override
    {
        rects.push_back({x, y, w, h});
    }
    void draw_text(float x, float y, const char* text, float, Color) override
    {
        texts.push_back({x, y, text});
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
    void push_clip_rect(float x, float y, float w, float h) override
    {
        clips.push_back({x, y, w, h});
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

    bool contains(const std::string& text) const
    {
        return std::any_of(texts.begin(), texts.end(),
                           [&](const TextCall& call) { return call.text == text; });
    }

    std::size_t index_of(const std::string& text) const
    {
        for (std::size_t i = 0; i < texts.size(); ++i)
            if (texts[i].text == text)
                return i;
        return std::string::npos;
    }

    const TextCall* find(const std::string& text) const
    {
        for (const auto& call : texts)
            if (call.text == text)
                return &call;
        return nullptr;
    }
};

struct MenuFixture
{
    fs::path root;
    nekomote::application::WallpaperController controller;
    Context ctx;
    RecordingPainter painter;
    MenuState menu;

    MenuFixture()
    {
        root = fs::temp_directory_path() /
               ("nekomote-menu-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root / "characters" / "chocola" / "casual");
        fs::create_directories(root / "backgrounds");
        std::ofstream(root / "characters" / "chocola" / "casual" / "model.nkm").put('x');
        std::ofstream(root / "characters" / "chocola" / "casual" / "session.nks.json").put('x');
        {
            std::ofstream f(root / "characters" / "characters.json");
            f << R"({"chocola/casual":{"en":"Casual · Original","zh-CN":"日常·原版",)"
              << R"("zh-TW":"日常·原版","ja":"私服・オリジナル"}})";
        }
        std::ofstream(root / "backgrounds" / "BG_07A.png").put('x');
        {
            std::ofstream f(root / "backgrounds" / "background.json");
            f << R"({"BG_07A.png":{"en":"Seaside","zh-CN":"海滨餐厅露台",)"
              << R"("zh-TW":"海濱餐廳露台","ja":"海辺のレストラン"}})";
        }

        REQUIRE(controller.scan_assets(root));
        menu.sidebar.open = true;
        menu.sidebar.animation = 1.0f;
        ctx.init(&painter);
    }

    ~MenuFixture()
    {
        fs::remove_all(root);
    }

    void render()
    {
        render(controller.catalog());
    }

    void render(const nekomote::application::AssetCatalog& catalog)
    {
        painter.texts.clear();
        painter.rects.clear();
        painter.clips.clear();
        build_sidebar(ctx, painter, catalog, controller, menu, painter.vh);
        ctx.end_frame();
    }

    // Expands the drawer at the given index through a synthetic click on its
    // header row, which also verifies real hit-testing. Idempotent: a drawer
    // already open stays open, so sections can switch languages and re-render.
    void click_drawer(int index)
    {
        if (menu.sidebar.expanded_drawer != index)
        {
            render();
            const float header_y = 52.0f + static_cast<float>(index) * 42.0f;
            ctx.mouse_move(8.0f + 132.0f, header_y + 21.0f);
            ctx.mouse_button(0, 1);
            // The press frame toggles the drawer state; its content renders on
            // the next frame, mirroring the per-frame host loop.
            render();
            ctx.mouse_button(0, 0);
        }
        render();
        REQUIRE(menu.sidebar.expanded_drawer == index);
    }
};
} // namespace

TEST_CASE("Expanding a long drawer preserves free sidebar placement", "[ui_menu]")
{
    MenuFixture fixture;
    auto catalog = fixture.controller.catalog();
    catalog.character_groups.front().items.resize(50);
    auto& sidebar = fixture.menu.sidebar;
    sidebar.is_free = true;
    sidebar.x = 300.0f;
    sidebar.y = 600.0f;
    const auto render = [&]
    {
        fixture.ctx.sidebar(sidebar);
        fixture.render(catalog);
    };
    const auto click_first_drawer = [&]
    {
        fixture.ctx.mouse_move(sidebar.x + 100.0f, sidebar.y + 44.0f + 21.0f);
        fixture.ctx.mouse_button(0, 1);
        render();
        fixture.ctx.mouse_button(0, 0);
        render();
    };

    render();
    REQUIRE(sidebar.y == 600.0f);
    click_first_drawer();
    REQUIRE(sidebar.expanded_drawer == 0);
    REQUIRE(sidebar.height == 864.0f);
    REQUIRE(sidebar.x == 300.0f);
    REQUIRE(sidebar.y < 600.0f);
    REQUIRE(sidebar.y + sidebar.height <= static_cast<float>(fixture.painter.vh));
    REQUIRE(sidebar.is_free);
    REQUIRE_FALSE(sidebar.position_reset);

    click_first_drawer();
    REQUIRE(sidebar.expanded_drawer == -1);
    REQUIRE(sidebar.height < 864.0f);
    REQUIRE(sidebar.x == 300.0f);
    REQUIRE(sidebar.is_free);
    REQUIRE_FALSE(sidebar.position_reset);
}

TEST_CASE("Sidebar height follows four fifths of the output on resize", "[ui_menu]")
{
    MenuFixture fixture;
    auto catalog = fixture.controller.catalog();
    catalog.character_groups.front().items.resize(50);
    auto& sidebar = fixture.menu.sidebar;
    sidebar.expanded_drawer = 0;
    for (const int height : {1080, 720, 480, 80, 55, 32, 0})
    {
        CAPTURE(height);
        fixture.painter.vh = height;
        fixture.render(catalog);
        REQUIRE(sidebar.height == static_cast<float>(height) * 0.8f);
        REQUIRE_FALSE(fixture.painter.rects.empty());
        REQUIRE(fixture.painter.rects.front().h == sidebar.height);
        for (const auto& clip : fixture.painter.clips)
        {
            REQUIRE(clip.h >= 0.0f);
            REQUIRE(clip.y >= sidebar.y);
            REQUIRE(clip.y + clip.h <= sidebar.y + sidebar.height);
        }
        if (sidebar.height > 44.0f)
        {
            const auto& scrollbar = fixture.painter.rects.back();
            REQUIRE(scrollbar.y >= sidebar.y + 44.0f);
            REQUIRE(scrollbar.h > 0.0f);
            REQUIRE(scrollbar.y + scrollbar.h <= sidebar.y + sidebar.height);
        }
    }
}

TEST_CASE("Capped sidebar scroll reaches the footer and resets when collapsed", "[ui_menu]")
{
    MenuFixture fixture;
    fixture.painter.vh = 480;
    auto catalog = fixture.controller.catalog();
    catalog.character_groups.front().items.resize(50);
    auto& sidebar = fixture.menu.sidebar;
    sidebar.expanded_drawer = 0;
    sidebar.scroll_y = 1000000.0f;
    fixture.render(catalog);
    REQUIRE(sidebar.scroll_y > 0.0f);
    REQUIRE(sidebar.scroll_y < 1000000.0f);
    const auto* footer = fixture.painter.find("GitHub project: NekoMote");
    REQUIRE(footer != nullptr);
    REQUIRE(footer->y >= sidebar.y + 44.0f);
    REQUIRE(footer->y + 12.0f <= sidebar.y + sidebar.height);

    sidebar.expanded_drawer = -1;
    fixture.render(catalog);
    REQUIRE(sidebar.scroll_y == 0.0f);
    REQUIRE(sidebar.height < static_cast<float>(fixture.painter.vh) * 0.8f);
}

TEST_CASE("Capped sidebar ignores clicks on hidden content below its bounds", "[ui_menu]")
{
    MenuFixture fixture;
    fixture.painter.vh = 480;
    auto catalog = fixture.controller.catalog();
    catalog.character_groups.front().items.resize(9);
    auto& sidebar = fixture.menu.sidebar;
    sidebar.expanded_drawer = 0;
    fixture.render(catalog);

    // The background header lies below the clipped panel, but within the output.
    const float header_y = sidebar.y + 44.0f + 42.0f + 9.0f * 34.0f;
    REQUIRE(header_y > sidebar.y + sidebar.height);
    REQUIRE(header_y + 21.0f < static_cast<float>(fixture.painter.vh));
    fixture.ctx.mouse_move(sidebar.x + 100.0f, header_y + 21.0f);
    fixture.ctx.mouse_button(0, 1);
    fixture.render(catalog);
    REQUIRE(sidebar.expanded_drawer == 0);
    fixture.ctx.mouse_button(0, 0);
    fixture.render(catalog);

    // Scrolling the same header into view makes it actionable again.
    sidebar.scroll_y = 200.0f;
    fixture.render(catalog);
    fixture.ctx.mouse_move(sidebar.x + 100.0f, header_y + 21.0f - sidebar.scroll_y);
    fixture.ctx.mouse_button(0, 1);
    fixture.render(catalog);
    REQUIRE(sidebar.expanded_drawer == 1);
}

TEST_CASE("Sidebar localizes drawer and settings strings per config language", "[ui_menu]")
{
    MenuFixture fixture;
    // One character group: background drawer index 1, settings drawer index 2.
    const int background_drawer = 1;
    const int settings_drawer = 2;

    SECTION("english default")
    {
        REQUIRE(fixture.controller.config().language == Language::English);
        fixture.click_drawer(settings_drawer);
        REQUIRE(fixture.painter.contains("Background"));
        REQUIRE(fixture.painter.contains("Settings"));
        REQUIRE(fixture.painter.contains("Language"));
        REQUIRE(fixture.painter.contains("English"));
        REQUIRE(fixture.painter.contains("简体中文"));
        REQUIRE(fixture.painter.contains("繁體中文"));
        REQUIRE(fixture.painter.contains("日本語"));
        REQUIRE(fixture.painter.contains("Frame rate"));
        REQUIRE(fixture.painter.contains("Unlimited"));
        REQUIRE(fixture.painter.contains("Recommended"));
        REQUIRE(fixture.painter.contains("Character"));
        REQUIRE(fixture.painter.contains("Mute"));
        REQUIRE(fixture.painter.contains("Reset position"));
    }

    SECTION("simplified chinese")
    {
        fixture.controller.config().language = Language::ChineseSimplified;
        fixture.click_drawer(settings_drawer);
        REQUIRE(fixture.painter.contains("背景选择"));
        REQUIRE(fixture.painter.contains("设置"));
        REQUIRE(fixture.painter.contains("语言"));
        REQUIRE(fixture.painter.contains("帧率限制"));
        REQUIRE(fixture.painter.contains("无上限"));
        REQUIRE(fixture.painter.contains("推荐"));
        REQUIRE(fixture.painter.contains("角色"));
        REQUIRE(fixture.painter.contains("关闭声音"));
        REQUIRE(fixture.painter.contains("重设角色位置"));
    }

    SECTION("traditional chinese and japanese")
    {
        fixture.controller.config().language = Language::ChineseTraditional;
        fixture.click_drawer(settings_drawer);
        REQUIRE(fixture.painter.contains("背景選擇"));
        REQUIRE(fixture.painter.contains("設定"));
        REQUIRE(fixture.painter.contains("幀率限制"));
        REQUIRE(fixture.painter.contains("關閉聲音"));

        fixture.controller.config().language = Language::Japanese;
        fixture.click_drawer(settings_drawer);
        REQUIRE(fixture.painter.contains("背景"));
        REQUIRE(fixture.painter.contains("言語"));
        REQUIRE(fixture.painter.contains("フレームレート"));
        REQUIRE(fixture.painter.contains("ミュート"));
    }

    SECTION("background drawer renders before settings drawer")
    {
        fixture.controller.config().language = Language::English;
        fixture.click_drawer(settings_drawer);
        const auto background_pos = fixture.painter.index_of("Background");
        const auto settings_pos = fixture.painter.index_of("Settings");
        REQUIRE(background_pos != std::string::npos);
        REQUIRE(settings_pos != std::string::npos);
        REQUIRE(background_pos < settings_pos);
    }

    SECTION("background drawer lists localized static names")
    {
        fixture.controller.config().language = Language::English;
        fixture.click_drawer(background_drawer);
        REQUIRE(fixture.painter.contains("Static backgrounds"));
        REQUIRE(fixture.painter.contains("Seaside"));
        REQUIRE_FALSE(fixture.painter.contains("海滨餐厅露台"));

        fixture.controller.config().language = Language::ChineseSimplified;
        fixture.click_drawer(background_drawer);
        REQUIRE(fixture.painter.contains("固定背景"));
        REQUIRE(fixture.painter.contains("海滨餐厅露台"));
    }

    SECTION("character group and item names follow the language")
    {
        fixture.controller.config().language = Language::English;
        fixture.click_drawer(0);
        REQUIRE(fixture.painter.contains("Chocola"));
        REQUIRE(fixture.painter.contains("Casual · Original"));

        fixture.controller.config().language = Language::Japanese;
        fixture.click_drawer(0);
        REQUIRE(fixture.painter.contains("チョコラ"));
        REQUIRE(fixture.painter.contains("私服・オリジナル"));
    }
}

TEST_CASE("Language radio click switches config language and persists it", "[ui_menu]")
{
    MenuFixture fixture;
    const fs::path config_path = fixture.root / "config.json";
    fixture.controller.load_config(config_path);
    // First launch: the deterministic default background was selected.
    REQUIRE(fixture.controller.config().background == "BG_07A");

    fixture.click_drawer(2);
    const auto* option = fixture.painter.find("简体中文");
    REQUIRE(option != nullptr);
    // The text starts inside its radio row; click within the row bounds.
    fixture.ctx.mouse_move(option->x + 20.0f, option->y + 7.0f);
    fixture.ctx.mouse_button(0, 1);
    fixture.render();

    REQUIRE(fixture.controller.config().language == Language::ChineseSimplified);
    const auto saved = nekomote::application::WallpaperConfig::load(config_path);
    REQUIRE(saved.language == Language::ChineseSimplified);
    REQUIRE(saved.background == "BG_07A");
}

TEST_CASE("Position lock preference persists only after a panel action", "[ui_menu][config]")
{
    MenuFixture fixture;
    const auto path = fixture.root / "panel.json";
    fixture.controller.load_config(path);
    fixture.click_drawer(2);
    REQUIRE_FALSE(fs::exists(path));
    const auto* option = fixture.painter.find("Lock position and size");
    REQUIRE(option != nullptr);
    fixture.ctx.mouse_move(option->x + 20, option->y + 7);
    fixture.ctx.mouse_button(0, 1);
    fixture.render();
    REQUIRE(fixture.menu.locked);
    REQUIRE(nekomote::application::WallpaperConfig::load(path).lock_transform);
    fixture.ctx.mouse_button(0, 0);
    fixture.render();
    fixture.controller.set_no_save(true);
    fixture.ctx.mouse_button(0, 1);
    fixture.render();
    REQUIRE_FALSE(fixture.menu.locked);
    REQUIRE(nekomote::application::WallpaperConfig::load(path).lock_transform);
}
