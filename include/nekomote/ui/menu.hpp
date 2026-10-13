#pragma once

#include <nekomote/application/catalog.hpp>
#include <nekomote/application/controller.hpp>
#include <nekomote/ui/layout.hpp>
#include <nekomote/ui/state.hpp>

namespace nekomote::ui
{

struct MenuState
{
    SidebarState sidebar{};
    int fps_limit{60};
    bool locked{false};
    bool muted{false};
};

void build_sidebar(Context& ctx, Painter& painter, const application::AssetCatalog& catalog,
                   application::WallpaperController& controller, MenuState& menu,
                   int output_height);

} // namespace nekomote::ui
