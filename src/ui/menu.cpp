#include <nekomote/ui/menu.hpp>

#include <algorithm>
#include <cstddef>

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
constexpr float kSidebarMargin = 16.0f;
constexpr float kSidebarMinHeight = 64.0f;
} // namespace

void build_sidebar(Context& ctx, Painter& painter, const application::AssetCatalog& catalog,
                   application::WallpaperController& controller, MenuState& menu, int output_width,
                   int output_height)
{
    auto& sidebar = menu.sidebar;
    if (sidebar.animation <= 0.001f)
        return;

    const Style style;
    const float sidebar_alpha = std::clamp(sidebar.animation, 0.0f, 1.0f);
    painter.set_global_alpha(sidebar_alpha);

    const float sidebar_x = sidebar.x;
    const float sidebar_y = sidebar.y;

    const int settings_drawer_index = static_cast<int>(catalog.character_groups.size());

    float content_scrollable_h = 0.0f;
    for (std::size_t g = 0; g < catalog.character_groups.size(); ++g)
    {
        content_scrollable_h += kDrawerHeaderHeight;
        if (sidebar.expanded_drawer == static_cast<int>(g))
            content_scrollable_h +=
                static_cast<float>(catalog.character_groups[g].items.size()) * kDrawerItemHeight;
    }
    content_scrollable_h += kDrawerHeaderHeight;
    if (sidebar.expanded_drawer == settings_drawer_index)
    {
        content_scrollable_h += kSectionHeaderHeight + 4.0f * kRadioRowHeight;
        content_scrollable_h += kSectionHeaderHeight + 2.0f * kSwitchRowHeight + kSwitchRowHeight;
        std::size_t static_bg_count = 0, dynamic_bg_count = 0;
        for (const auto& bg : catalog.backgrounds)
            if (bg.type == application::BackgroundType::Dynamic)
                ++dynamic_bg_count;
            else
                ++static_bg_count;
        content_scrollable_h += kSectionHeaderHeight;
        if (static_bg_count > 0)
            content_scrollable_h +=
                kSectionHeaderHeight + static_cast<float>(static_bg_count) * kRadioRowHeight;
        if (dynamic_bg_count > 0)
            content_scrollable_h +=
                kSectionHeaderHeight + static_cast<float>(dynamic_bg_count) * kRadioRowHeight;
    }
    content_scrollable_h += kFooterHeight;

    const float max_sidebar_h =
        std::max(kSidebarMinHeight, static_cast<float>(output_height) - kSidebarMargin);
    const float content_total_h = kSidebarHeaderHeight + content_scrollable_h;
    sidebar.height = std::min(max_sidebar_h, content_total_h);

    const float max_scroll =
        std::max(0.0f, content_scrollable_h - (sidebar.height - kSidebarHeaderHeight));
    sidebar.scroll_y = std::clamp(sidebar.scroll_y, 0.0f, max_scroll);

    painter.draw_rounded_rect(sidebar_x, sidebar_y, sidebar.width, sidebar.height,
                              style.corner_radius, style.panel_bg, style.accent, 2.0f);
    ctx.sidebar_header(sidebar, "猫娘乐园角色E-mote图鉴", style);

    const float clip_y = sidebar_y + kSidebarHeaderHeight;
    const float clip_h = sidebar.height - kSidebarHeaderHeight;
    painter.push_clip_rect(sidebar_x, clip_y, sidebar.width, clip_h);

    float curr_y = clip_y - sidebar.scroll_y;

    for (std::size_t g = 0; g < catalog.character_groups.size(); ++g)
    {
        const auto& group = catalog.character_groups[g];
        const bool is_open = (sidebar.expanded_drawer == static_cast<int>(g));
        if (ctx.drawer_header(group.title.c_str(), is_open, sidebar_x, curr_y, sidebar.width,
                              kDrawerHeaderHeight, style))
            sidebar.expanded_drawer = is_open ? -1 : static_cast<int>(g);
        curr_y += kDrawerHeaderHeight;
        if (is_open)
        {
            for (const auto& item : group.items)
            {
                const bool is_selected =
                    (item.character_index == controller.current_character_index());
                if (ctx.drawer_item(item.display_name.c_str(), is_selected, sidebar_x, curr_y,
                                    sidebar.width, kDrawerItemHeight, style))
                    controller.request_switch_character(item.character_index);
                curr_y += kDrawerItemHeight;
            }
        }
    }

    const bool is_settings_open = (sidebar.expanded_drawer == settings_drawer_index);
    if (ctx.drawer_header("设置", is_settings_open, sidebar_x, curr_y, sidebar.width,
                          kDrawerHeaderHeight, style))
        sidebar.expanded_drawer = is_settings_open ? -1 : settings_drawer_index;
    curr_y += kDrawerHeaderHeight;
    if (is_settings_open)
    {
        ctx.section_header("帧率限制", sidebar_x, curr_y, sidebar.width, kSectionHeaderHeight,
                           style);
        curr_y += kSectionHeaderHeight;
        const struct
        {
            int fps;
            const char* label;
            const char* badge;
        } fps_opts[] = {
            {0, "无上限", "∞"}, {30, "30 FPS", ""}, {60, "60 FPS", "推荐"}, {120, "120 FPS", ""}};
        for (const auto& opt : fps_opts)
        {
            const bool selected = (menu.fps_limit == opt.fps);
            if (ctx.radio_row(opt.label, opt.badge, selected, sidebar_x, curr_y, sidebar.width,
                              kRadioRowHeight, style))
            {
                menu.fps_limit = opt.fps;
                controller.config().fps = opt.fps;
                if (!controller.no_save())
                    controller.save_config(controller.config_path());
            }
            curr_y += kRadioRowHeight;
        }

        ctx.section_header("角色", sidebar_x, curr_y, sidebar.width, kSectionHeaderHeight, style);
        curr_y += kSectionHeaderHeight;
        ctx.switch_row("锁定人物位置与大小", menu.locked, sidebar_x, curr_y, sidebar.width,
                       kSwitchRowHeight, style);
        curr_y += kSwitchRowHeight;
        bool is_muted = menu.muted;
        if (ctx.switch_row("关闭声音", is_muted, sidebar_x, curr_y, sidebar.width, kSwitchRowHeight,
                           style))
        {
            menu.muted = is_muted;
            controller.config().mute = is_muted;
            if (!controller.no_save())
                controller.save_config(controller.config_path());
        }
        curr_y += kSwitchRowHeight;
        if (ctx.reset_button("重设角色位置", sidebar_x, curr_y, sidebar.width, kSwitchRowHeight,
                             style))
        {
            if (output_width > 0 && output_height > 0)
                controller.fit_to_output(output_width, output_height);
        }
        curr_y += kSwitchRowHeight;

        ctx.section_header("壁纸选择", sidebar_x, curr_y, sidebar.width, kSectionHeaderHeight,
                           style);
        curr_y += kSectionHeaderHeight;
        bool has_static = false, has_dynamic = false;
        for (const auto& bg : catalog.backgrounds)
            if (bg.type == application::BackgroundType::Dynamic)
                has_dynamic = true;
            else
                has_static = true;
        if (has_static)
        {
            ctx.section_header("固定背景", sidebar_x, curr_y, sidebar.width, kSectionHeaderHeight,
                               style);
            curr_y += kSectionHeaderHeight;
            for (std::size_t b = 0; b < catalog.backgrounds.size(); ++b)
            {
                if (catalog.backgrounds[b].type != application::BackgroundType::Static)
                    continue;
                const auto& bg = catalog.backgrounds[b];
                const bool selected =
                    (static_cast<int>(b) == controller.current_background_index());
                if (ctx.radio_row(bg.display_name.c_str(), "", selected, sidebar_x, curr_y,
                                  sidebar.width, kRadioRowHeight, style))
                    controller.request_switch_background(static_cast<int>(b));
                curr_y += kRadioRowHeight;
            }
        }
        if (has_dynamic)
        {
            ctx.section_header("动态背景", sidebar_x, curr_y, sidebar.width, kSectionHeaderHeight,
                               style);
            curr_y += kSectionHeaderHeight;
            for (std::size_t b = 0; b < catalog.backgrounds.size(); ++b)
            {
                if (catalog.backgrounds[b].type != application::BackgroundType::Dynamic)
                    continue;
                const auto& bg = catalog.backgrounds[b];
                const bool selected =
                    (static_cast<int>(b) == controller.current_background_index());
                if (ctx.radio_row(bg.display_name.c_str(), "", selected, sidebar_x, curr_y,
                                  sidebar.width, kRadioRowHeight, style))
                    controller.request_switch_background(static_cast<int>(b));
                curr_y += kRadioRowHeight;
            }
        }
    }

    ctx.divider_line(sidebar_x, curr_y, sidebar.width, style);
    curr_y += 10.0f;
    const char* author_txt = "作者：GTX690战术核显卡导弹";
    const float atw = painter.font() ? painter.font()->text_width(author_txt, 12.0f) : 160.0f;
    ctx.label(author_txt, sidebar_x + (sidebar.width - atw) * 0.5f, curr_y, 12.0f, style.text);
    curr_y += 24.0f;
    ctx.divider_line(sidebar_x, curr_y, sidebar.width, style);
    curr_y += 10.0f;
    const char* gh_txt = "Github项目：NekoWebShow";
    const float gtw = painter.font() ? painter.font()->text_width(gh_txt, 12.0f) : 170.0f;
    ctx.label(gh_txt, sidebar_x + (sidebar.width - gtw) * 0.5f, curr_y, 12.0f, style.text);
    curr_y += 24.0f;

    painter.pop_clip_rect();
    ctx.draw_scrollbar(sidebar_x, clip_y, sidebar.width, clip_h, content_scrollable_h,
                       sidebar.scroll_y, style);
    painter.set_global_alpha(1.0f);
}

} // namespace nekomote::ui
