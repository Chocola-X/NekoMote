#include <nekomote/ui/menu.hpp>

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace nekomote::ui
{

namespace
{
constexpr float kDrawerHeaderHeight = 42.0f;
constexpr float kDrawerItemHeight = 34.0f;
constexpr float kSectionHeaderHeight = 30.0f;
constexpr float kRadioRowHeight = 32.0f;
constexpr float kSwitchRowHeight = 34.0f;
constexpr float kSidebarHeaderHeight = 44.0f;
constexpr float kFooterHeight = 68.0f;
constexpr float kSidebarMaxHeightRatio = 4.0f / 5.0f;

struct FpsOption
{
    int fps;
    const char* label;
};
constexpr FpsOption kFpsOptions[] = {{0, ""},          {30, "30 FPS"},   {60, "60 FPS"},
                                     {120, "120 FPS"}, {180, "180 FPS"}, {240, "240 FPS"}};

// Sidebar strings per config language. FPS option labels ("30 FPS", "∞") and
// language option labels (shown in their own language) are shared.
struct UiStrings
{
    const char* background_drawer;
    const char* settings_drawer;
    const char* language_section;
    const char* fps_section;
    const char* unlimited;
    const char* recommended;
    const char* character_section;
    const char* lock_transform;
    const char* mute;
    const char* reset_position;
    const char* static_bg;
    const char* dynamic_bg;
    const char* author_line;
    const char* github_line;
};

constexpr UiStrings kStringsEn{"Background",
                               "Settings",
                               "Language",
                               "Frame rate",
                               "Unlimited",
                               "Recommended",
                               "Character",
                               "Lock position and size",
                               "Mute",
                               "Reset position",
                               "Static backgrounds",
                               "Dynamic backgrounds",
                               "Author: GTX690战术核显卡导弹",
                               "GitHub project: NekoMote"};
constexpr UiStrings kStringsZhCn{"背景选择",
                                 "设置",
                                 "语言",
                                 "帧率限制",
                                 "无上限",
                                 "推荐",
                                 "角色",
                                 "锁定人物位置与大小",
                                 "关闭声音",
                                 "重设角色位置",
                                 "固定背景",
                                 "动态背景",
                                 "作者：GTX690战术核显卡导弹",
                                 "Github项目：NekoMote"};
constexpr UiStrings kStringsZhTw{"背景選擇",
                                 "設定",
                                 "語言",
                                 "幀率限制",
                                 "無上限",
                                 "推薦",
                                 "角色",
                                 "鎖定人物位置與大小",
                                 "關閉聲音",
                                 "重設角色位置",
                                 "固定背景",
                                 "動態背景",
                                 "作者：GTX690战术核显卡导弹",
                                 "Github項目：NekoMote"};
constexpr UiStrings kStringsJa{"背景",
                               "設定",
                               "言語",
                               "フレームレート",
                               "無制限",
                               "推奨",
                               "キャラクター",
                               "位置とサイズを固定",
                               "ミュート",
                               "位置をリセット",
                               "静止画背景",
                               "動的背景",
                               "作者：GTX690战术核显卡导弹",
                               "GitHubプロジェクト：NekoMote"};

const UiStrings& ui_strings(application::Language lang) noexcept
{
    switch (lang)
    {
        case application::Language::ChineseSimplified:
            return kStringsZhCn;
        case application::Language::ChineseTraditional:
            return kStringsZhTw;
        case application::Language::Japanese:
            return kStringsJa;
        case application::Language::English:
            break;
    }
    return kStringsEn;
}
} // namespace

bool prepare_menu_text(FontMetrics& font, const application::AssetCatalog& catalog)
{
    std::string text = "NekoMote Wallpaper English 简体中文 繁體中文 日本語 ∞ ";
    const auto append = [&text](const auto& value)
    {
        text += value;
        text += ' ';
    };
    for (const auto& option : kFpsOptions)
        append(option.label);
    for (const auto* strings : {&kStringsEn, &kStringsZhCn, &kStringsZhTw, &kStringsJa})
        for (const auto* label :
             {strings->background_drawer, strings->settings_drawer, strings->language_section,
              strings->fps_section, strings->unlimited, strings->recommended,
              strings->character_section, strings->lock_transform, strings->mute,
              strings->reset_position, strings->static_bg, strings->dynamic_bg,
              strings->author_line, strings->github_line})
            append(label);
    for (std::size_t i = 0; i < application::kLanguageCount; ++i)
    {
        const auto lang = static_cast<application::Language>(i);
        for (const auto& group : catalog.character_groups)
        {
            append(group.title.resolve(lang, group.id));
            for (const auto& item : group.items)
                append(item.names.resolve(lang, item.fallback_name));
        }
        for (const auto& background : catalog.backgrounds)
            append(background.names.resolve(lang, background.name));
    }
    return font.prepare_text(text.c_str());
}

void build_sidebar(Context& ctx, Painter& painter, const application::AssetCatalog& catalog,
                   application::WallpaperController& controller, MenuState& menu, int output_height)
{
    auto& sidebar = menu.sidebar;
    if (sidebar.animation <= 0.001f)
        return;

    const Style style;
    const float sidebar_alpha = std::clamp(sidebar.animation, 0.0f, 1.0f);
    painter.set_global_alpha(sidebar_alpha);

    const auto lang = controller.config().language;
    const UiStrings& txt = ui_strings(lang);

    const int background_drawer_index = static_cast<int>(catalog.character_groups.size());
    const int settings_drawer_index = background_drawer_index + 1;

    float content_scrollable_h = 0.0f;
    for (std::size_t g = 0; g < catalog.character_groups.size(); ++g)
    {
        content_scrollable_h += kDrawerHeaderHeight;
        if (sidebar.expanded_drawer == static_cast<int>(g))
            content_scrollable_h +=
                static_cast<float>(catalog.character_groups[g].items.size()) * kDrawerItemHeight;
    }

    content_scrollable_h += kDrawerHeaderHeight;
    if (sidebar.expanded_drawer == background_drawer_index)
    {
        std::size_t static_bg_count = 0, dynamic_bg_count = 0;
        for (const auto& bg : catalog.backgrounds)
            if (bg.type == application::BackgroundType::Dynamic)
                ++dynamic_bg_count;
            else
                ++static_bg_count;
        if (static_bg_count > 0)
            content_scrollable_h +=
                kSectionHeaderHeight + static_cast<float>(static_bg_count) * kRadioRowHeight;
        if (dynamic_bg_count > 0)
            content_scrollable_h +=
                kSectionHeaderHeight + static_cast<float>(dynamic_bg_count) * kRadioRowHeight;
    }

    content_scrollable_h += kDrawerHeaderHeight;
    if (sidebar.expanded_drawer == settings_drawer_index)
    {
        content_scrollable_h += kSectionHeaderHeight + 4.0f * kRadioRowHeight;
        content_scrollable_h +=
            kSectionHeaderHeight + static_cast<float>(std::size(kFpsOptions)) * kRadioRowHeight;
        content_scrollable_h += kSectionHeaderHeight + 2.0f * kSwitchRowHeight + kSwitchRowHeight;
    }

    const float max_sidebar_h =
        std::max(0.0f, static_cast<float>(output_height)) * kSidebarMaxHeightRatio;
    const float content_total_h = kSidebarHeaderHeight + content_scrollable_h + kFooterHeight;
    sidebar.height = std::min(max_sidebar_h, content_total_h);
    ctx.validate_sidebar_position(sidebar);
    const float sidebar_x = sidebar.x;
    const float sidebar_y = sidebar.y;

    // Fixed chrome keeps both the content and scrollbar away from the panel's
    // rounded bottom. On tiny outputs, clip the header/footer without overlap.
    const float header_h = std::min(kSidebarHeaderHeight, sidebar.height);
    const float footer_h = std::min(kFooterHeight, sidebar.height - header_h);
    const float clip_y = sidebar_y + header_h;
    const float clip_h = std::max(0.0f, sidebar.height - header_h - footer_h);
    const float footer_top = clip_y + clip_h;
    const float max_scroll = clip_h > 0.0f ? std::max(0.0f, content_scrollable_h - clip_h) : 0.0f;
    if (clip_h > 0.0f && ctx.mouse_in_rect(sidebar_x, clip_y, sidebar.width, clip_h))
        sidebar.scroll_y -= ctx.scroll_delta() * kRadioRowHeight;
    sidebar.scroll_y = std::clamp(sidebar.scroll_y, 0.0f, max_scroll);

    painter.draw_rounded_rect(sidebar_x, sidebar_y, sidebar.width, sidebar.height,
                              style.corner_radius, style.panel_bg, style.accent, 2.0f);
    if (header_h > 0.0f)
    {
        painter.push_clip_rect(sidebar_x, sidebar_y, sidebar.width, header_h);
        ctx.sidebar_header(sidebar, "NekoMote Wallpaper", style);
        painter.pop_clip_rect();
    }
    if (footer_h > 0.0f)
    {
        painter.push_clip_rect(sidebar_x + style.padding, footer_top,
                               std::max(0.0f, sidebar.width - 2.0f * style.padding), footer_h);
        float footer_y = footer_top;
        ctx.divider_line(sidebar_x, footer_y, sidebar.width, style);
        footer_y += 10.0f;
        const float atw =
            painter.font() ? painter.font()->text_width(txt.author_line, 12.0f) : 160.0f;
        ctx.label(txt.author_line, sidebar_x + (sidebar.width - atw) * 0.5f, footer_y, 12.0f,
                  style.text);
        footer_y += 24.0f;
        ctx.divider_line(sidebar_x, footer_y, sidebar.width, style);
        footer_y += 10.0f;
        const float gtw =
            painter.font() ? painter.font()->text_width(txt.github_line, 12.0f) : 170.0f;
        ctx.label(txt.github_line, sidebar_x + (sidebar.width - gtw) * 0.5f, footer_y, 12.0f,
                  style.text);
        painter.pop_clip_rect();
    }
    if (clip_h <= 0.0f)
    {
        painter.set_global_alpha(1.0f);
        return;
    }

    painter.push_clip_rect(sidebar_x, clip_y, sidebar.width, clip_h);
    ctx.set_input_clip_rect(sidebar_x, clip_y, sidebar.width, clip_h);

    float curr_y = clip_y - sidebar.scroll_y;

    for (std::size_t g = 0; g < catalog.character_groups.size(); ++g)
    {
        const auto& group = catalog.character_groups[g];
        const bool is_open = (sidebar.expanded_drawer == static_cast<int>(g));
        const std::string& group_title = group.title.resolve(lang, group.id);
        if (ctx.drawer_header(group_title.c_str(), is_open, sidebar_x, curr_y, sidebar.width,
                              kDrawerHeaderHeight, style))
            sidebar.expanded_drawer = is_open ? -1 : static_cast<int>(g);
        curr_y += kDrawerHeaderHeight;
        if (is_open)
        {
            for (const auto& item : group.items)
            {
                const bool is_selected =
                    (item.character_index == controller.current_character_index());
                const std::string& item_label = item.names.resolve(lang, item.fallback_name);
                if (ctx.drawer_item(item_label.c_str(), is_selected, sidebar_x, curr_y,
                                    sidebar.width, kDrawerItemHeight, style))
                    controller.request_switch_character(item.character_index);
                curr_y += kDrawerItemHeight;
            }
        }
    }

    const bool is_background_open = (sidebar.expanded_drawer == background_drawer_index);
    if (ctx.drawer_header(txt.background_drawer, is_background_open, sidebar_x, curr_y,
                          sidebar.width, kDrawerHeaderHeight, style))
        sidebar.expanded_drawer = is_background_open ? -1 : background_drawer_index;
    curr_y += kDrawerHeaderHeight;
    if (is_background_open)
    {
        const auto draw_background_rows =
            [&](application::BackgroundType type, const char* section_label)
        {
            bool has_any = false;
            for (const auto& bg : catalog.backgrounds)
                if (bg.type == type)
                    has_any = true;
            if (!has_any)
                return;
            ctx.section_header(section_label, sidebar_x, curr_y, sidebar.width,
                               kSectionHeaderHeight, style);
            curr_y += kSectionHeaderHeight;
            for (std::size_t b = 0; b < catalog.backgrounds.size(); ++b)
            {
                if (catalog.backgrounds[b].type != type)
                    continue;
                const auto& bg = catalog.backgrounds[b];
                const bool selected =
                    (static_cast<int>(b) == controller.current_background_index());
                const std::string& bg_label = bg.names.resolve(lang, bg.name);
                if (ctx.radio_row(bg_label.c_str(), "", selected, sidebar_x, curr_y, sidebar.width,
                                  kRadioRowHeight, style))
                    controller.request_switch_background(static_cast<int>(b));
                curr_y += kRadioRowHeight;
            }
        };
        draw_background_rows(application::BackgroundType::Static, txt.static_bg);
        draw_background_rows(application::BackgroundType::Dynamic, txt.dynamic_bg);
    }

    const bool is_settings_open = (sidebar.expanded_drawer == settings_drawer_index);
    if (ctx.drawer_header(txt.settings_drawer, is_settings_open, sidebar_x, curr_y, sidebar.width,
                          kDrawerHeaderHeight, style))
        sidebar.expanded_drawer = is_settings_open ? -1 : settings_drawer_index;
    curr_y += kDrawerHeaderHeight;
    if (is_settings_open)
    {
        ctx.section_header(txt.language_section, sidebar_x, curr_y, sidebar.width,
                           kSectionHeaderHeight, style);
        curr_y += kSectionHeaderHeight;
        // Language options are labeled in their own language so users can
        // find their language regardless of the current setting.
        const struct
        {
            application::Language lang;
            const char* label;
        } lang_opts[] = {{application::Language::English, "English"},
                         {application::Language::ChineseSimplified, "简体中文"},
                         {application::Language::ChineseTraditional, "繁體中文"},
                         {application::Language::Japanese, "日本語"}};
        for (const auto& opt : lang_opts)
        {
            const bool selected = (lang == opt.lang);
            if (ctx.radio_row(opt.label, "", selected, sidebar_x, curr_y, sidebar.width,
                              kRadioRowHeight, style))
            {
                controller.config().language = opt.lang;
                if (!controller.no_save())
                    controller.save_config(controller.config_path());
            }
            curr_y += kRadioRowHeight;
        }

        ctx.section_header(txt.fps_section, sidebar_x, curr_y, sidebar.width, kSectionHeaderHeight,
                           style);
        curr_y += kSectionHeaderHeight;
        for (const auto& opt : kFpsOptions)
        {
            const bool selected = (menu.fps_limit == opt.fps);
            const auto* label = opt.fps == 0 ? txt.unlimited : opt.label;
            const auto* badge = opt.fps == 0 ? "∞" : (opt.fps == 60 ? txt.recommended : "");
            if (ctx.radio_row(label, badge, selected, sidebar_x, curr_y, sidebar.width,
                              kRadioRowHeight, style))
            {
                menu.fps_limit = opt.fps;
                controller.config().fps = opt.fps;
                if (!controller.no_save())
                    controller.save_config(controller.config_path());
            }
            curr_y += kRadioRowHeight;
        }

        ctx.section_header(txt.character_section, sidebar_x, curr_y, sidebar.width,
                           kSectionHeaderHeight, style);
        curr_y += kSectionHeaderHeight;
        if (ctx.switch_row(txt.lock_transform, menu.locked, sidebar_x, curr_y, sidebar.width,
                           kSwitchRowHeight, style))
        {
            controller.config().lock_transform = menu.locked;
            if (!controller.no_save())
                controller.save_config(controller.config_path());
        }
        curr_y += kSwitchRowHeight;
        bool is_muted = menu.muted;
        if (ctx.switch_row(txt.mute, is_muted, sidebar_x, curr_y, sidebar.width, kSwitchRowHeight,
                           style))
        {
            menu.muted = is_muted;
            controller.config().mute = is_muted;
            if (!controller.no_save())
                controller.save_config(controller.config_path());
        }
        curr_y += kSwitchRowHeight;
        if (ctx.reset_button(txt.reset_position, sidebar_x, curr_y, sidebar.width, kSwitchRowHeight,
                             style))
            controller.reset_character_transform();
        curr_y += kSwitchRowHeight;
    }

    ctx.draw_scrollbar(sidebar_x, clip_y, sidebar.width, clip_h, content_scrollable_h,
                       sidebar.scroll_y, style);
    ctx.clear_input_clip_rect();
    painter.pop_clip_rect();
    painter.set_global_alpha(1.0f);
}

} // namespace nekomote::ui
